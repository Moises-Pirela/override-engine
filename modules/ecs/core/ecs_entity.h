/**************************************************************************/
/*  ecs_entity.h                                                          */
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

#include "ecs_bitset.h"

#include "core/typedefs.h"

namespace ecs {

// Maximum number of component types a single World can register.
// Archetype signatures and query masks are bitsets of this width.
constexpr uint32_t MAX_COMPONENTS = 512;

typedef uint32_t ComponentId;
constexpr ComponentId INVALID_COMPONENT = 0xFFFFFFFF;

typedef ECSBitSet<MAX_COMPONENTS> ComponentMask;

typedef uint32_t ArchetypeId;
constexpr ArchetypeId INVALID_ARCHETYPE = 0xFFFFFFFF;

// An entity is a 64 bit handle: low 32 bits are the slot index in the world,
// high 32 bits are the generation of that slot. Generations start at 1, so the
// value 0 is never a valid entity and is used as "null".
//
// Entities are plain integers and can be passed around scripts as `int`.
struct Entity {
	uint64_t id = 0;

	static constexpr uint32_t INDEX_MASK = 0xFFFFFFFF;

	_FORCE_INLINE_ uint32_t index() const { return uint32_t(id & INDEX_MASK); }
	_FORCE_INLINE_ uint32_t generation() const { return uint32_t(id >> 32); }
	_FORCE_INLINE_ bool is_null() const { return id == 0; }
	_FORCE_INLINE_ bool is_valid() const { return id != 0; }

	static _FORCE_INLINE_ Entity make(uint32_t p_index, uint32_t p_generation) {
		Entity e;
		e.id = (uint64_t(p_generation) << 32) | uint64_t(p_index);
		return e;
	}

	_FORCE_INLINE_ bool operator==(const Entity &p_o) const { return id == p_o.id; }
	_FORCE_INLINE_ bool operator!=(const Entity &p_o) const { return id != p_o.id; }
	_FORCE_INLINE_ bool operator<(const Entity &p_o) const { return id < p_o.id; }

	_FORCE_INLINE_ Entity() {}
	_FORCE_INLINE_ explicit Entity(uint64_t p_id) :
			id(p_id) {}
};

static const Entity NULL_ENTITY;

} // namespace ecs
