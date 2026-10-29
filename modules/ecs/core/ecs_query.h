/**************************************************************************/
/*  ecs_query.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "ecs_archetype.h"
#include "ecs_world.h"

#include "core/object/worker_thread_pool.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace ecs {

// A query selects every entity whose component set contains all of `include`
// and none of `exclude`. Matching happens at the archetype level with two
// bitset operations, and the list of matching archetypes is cached: since
// archetypes are never destroyed and are appended to the world's list, the
// query only needs to test archetypes created after its last refresh.
//
// Sparse components (COMPONENT_FLAG_SPARSE) are not part of archetype
// signatures, so include/exclude terms referring to them are checked per
// entity against the sparse sets.
class Query {
	World *world = nullptr;
	ComponentMask include_table;
	ComponentMask exclude_table;
	LocalVector<ComponentId> include_sparse;
	LocalVector<ComponentId> exclude_sparse;
	LocalVector<Archetype *> matched;
	uint32_t checked_archetypes = 0;

	_FORCE_INLINE_ bool _matches(const Archetype *p_archetype) const {
		const ComponentMask &m = p_archetype->get_mask();
		return m.contains_all(include_table) && !m.intersects(exclude_table);
	}

	template <typename T>
	using Bare = std::remove_const_t<std::remove_reference_t<T>>;

	template <typename... Ts, typename F, size_t... Is>
	void _each_impl(F &&p_fn, std::index_sequence<Is...>) {
		constexpr size_t N = sizeof...(Ts);
		ComponentId ids[N > 0 ? N : 1] = { world->component_id<Bare<Ts>>()... };
		constexpr bool writes[N > 0 ? N : 1] = { !std::is_const_v<std::remove_reference_t<Ts>>... };
		const uint32_t tick = world->get_tick();
		const bool sparse_terms = !include_sparse.is_empty() || !exclude_sparse.is_empty();
		for (Archetype *a : matched) {
			uint32_t count = a->size();
			if (count == 0) {
				continue;
			}
			int32_t cols[N > 0 ? N : 1];
			for (size_t i = 0; i < N; i++) {
				cols[i] = a->get_column_index(ids[i]);
				DEV_ASSERT(cols[i] >= 0);
			}
			uint8_t *ptrs[N > 0 ? N : 1];
			uint32_t strides[N > 0 ? N : 1];
			for (size_t i = 0; i < N; i++) {
				ptrs[i] = a->get_column(cols[i]).data;
				strides[i] = a->get_column(cols[i]).stride;
			}
			const Entity *ents = a->entities_ptr();
			for (uint32_t row = 0; row < count; row++) {
				if (sparse_terms && !matches_sparse(ents[row])) {
					continue;
				}
				p_fn(ents[row], *reinterpret_cast<std::remove_reference_t<Ts> *>(ptrs[Is] + size_t(row) * strides[Is])...);
			}
			// Mark written columns as changed for the whole range.
			for (size_t i = 0; i < N; i++) {
				if (writes[i]) {
					uint32_t *ticks = a->get_column(cols[i]).change_ticks;
					for (uint32_t row = 0; row < count; row++) {
						ticks[row] = tick;
					}
				}
			}
		}
	}

public:
	void init(World *p_world, const ComponentMask &p_include, const ComponentMask &p_exclude = ComponentMask());

	// Convenience: build from native types.
	template <typename... Ts>
	void init_with(World *p_world, const ComponentMask &p_exclude = ComponentMask()) {
		ComponentMask inc;
		ComponentId ids[] = { p_world->component_id<Ts>()..., INVALID_COMPONENT };
		for (ComponentId id : ids) {
			if (id != INVALID_COMPONENT) {
				inc.set(id);
			}
		}
		init(p_world, inc, p_exclude);
	}

	_FORCE_INLINE_ World *get_world() const { return world; }
	_FORCE_INLINE_ const ComponentMask &get_include_mask() const { return include_table; }
	_FORCE_INLINE_ const ComponentMask &get_exclude_mask() const { return exclude_table; }
	_FORCE_INLINE_ bool has_sparse_terms() const { return !include_sparse.is_empty() || !exclude_sparse.is_empty(); }

	// Incrementally matches archetypes created since the last call.
	void refresh();

	_FORCE_INLINE_ const LocalVector<Archetype *> &get_archetypes() {
		refresh();
		return matched;
	}

	// Per entity test of the sparse include/exclude terms.
	bool matches_sparse(Entity p_entity) const;
	// True if the entity matches every term of the query.
	bool matches(Entity p_entity) const;

	// Number of matching entities.
	uint32_t count();

	// Calls p_fn(Archetype &) for every matching archetype.
	template <typename F>
	void for_each_archetype(F &&p_fn) {
		refresh();
		for (Archetype *a : matched) {
			if (a->size()) {
				p_fn(*a);
			}
		}
	}

	// Calls p_fn(Entity, Archetype &, uint32_t row) for every matching entity.
	template <typename F>
	void for_each(F &&p_fn) {
		refresh();
		const bool sparse_terms = has_sparse_terms();
		for (Archetype *a : matched) {
			uint32_t count = a->size();
			const Entity *ents = a->entities_ptr();
			for (uint32_t row = 0; row < count; row++) {
				if (sparse_terms && !matches_sparse(ents[row])) {
					continue;
				}
				p_fn(ents[row], *a, row);
			}
		}
	}

	// Typed iteration: p_fn(Entity, Ts &...). Use `const T` for read-only
	// access; non-const columns are marked changed. All Ts must be table
	// components present in the include set.
	template <typename... Ts, typename F>
	void each(F &&p_fn) {
		refresh();
		_each_impl<Ts...>(std::forward<F>(p_fn), std::index_sequence_for<Ts...>{});
	}

	// Splits the matching archetypes into row ranges of at least
	// p_min_batch_size rows and processes them on the WorkerThreadPool.
	// p_fn receives (Archetype &, row_begin, row_end, userdata).
	typedef void (*ParallelFunc)(Archetype &p_archetype, uint32_t p_row_begin, uint32_t p_row_end, void *p_userdata);
	void for_each_parallel(ParallelFunc p_fn, void *p_userdata, uint32_t p_min_batch_size = 1024);

	// Collects the matching entities.
	void get_entities(LocalVector<Entity> &r_entities);

	Query() {}
	Query(World *p_world, const ComponentMask &p_include, const ComponentMask &p_exclude = ComponentMask()) {
		init(p_world, p_include, p_exclude);
	}
};

} // namespace ecs
