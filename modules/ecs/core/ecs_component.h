/**************************************************************************/
/*  ecs_component.h                                                       */
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

#include "ecs_entity.h"

#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/variant.h"

#include <type_traits>

namespace ecs {

class World;

enum ComponentFlags : uint32_t {
	COMPONENT_FLAG_NONE = 0,
	// Stored in a per-type sparse set instead of archetype tables. Adding or
	// removing a sparse component never moves the entity between archetypes,
	// which makes it ideal for tags/state that is toggled very frequently.
	COMPONENT_FLAG_SPARSE = 1 << 0,
	// Replicated over the network from the server to every client.
	COMPONENT_FLAG_REPLICATED = 1 << 1,
	// Replicated by the peer that has authority over the entity (e.g. the
	// owning client) towards the server, which then relays it. Implies
	// COMPONENT_FLAG_REPLICATED.
	COMPONENT_FLAG_OWNER_AUTHORITY = 1 << 2,
	// Use unreliable (ordered) transfer for this component's updates.
	COMPONENT_FLAG_UNRELIABLE = 1 << 3,
	// Zero-sized marker component. Set automatically when size is 0.
	COMPONENT_FLAG_TAG = 1 << 4,
};

// Storage classes for script-visible fields.
enum FieldKind : uint8_t {
	FIELD_KIND_TRIVIAL, // memcpy-able value (int64, double, Vector3, Transform3D, ...).
	FIELD_KIND_ENTITY, // ecs::Entity, needs remapping when replicated.
	FIELD_KIND_STRING, // String.
	FIELD_KIND_STRING_NAME, // StringName.
	FIELD_KIND_VARIANT, // Any Variant (arrays, dictionaries, objects...).
};

// Pseudo Variant::Type used when registering fields from scripts to mark an
// entity reference. Stored as an ecs::Entity (64 bits).
constexpr int FIELD_TYPE_ENTITY = 1000;

struct ComponentField {
	StringName name;
	Variant::Type type = Variant::NIL;
	FieldKind kind = FIELD_KIND_TRIVIAL;
	uint32_t offset = 0;
	uint32_t size = 0;
};

typedef void (*ComponentHook)(World &p_world, Entity p_entity, void *p_component);

// Describes a component type registered in a World. Components are blobs of
// `size` bytes laid out in archetype columns (or sparse sets). Lifecycle
// callbacks are only needed for non trivially copyable types.
struct ComponentType {
	ComponentId id = INVALID_COMPONENT;
	StringName name;
	uint32_t size = 0;
	uint32_t align = 1;
	uint32_t flags = 0;
	bool trivial = true; // Can be memcpy'd / memset'd.
	bool script_defined = false; // Layout generated from a field list.

	// Lifecycle callbacks. They receive the type so that generic (script
	// defined) layouts can walk their field list.
	void (*construct)(const ComponentType &p_type, void *p_dst) = nullptr; // Default construct into uninitialized memory.
	void (*destruct)(const ComponentType &p_type, void *p_ptr) = nullptr;
	void (*move)(const ComponentType &p_type, void *p_dst, void *p_src) = nullptr; // Move-construct into uninitialized memory.
	void (*copy)(const ComponentType &p_type, void *p_dst, const void *p_src) = nullptr; // Copy-construct into uninitialized memory.
	void (*assign)(const ComponentType &p_type, void *p_dst, const void *p_src) = nullptr; // Copy-assign into constructed memory.

	ComponentHook on_add = nullptr;
	ComponentHook on_remove = nullptr;

	LocalVector<ComponentField> fields;
	HashMap<StringName, uint32_t> field_index;

	_FORCE_INLINE_ bool is_sparse() const { return flags & COMPONENT_FLAG_SPARSE; }
	_FORCE_INLINE_ bool is_tag() const { return size == 0; }
	_FORCE_INLINE_ bool is_replicated() const { return flags & (COMPONENT_FLAG_REPLICATED | COMPONENT_FLAG_OWNER_AUTHORITY); }
	_FORCE_INLINE_ bool is_owner_authority() const { return flags & COMPONENT_FLAG_OWNER_AUTHORITY; }

	int32_t find_field(const StringName &p_name) const {
		const uint32_t *idx = field_index.getptr(p_name);
		return idx ? int32_t(*idx) : -1;
	}

