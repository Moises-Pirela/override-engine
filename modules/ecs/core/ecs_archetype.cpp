/**************************************************************************/
/*  ecs_archetype.cpp                                                     */
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

#include "ecs_archetype.h"

namespace ecs {

Archetype::Archetype(ArchetypeId p_id, const ComponentMask &p_mask, const LocalVector<const ComponentType *> &p_types) :
		id(p_id), mask(p_mask) {
	column_of.resize(MAX_COMPONENTS);
	for (uint32_t i = 0; i < MAX_COMPONENTS; i++) {
		column_of[i] = -1;
	}
	columns.resize(p_types.size());
	types.resize(p_types.size());
	for (uint32_t i = 0; i < p_types.size(); i++) {
		const ComponentType *t = p_types[i];
		DEV_ASSERT(!t->is_sparse());
		types[i] = t->id;
		columns[i].type = t;
		columns[i].stride = t->size;
		column_of[t->id] = int16_t(i);
	}
}

Archetype::~Archetype() {
	for (uint32_t r = 0; r < entities.size(); r++) {
		for (uint32_t c = 0; c < columns.size(); c++) {
			columns[c].type->destruct_at(get(c, r));
		}
	}
	for (uint32_t c = 0; c < columns.size(); c++) {
		if (columns[c].data) {
			memfree(columns[c].data);
		}
		if (columns[c].change_ticks) {
			memfree(columns[c].change_ticks);
		}
	}
}

void Archetype::_grow(uint32_t p_min_capacity) {
	uint32_t new_capacity = MAX(capacity * 2, MAX(p_min_capacity, 16u));
	for (uint32_t c = 0; c < columns.size(); c++) {
		Column &col = columns[c];
		uint32_t *new_ticks = (uint32_t *)memalloc(sizeof(uint32_t) * new_capacity);
		if (col.change_ticks) {
			memcpy(new_ticks, col.change_ticks, sizeof(uint32_t) * entities.size());
			memfree(col.change_ticks);
		}
		col.change_ticks = new_ticks;

		if (col.stride == 0) {
			continue; // Tag component, no data.
		}
		uint8_t *new_data = (uint8_t *)memalloc(size_t(col.stride) * new_capacity);
		if (col.data) {
			if (col.type->trivial || col.type->move == nullptr) {
				memcpy(new_data, col.data, size_t(col.stride) * entities.size());
			} else {
				for (uint32_t r = 0; r < entities.size(); r++) {
					void *src = col.data + size_t(r) * col.stride;
					col.type->move_construct(new_data + size_t(r) * col.stride, src);
					col.type->destruct_at(src);
				}
			}
			memfree(col.data);
		}
		col.data = new_data;
	}
	capacity = new_capacity;
}

uint64_t Archetype::get_memory_usage() const {
	uint64_t bytes = sizeof(Archetype) + uint64_t(column_of.size()) * sizeof(int16_t);
	for (const Column &c : columns) {
		bytes += uint64_t(capacity) * (c.stride + sizeof(uint32_t));
	}
	bytes += uint64_t(entities.size()) * sizeof(Entity);
	return bytes;
}

void Archetype::reserve(uint32_t p_capacity) {
	if (p_capacity > capacity) {
		_grow(p_capacity);
	}
	entities.reserve(p_capacity);
}

uint32_t Archetype::alloc_row(Entity p_entity, uint32_t p_tick) {
	uint32_t row = entities.size();
	if (row >= capacity) {
		_grow(row + 1);
	}
	entities.push_back(p_entity);
	for (uint32_t c = 0; c < columns.size(); c++) {
		columns[c].change_ticks[row] = p_tick;
	}
	return row;
}

uint32_t Archetype::add_row(Entity p_entity, uint32_t p_tick) {
	uint32_t row = alloc_row(p_entity, p_tick);
	for (uint32_t c = 0; c < columns.size(); c++) {
		columns[c].type->construct_at(get(c, row));
	}
	return row;
}

Entity Archetype::remove_row(uint32_t p_row) {
	uint32_t last = entities.size() - 1;
	DEV_ASSERT(p_row <= last);
	for (uint32_t c = 0; c < columns.size(); c++) {
		Column &col = columns[c];
		col.type->destruct_at(get(c, p_row));
		if (p_row != last && col.stride) {
			void *dst = get(c, p_row);
			void *src = get(c, last);
			if (col.type->trivial || col.type->move == nullptr) {
				memcpy(dst, src, col.stride);
			} else {
				col.type->move_construct(dst, src);
				col.type->destruct_at(src);
			}
		}
		if (p_row != last) {
			col.change_ticks[p_row] = col.change_ticks[last];
		}
	}
	Entity moved;
	if (p_row != last) {
		moved = entities[last];
		entities[p_row] = moved;
	}
	entities.resize(last);
	return moved;
}

} // namespace ecs
