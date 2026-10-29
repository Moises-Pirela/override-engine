/**************************************************************************/
/*  ecs_component.cpp                                                     */
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

#include "ecs_component.h"

#include "core/templates/hashfuncs.h"
#include "core/templates/safe_refcount.h"

namespace ecs {

uint32_t TypeIndexCounter::next() {
	static SafeNumeric<uint32_t> counter;
	return counter.postincrement();
}

bool ComponentType::get_field_storage(int p_type, uint32_t &r_size, uint32_t &r_align, FieldKind &r_kind) {
	r_kind = FIELD_KIND_TRIVIAL;
	if (p_type == FIELD_TYPE_ENTITY) {
		r_size = sizeof(Entity);
		r_align = alignof(Entity);
		r_kind = FIELD_KIND_ENTITY;
		return true;
	}
#define STORE(m_type)          \
	r_size = sizeof(m_type);   \
	r_align = alignof(m_type); \
	return true;

	switch (Variant::Type(p_type)) {
		case Variant::BOOL:
			STORE(bool);
		case Variant::INT:
			STORE(int64_t);
		case Variant::FLOAT:
			STORE(double);
		case Variant::VECTOR2:
			STORE(Vector2);
		case Variant::VECTOR2I:
			STORE(Vector2i);
		case Variant::RECT2:
			STORE(Rect2);
		case Variant::RECT2I:
			STORE(Rect2i);
		case Variant::VECTOR3:
			STORE(Vector3);
		case Variant::VECTOR3I:
			STORE(Vector3i);
		case Variant::TRANSFORM2D:
			STORE(Transform2D);
		case Variant::VECTOR4:
			STORE(Vector4);
		case Variant::VECTOR4I:
			STORE(Vector4i);
		case Variant::PLANE:
			STORE(Plane);
		case Variant::QUATERNION:
			STORE(Quaternion);
		case Variant::AABB:
			STORE(AABB);
		case Variant::BASIS:
			STORE(Basis);
		case Variant::TRANSFORM3D:
			STORE(Transform3D);
		case Variant::PROJECTION:
			STORE(Projection);
		case Variant::COLOR:
			STORE(Color);
		case Variant::RID:
			STORE(RID);
		case Variant::STRING:
			r_kind = FIELD_KIND_STRING;
			STORE(String);
		case Variant::STRING_NAME:
			r_kind = FIELD_KIND_STRING_NAME;
			STORE(StringName);
		case Variant::NIL:
		case Variant::OBJECT:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
		case Variant::DICTIONARY:
		case Variant::ARRAY:
		case Variant::NODE_PATH:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
			r_kind = FIELD_KIND_VARIANT;
			STORE(Variant);
		default:
			return false;
	}
#undef STORE
}

void ComponentType::add_field(const StringName &p_name, Variant::Type p_type, uint32_t p_offset, uint32_t p_size, FieldKind p_kind) {
	ComponentField f;
	f.name = p_name;
	f.type = p_type;
	f.kind = p_kind;
	f.offset = p_offset;
	f.size = p_size;
	field_index.insert(p_name, fields.size());
	fields.push_back(f);
}

static void _script_construct(const ComponentType &p_type, void *p_dst) {
	memset(p_dst, 0, p_type.size);
	for (const ComponentField &f : p_type.fields) {
		uint8_t *ptr = (uint8_t *)p_dst + f.offset;
		switch (f.kind) {
			case FIELD_KIND_STRING:
				memnew_placement(ptr, String);
				break;
			case FIELD_KIND_STRING_NAME:
				memnew_placement(ptr, StringName);
				break;
			case FIELD_KIND_VARIANT:
				memnew_placement(ptr, Variant);
				break;
			default:
				break;
		}
	}
}

static void _script_destruct(const ComponentType &p_type, void *p_ptr) {
	for (const ComponentField &f : p_type.fields) {
		uint8_t *ptr = (uint8_t *)p_ptr + f.offset;
		switch (f.kind) {
			case FIELD_KIND_STRING:
				((String *)ptr)->~String();
				break;
			case FIELD_KIND_STRING_NAME:
				((StringName *)ptr)->~StringName();
				break;
			case FIELD_KIND_VARIANT:
				((Variant *)ptr)->~Variant();
				break;
			default:
				break;
		}
	}
}

static void _script_copy(const ComponentType &p_type, void *p_dst, const void *p_src, bool p_construct) {
	for (const ComponentField &f : p_type.fields) {
		uint8_t *dst = (uint8_t *)p_dst + f.offset;
		const uint8_t *src = (const uint8_t *)p_src + f.offset;
		switch (f.kind) {
			case FIELD_KIND_STRING:
				if (p_construct) {
					memnew_placement(dst, String(*(const String *)src));
				} else {
					*(String *)dst = *(const String *)src;
				}
				break;
			case FIELD_KIND_STRING_NAME:
				if (p_construct) {
					memnew_placement(dst, StringName(*(const StringName *)src));
				} else {
					*(StringName *)dst = *(const StringName *)src;
				}
				break;
			case FIELD_KIND_VARIANT:
				if (p_construct) {
					memnew_placement(dst, Variant(*(const Variant *)src));
				} else {
					*(Variant *)dst = *(const Variant *)src;
				}
				break;
			default:
				memcpy(dst, src, f.size);
				break;
		}
	}
}

static void _sc_construct(const ComponentType &p_type, void *p_dst) {
	_script_construct(p_type, p_dst);
}
static void _sc_destruct(const ComponentType &p_type, void *p_ptr) {
	_script_destruct(p_type, p_ptr);
}
static void _sc_move(const ComponentType &p_type, void *p_dst, void *p_src) {
	_script_copy(p_type, p_dst, p_src, true);
	_script_destruct(p_type, p_src);
}
static void _sc_copy(const ComponentType &p_type, void *p_dst, const void *p_src) {
	_script_copy(p_type, p_dst, p_src, true);
}
static void _sc_assign(const ComponentType &p_type, void *p_dst, const void *p_src) {
	_script_copy(p_type, p_dst, p_src, false);
}

bool ComponentType::build_script_layout(ComponentType &r_type, const Dictionary &p_fields, String *r_error) {
	r_type.fields.clear();
	r_type.field_index.clear();
	r_type.script_defined = true;

	uint32_t offset = 0;
	uint32_t max_align = 1;
	bool trivial = true;

	for (const KeyValue<Variant, Variant> &kv : p_fields) {
		StringName fname = kv.key;
		if (fname == StringName()) {
			if (r_error) {
				*r_error = "Field names must be non-empty strings.";
			}
			return false;
		}
		if (r_type.field_index.has(fname)) {
			if (r_error) {
				*r_error = vformat("Duplicate field name '%s'.", fname);
			}
			return false;
		}
		int ftype;
		if (kv.value.get_type() == Variant::INT) {
			ftype = kv.value;
		} else if (kv.value.get_type() == Variant::STRING || kv.value.get_type() == Variant::STRING_NAME) {
			String s = kv.value;
			if (s.to_lower() == "entity") {
				ftype = FIELD_TYPE_ENTITY;
			} else {
				ftype = -1;
				for (int i = 0; i < Variant::VARIANT_MAX; i++) {
					if (Variant::get_type_name(Variant::Type(i)).to_lower() == s.to_lower()) {
						ftype = i;
						break;
					}
				}
				if (ftype == -1) {
					if (r_error) {
						*r_error = vformat("Unknown field type name '%s' for field '%s'.", s, fname);
					}
					return false;
				}
			}
		} else {
			if (r_error) {
				*r_error = vformat("Field '%s' must map to a Variant.Type integer, a type name, or \"entity\".", fname);
			}
			return false;
		}

		uint32_t fsize, falign;
		FieldKind kind;
		if (!get_field_storage(ftype, fsize, falign, kind)) {
			if (r_error) {
				*r_error = vformat("Unsupported field type for field '%s'.", fname);
			}
			return false;
		}
		if (kind != FIELD_KIND_TRIVIAL && kind != FIELD_KIND_ENTITY) {
			trivial = false;
		}
		offset = (offset + falign - 1) & ~(falign - 1);
		max_align = MAX(max_align, falign);
		r_type.add_field(fname, ftype == FIELD_TYPE_ENTITY ? Variant::INT : Variant::Type(ftype), offset, fsize, kind);
		offset += fsize;
	}

	r_type.size = (offset + max_align - 1) & ~(max_align - 1);
	r_type.align = max_align;
	r_type.trivial = trivial;
	r_type.construct = trivial ? nullptr : _sc_construct;
	r_type.destruct = trivial ? nullptr : _sc_destruct;
	r_type.move = trivial ? nullptr : _sc_move;
	r_type.copy = trivial ? nullptr : _sc_copy;
	r_type.assign = trivial ? nullptr : _sc_assign;
	return true;
}

Variant ComponentType::get_field(const void *p_component, uint32_t p_field) const {
	ERR_FAIL_UNSIGNED_INDEX_V(p_field, fields.size(), Variant());
	const ComponentField &f = fields[p_field];
	const uint8_t *ptr = (const uint8_t *)p_component + f.offset;
	switch (f.kind) {
		case FIELD_KIND_ENTITY:
			return int64_t(((const Entity *)ptr)->id);
		case FIELD_KIND_STRING:
			return *(const String *)ptr;
		case FIELD_KIND_STRING_NAME:
			return *(const StringName *)ptr;
		case FIELD_KIND_VARIANT:
			return *(const Variant *)ptr;
		case FIELD_KIND_TRIVIAL:
			break;
	}
#define LOAD(m_vt, m_type) \
	case Variant::m_vt:    \
		return *(const m_type *)ptr;
	switch (f.type) {
		LOAD(BOOL, bool)
		LOAD(INT, int64_t)
		LOAD(FLOAT, double)
		LOAD(VECTOR2, Vector2)
		LOAD(VECTOR2I, Vector2i)
		LOAD(RECT2, Rect2)
		LOAD(RECT2I, Rect2i)
		LOAD(VECTOR3, Vector3)
		LOAD(VECTOR3I, Vector3i)
		LOAD(TRANSFORM2D, Transform2D)
		LOAD(VECTOR4, Vector4)
		LOAD(VECTOR4I, Vector4i)
		LOAD(PLANE, Plane)
		LOAD(QUATERNION, Quaternion)
		LOAD(AABB, AABB)
		LOAD(BASIS, Basis)
		LOAD(TRANSFORM3D, Transform3D)
		LOAD(PROJECTION, Projection)
		LOAD(COLOR, Color)
		LOAD(RID, RID)
		default:
			break;
	}
#undef LOAD
	// Native fields may use narrower storage than the script type.
	if (f.type == Variant::INT) {
		switch (f.size) {
			case 1:
				return int64_t(*(const int8_t *)ptr);
			case 2:
				return int64_t(*(const int16_t *)ptr);
			case 4:
				return int64_t(*(const int32_t *)ptr);
			default:
				break;
		}
	} else if (f.type == Variant::FLOAT && f.size == 4) {
		return double(*(const float *)ptr);
	}
	return Variant();
}

bool ComponentType::set_field(void *p_component, uint32_t p_field, const Variant &p_value) const {
	ERR_FAIL_UNSIGNED_INDEX_V(p_field, fields.size(), false);
	const ComponentField &f = fields[p_field];
	uint8_t *ptr = (uint8_t *)p_component + f.offset;
	switch (f.kind) {
		case FIELD_KIND_ENTITY:
			((Entity *)ptr)->id = uint64_t(int64_t(p_value));
			return true;
		case FIELD_KIND_STRING:
			*(String *)ptr = p_value;
			return true;
		case FIELD_KIND_STRING_NAME:
			*(StringName *)ptr = p_value;
			return true;
		case FIELD_KIND_VARIANT:
			if (f.type != Variant::NIL && p_value.get_type() != f.type && p_value.get_type() != Variant::NIL) {
				ERR_FAIL_V_MSG(false, vformat("Field '%s' expects a value of type %s.", f.name, Variant::get_type_name(f.type)));
			}
			*(Variant *)ptr = p_value;
			return true;
		case FIELD_KIND_TRIVIAL:
			break;
	}
#define STORE(m_vt, m_type)               \
	case Variant::m_vt:                   \
		*(m_type *)ptr = (m_type)p_value; \
		return true;
	switch (f.type) {
		STORE(BOOL, bool)
		STORE(VECTOR2, Vector2)
		STORE(VECTOR2I, Vector2i)
		STORE(RECT2, Rect2)
		STORE(RECT2I, Rect2i)
		STORE(VECTOR3, Vector3)
		STORE(VECTOR3I, Vector3i)
		STORE(TRANSFORM2D, Transform2D)
		STORE(VECTOR4, Vector4)
		STORE(VECTOR4I, Vector4i)
		STORE(PLANE, Plane)
		STORE(QUATERNION, Quaternion)
		STORE(AABB, AABB)
		STORE(BASIS, Basis)
		STORE(TRANSFORM3D, Transform3D)
		STORE(PROJECTION, Projection)
		STORE(COLOR, Color)
		STORE(RID, RID)
		case Variant::INT: {
			int64_t v = p_value;
			switch (f.size) {
				case 1:
					*(int8_t *)ptr = int8_t(v);
					break;
				case 2:
					*(int16_t *)ptr = int16_t(v);
					break;
				case 4:
					*(int32_t *)ptr = int32_t(v);
					break;
				default:
					*(int64_t *)ptr = v;
					break;
			}
			return true;
		}
		case Variant::FLOAT: {
			double v = p_value;
			if (f.size == 4) {
				*(float *)ptr = float(v);
			} else {
				*(double *)ptr = v;
			}
			return true;
		}
		default:
			break;
	}
#undef STORE
	return false;
}

Dictionary ComponentType::to_dictionary(const void *p_component) const {
	Dictionary d;
	for (uint32_t i = 0; i < fields.size(); i++) {
		d[fields[i].name] = get_field(p_component, i);
	}
	return d;
}

void ComponentType::from_dictionary(void *p_component, const Dictionary &p_values) const {
	for (const KeyValue<Variant, Variant> &kv : p_values) {
		int32_t idx = find_field(kv.key);
		if (idx < 0) {
			WARN_PRINT(vformat("Component '%s' has no field named '%s'.", name, String(kv.key)));
			continue;
		}
		set_field(p_component, uint32_t(idx), kv.value);
	}
}

uint32_t ComponentType::layout_hash() const {
	CharString n = String(name).utf8();
	uint32_t h = hash_djb2_buffer((const uint8_t *)n.get_data(), n.length());
	for (const ComponentField &f : fields) {
		CharString fn = String(f.name).utf8();
		h = hash_djb2_buffer((const uint8_t *)fn.get_data(), fn.length(), h);
		h = hash_djb2_one_32(uint32_t(f.type), h);
		h = hash_djb2_one_32(uint32_t(f.kind), h);
		h = hash_djb2_one_32(f.size, h);
	}
	return h;
}

} // namespace ecs