	// Generic lifecycle helpers that dispatch to the callbacks above or to
	// memcpy/memset for trivial types.
	_FORCE_INLINE_ void construct_at(void *p_dst) const {
		if (construct) {
			construct(*this, p_dst);
		} else if (size) {
			memset(p_dst, 0, size);
		}
	}
	_FORCE_INLINE_ void destruct_at(void *p_ptr) const {
		if (destruct) {
			destruct(*this, p_ptr);
		}
	}
	_FORCE_INLINE_ void move_construct(void *p_dst, void *p_src) const {
		if (move) {
			move(*this, p_dst, p_src);
		} else if (size) {
			memcpy(p_dst, p_src, size);
		}
	}
	_FORCE_INLINE_ void copy_construct(void *p_dst, const void *p_src) const {
		if (copy) {
			copy(*this, p_dst, p_src);
		} else if (size) {
			memcpy(p_dst, p_src, size);
		}
	}
	_FORCE_INLINE_ void copy_assign(void *p_dst, const void *p_src) const {
		if (assign) {
			assign(*this, p_dst, p_src);
		} else if (size) {
			memcpy(p_dst, p_src, size);
		}
	}

	// Field level reflection, used by script bindings and the replicator.
	Variant get_field(const void *p_component, uint32_t p_field) const;
	bool set_field(void *p_component, uint32_t p_field, const Variant &p_value) const;
	Dictionary to_dictionary(const void *p_component) const;
	void from_dictionary(void *p_component, const Dictionary &p_values) const;

	// Stable hash of the layout (name + field names/types), used to verify
	// that two peers agree on how a component is serialized.
	uint32_t layout_hash() const;

	// Builds a script-defined component type from a list of fields.
	// p_fields values are Variant::Type integers, or FIELD_TYPE_ENTITY.
	static bool build_script_layout(ComponentType &r_type, const Dictionary &p_fields, String *r_error = nullptr);

	// Adds a reflected field to a native component type. Offsets are relative
	// to the component start.
	void add_field(const StringName &p_name, Variant::Type p_type, uint32_t p_offset, uint32_t p_size, FieldKind p_kind = FIELD_KIND_TRIVIAL);

	// Returns storage size / alignment / kind for a script field type.
	static bool get_field_storage(int p_type, uint32_t &r_size, uint32_t &r_align, FieldKind &r_kind);
};

// Helpers to register native C++ types. Lifecycle functions are only
// generated for types that need them.
template <typename T>
struct ComponentTraits {
	static void construct(const ComponentType &, void *p_dst) { memnew_placement(p_dst, T); }
	static void destruct(const ComponentType &, void *p_ptr) { static_cast<T *>(p_ptr)->~T(); }
	static void move(const ComponentType &, void *p_dst, void *p_src) { memnew_placement(p_dst, T(std::move(*static_cast<T *>(p_src)))); }
	static void copy(const ComponentType &, void *p_dst, const void *p_src) { memnew_placement(p_dst, T(*static_cast<const T *>(p_src))); }
	static void assign(const ComponentType &, void *p_dst, const void *p_src) { *static_cast<T *>(p_dst) = *static_cast<const T *>(p_src); }

	static void fill(ComponentType &r_type) {
		if constexpr (std::is_empty_v<T>) {
			// Empty structs are tags: no storage at all.
			r_type.size = 0;
			r_type.align = 1;
			r_type.trivial = true;
			r_type.flags |= COMPONENT_FLAG_TAG;
			return;
		}
		r_type.size = uint32_t(sizeof(T));
		r_type.align = uint32_t(alignof(T));
		r_type.trivial = std::is_trivially_copyable_v<T> && std::is_trivially_default_constructible_v<T>;
		if (!std::is_trivially_default_constructible_v<T> || !std::is_trivially_copyable_v<T>) {
			r_type.construct = construct;
		}
		if (!std::is_trivially_destructible_v<T>) {
			r_type.destruct = destruct;
		}
		if (!std::is_trivially_copyable_v<T>) {
			r_type.move = move;
			r_type.copy = copy;
			r_type.assign = assign;
		}
	}
};

// Zero sized tag components.
template <>
struct ComponentTraits<void> {
	static void fill(ComponentType &r_type) {
		r_type.size = 0;
		r_type.align = 1;
		r_type.trivial = true;
		r_type.flags |= COMPONENT_FLAG_TAG;
	}
};

// Per native type index, used by World to map C++ types to component ids in
// O(1) without hashing.
struct TypeIndexCounter {
	static uint32_t next();
};

template <typename T>
struct TypeIndex {
	static uint32_t get() {
		static uint32_t idx = TypeIndexCounter::next();
		return idx;
	}
};

} // namespace ecs
