/**************************************************************************/
/*  ecs_archetype.h                                                       */
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

#include "ecs_component.h"
#include "ecs_entity.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

namespace ecs {

// An archetype stores every entity that has exactly the same set of (table)
// components. Storage is structure-of-arrays: one tightly packed column per
// component type, plus a column of entity handles and a per-column array of
// change ticks. Rows are removed with swap-remove so the columns never have
// holes, which is what makes query iteration a linear scan over memory.
//
// Archetypes are connected in a graph: `add_edges[c]` is the archetype that
// results from adding component `c`, `remove_edges[c]` from removing it. The
// World caches these edges lazily, so repeated add/remove of the same
// component between the same two archetypes becomes a single hash lookup.
class Archetype {
public:
	struct Column {
		const ComponentType *type = nullptr;
		uint8_t *data = nullptr;
		uint32_t *change_ticks = nullptr;
		uint32_t stride = 0;
	};

private:
	ArchetypeId id = INVALID_ARCHETYPE;
	ComponentMask mask;
	LocalVector<ComponentId> types;
	LocalVector<Column> columns;
	LocalVector<Entity> entities;
	LocalVector<int16_t> column_of; // ComponentId -> column index, -1 if absent.
	uint32_t capacity = 0;

	HashMap<ComponentId, Archetype *> add_edges;
	HashMap<ComponentId, Archetype *> remove_edges;

	void _grow(uint32_t p_min_capacity);

public:
	_FORCE_INLINE_ ArchetypeId get_id() const { return id; }
	_FORCE_INLINE_ const ComponentMask &get_mask() const { return mask; }
	_FORCE_INLINE_ const LocalVector<ComponentId> &get_types() const { return types; }
	_FORCE_INLINE_ uint32_t size() const { return entities.size(); }
	_FORCE_INLINE_ uint32_t get_capacity() const { return capacity; }
	_FORCE_INLINE_ uint32_t get_column_count() const { return columns.size(); }
	_FORCE_INLINE_ bool has(ComponentId p_component) const { return mask.test(p_component); }

	_FORCE_INLINE_ int32_t get_column_index(ComponentId p_component) const {
		return p_component < column_of.size() ? column_of[p_component] : -1;
	}
	_FORCE_INLINE_ Column &get_column(uint32_t p_column) { return columns[p_column]; }
	_FORCE_INLINE_ const Column &get_column(uint32_t p_column) const { return columns[p_column]; }

	_FORCE_INLINE_ void *get(uint32_t p_column, uint32_t p_row) {
		const Column &c = columns[p_column];
		return c.data ? c.data + size_t(p_row) * c.stride : nullptr;
	}
	_FORCE_INLINE_ const void *get(uint32_t p_column, uint32_t p_row) const {
		const Column &c = columns[p_column];
		return c.data ? c.data + size_t(p_row) * c.stride : nullptr;
	}
	// Raw column base pointer, for tight native loops.
	template <typename T>
	_FORCE_INLINE_ T *column_ptr(uint32_t p_column) {
		return reinterpret_cast<T *>(columns[p_column].data);
	}
	template <typename T>
	_FORCE_INLINE_ const T *column_ptr(uint32_t p_column) const {
		return reinterpret_cast<const T *>(columns[p_column].data);
	}

	_FORCE_INLINE_ const Entity *entities_ptr() const { return entities.ptr(); }
	_FORCE_INLINE_ Entity get_entity(uint32_t p_row) const { return entities[p_row]; }

	_FORCE_INLINE_ void mark_changed(uint32_t p_column, uint32_t p_row, uint32_t p_tick) {
		columns[p_column].change_ticks[p_row] = p_tick;
	}
	_FORCE_INLINE_ uint32_t get_change_tick(uint32_t p_column, uint32_t p_row) const {
		return columns[p_column].change_ticks[p_row];
	}
	_FORCE_INLINE_ const uint32_t *change_ticks_ptr(uint32_t p_column) const {
		return columns[p_column].change_ticks;
	}

	// Allocates a row for p_entity without constructing its components.
	// The caller must construct (or move into) every column.
	uint32_t alloc_row(Entity p_entity, uint32_t p_tick);
	// Allocates a row and default-constructs every component.
	uint32_t add_row(Entity p_entity, uint32_t p_tick);
	// Destructs the components of the row and swap-removes it. Returns the
	// entity that was moved into p_row (the previous last row), or a null
	// entity when p_row was the last row.
	Entity remove_row(uint32_t p_row);

	void reserve(uint32_t p_capacity);

	// Bytes allocated for this archetype (columns, change ticks, entity list).
	uint64_t get_memory_usage() const;

	_FORCE_INLINE_ Archetype *get_add_edge(ComponentId p_component) const {
		Archetype *const *a = add_edges.getptr(p_component);
		return a ? *a : nullptr;
	}
	_FORCE_INLINE_ Archetype *get_remove_edge(ComponentId p_component) const {
		Archetype *const *a = remove_edges.getptr(p_component);
		return a ? *a : nullptr;
	}
	_FORCE_INLINE_ void set_add_edge(ComponentId p_component, Archetype *p_to) { add_edges.insert(p_component, p_to); }
	_FORCE_INLINE_ void set_remove_edge(ComponentId p_component, Archetype *p_to) { remove_edges.insert(p_component, p_to); }

	Archetype(ArchetypeId p_id, const ComponentMask &p_mask, const LocalVector<const ComponentType *> &p_types);
	~Archetype();

	Archetype(const Archetype &) = delete;
	Archetype &operator=(const Archetype &) = delete;
};

} // namespace ecs
