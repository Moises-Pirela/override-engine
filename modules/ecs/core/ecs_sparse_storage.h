/**************************************************************************/
/*  ecs_sparse_storage.h                                                  */
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
#include "ecs_sparse_set.h"

namespace ecs {

// Storage for components flagged COMPONENT_FLAG_SPARSE.
//
// Entity index -> dense slot is resolved with a paged sparse set, and the
// component payloads are kept in a tightly packed blob array (swap-removed
// together with the sparse set's dense array). Adding / removing a sparse
// component never migrates the entity to another archetype, so it is the
// right choice for state that flips often (Selected, Damaged, Hidden, ...)
// or for rarely present data that would otherwise fragment archetypes.
class SparseComponentStorage {
	const ComponentType *type = nullptr;
	ECSSparseSet<uint8_t> index; // Value unused; dense order is mirrored by `data`.
	uint8_t *data = nullptr;
	uint32_t *change_ticks = nullptr;
	uint32_t capacity = 0;

	void _grow(uint32_t p_min) {
		uint32_t new_capacity = MAX(capacity * 2, MAX(p_min, 16u));
		uint32_t count = index.size();
		uint32_t *new_ticks = (uint32_t *)memalloc(sizeof(uint32_t) * new_capacity);
		if (change_ticks) {
			memcpy(new_ticks, change_ticks, sizeof(uint32_t) * count);
			memfree(change_ticks);
		}
		change_ticks = new_ticks;
		if (type->size) {
			uint8_t *new_data = (uint8_t *)memalloc(size_t(type->size) * new_capacity);
			if (data) {
				if (type->trivial || type->move == nullptr) {
					memcpy(new_data, data, size_t(type->size) * count);
				} else {
					for (uint32_t i = 0; i < count; i++) {
						type->move_construct(new_data + size_t(i) * type->size, data + size_t(i) * type->size);
						type->destruct_at(data + size_t(i) * type->size);
					}
				}
				memfree(data);
			}
			data = new_data;
		}
		capacity = new_capacity;
	}

public:
	_FORCE_INLINE_ const ComponentType *get_type() const { return type; }
	_FORCE_INLINE_ uint32_t size() const { return index.size(); }
	_FORCE_INLINE_ bool has(uint32_t p_entity_index) const { return index.has(p_entity_index); }

	_FORCE_INLINE_ void *get(uint32_t p_entity_index) {
		uint32_t slot = index.index_of(p_entity_index);
		return slot == ECSSparseSet<uint8_t>::INVALID ? nullptr : data + size_t(slot) * type->size;
	}
	_FORCE_INLINE_ const void *get(uint32_t p_entity_index) const {
		uint32_t slot = index.index_of(p_entity_index);
		return slot == ECSSparseSet<uint8_t>::INVALID ? nullptr : data + size_t(slot) * type->size;
	}
	_FORCE_INLINE_ uint32_t get_change_tick(uint32_t p_entity_index) const {
		uint32_t slot = index.index_of(p_entity_index);
		return slot == ECSSparseSet<uint8_t>::INVALID ? 0 : change_ticks[slot];
	}
	_FORCE_INLINE_ void mark_changed(uint32_t p_entity_index, uint32_t p_tick) {
		uint32_t slot = index.index_of(p_entity_index);
		if (slot != ECSSparseSet<uint8_t>::INVALID) {
			change_ticks[slot] = p_tick;
		}
	}

	uint64_t get_memory_usage() const {
		return index.get_memory_usage() + uint64_t(capacity) * (type->size + sizeof(uint32_t));
	}

	// Dense iteration.
	_FORCE_INLINE_ uint32_t entity_index_at(uint32_t p_dense) const { return index.key_at(p_dense); }
	_FORCE_INLINE_ void *value_at(uint32_t p_dense) { return data + size_t(p_dense) * type->size; }

	// Inserts (constructed from p_init when given, default otherwise). If the
	// component already exists it is copy-assigned from p_init (when given).
	void *insert(uint32_t p_entity_index, const void *p_init, uint32_t p_tick) {
		uint32_t slot = index.index_of(p_entity_index);
		if (slot != ECSSparseSet<uint8_t>::INVALID) {
			void *ptr = data + size_t(slot) * type->size;
			if (p_init) {
				type->copy_assign(ptr, p_init);
			}
			change_ticks[slot] = p_tick;
			return ptr;
		}
		if (index.size() >= capacity) {
			_grow(index.size() + 1);
		}
		slot = index.insert(p_entity_index, 0);
		void *ptr = data + size_t(slot) * type->size;
		if (p_init) {
			type->copy_construct(ptr, p_init);
		} else {
			type->construct_at(ptr);
		}
		change_ticks[slot] = p_tick;
		return ptr;
	}

	bool erase(uint32_t p_entity_index) {
		uint32_t slot = index.index_of(p_entity_index);
		if (slot == ECSSparseSet<uint8_t>::INVALID) {
			return false;
		}
		uint32_t last = index.size() - 1;
		if (type->size) {
			void *dst = data + size_t(slot) * type->size;
			type->destruct_at(dst);
			if (slot != last) {
				void *src = data + size_t(last) * type->size;
				if (type->trivial || type->move == nullptr) {
					memcpy(dst, src, type->size);
				} else {
					type->move_construct(dst, src);
					type->destruct_at(src);
				}
			}
		}
		if (slot != last) {
			change_ticks[slot] = change_ticks[last];
		}
		index.erase(p_entity_index); // Mirrors the swap-remove above.
		return true;
	}

	void clear() {
		if (data) {
			for (uint32_t i = 0; i < index.size(); i++) {
				type->destruct_at(data + size_t(i) * type->size);
			}
			memfree(data);
			data = nullptr;
		}
		if (change_ticks) {
			memfree(change_ticks);
			change_ticks = nullptr;
		}
		index.clear();
		capacity = 0;
	}

	explicit SparseComponentStorage(const ComponentType *p_type) :
			type(p_type) {}
	~SparseComponentStorage() { clear(); }

	SparseComponentStorage(const SparseComponentStorage &) = delete;
	SparseComponentStorage &operator=(const SparseComponentStorage &) = delete;
};

} // namespace ecs
