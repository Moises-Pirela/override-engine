/**************************************************************************/
/*  ecs_prefab.cpp                                                        */
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

#include "ecs_prefab.h"

#include "../core/ecs_world.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"

/* ECSComponentSchema */

struct SupportedType {
	const char *name;
	int type;
};

static const SupportedType supported_types[] = {
	{ "bool", Variant::BOOL },
	{ "int", Variant::INT },
	{ "float", Variant::FLOAT },
	{ "String", Variant::STRING },
	{ "StringName", Variant::STRING_NAME },
	{ "Vector2", Variant::VECTOR2 },
	{ "Vector2i", Variant::VECTOR2I },
	{ "Rect2", Variant::RECT2 },
	{ "Rect2i", Variant::RECT2I },
	{ "Vector3", Variant::VECTOR3 },
	{ "Vector3i", Variant::VECTOR3I },
	{ "Transform2D", Variant::TRANSFORM2D },
	{ "Vector4", Variant::VECTOR4 },
	{ "Vector4i", Variant::VECTOR4I },
	{ "Plane", Variant::PLANE },
	{ "Quaternion", Variant::QUATERNION },
	{ "AABB", Variant::AABB },
	{ "Basis", Variant::BASIS },
	{ "Transform3D", Variant::TRANSFORM3D },
	{ "Projection", Variant::PROJECTION },
	{ "Color", Variant::COLOR },
	{ "RID", Variant::RID },
	{ "Array", Variant::ARRAY },
	{ "Dictionary", Variant::DICTIONARY },
	{ "Variant", Variant::NIL },
	{ "Entity", ecs::FIELD_TYPE_ENTITY },
};

void ECSComponentSchema::set_component_name(const StringName &p_name) {
	if (component_name == p_name) {
		return;
	}
	component_name = p_name;
	emit_changed();
}

StringName ECSComponentSchema::get_component_name() const {
	return component_name;
}

void ECSComponentSchema::set_fields(const Dictionary &p_fields) {
	fields = p_fields;
	emit_changed();
}

Dictionary ECSComponentSchema::get_fields() const {
	return fields;
}

void ECSComponentSchema::add_field(const StringName &p_name, int p_type) {
	ERR_FAIL_COND_MSG(p_name == StringName(), "Field name can't be empty.");
	fields[p_name] = p_type;
	emit_changed();
}

void ECSComponentSchema::remove_field(const StringName &p_name) {
	if (fields.erase(p_name)) {
		emit_changed();
	}
}

bool ECSComponentSchema::has_field(const StringName &p_name) const {
	return fields.has(p_name);
}

int ECSComponentSchema::get_field_type(const StringName &p_name) const {
	if (!fields.has(p_name)) {
		return -1;
	}
	return fields[p_name];
}

PackedStringArray ECSComponentSchema::get_field_names() const {
	PackedStringArray r;
	for (const KeyValue<Variant, Variant> &kv : fields) {
		r.push_back(kv.key);
	}
	return r;
}

void ECSComponentSchema::set_flags(int p_flags) {
	flags = p_flags;
	emit_changed();
}

int ECSComponentSchema::get_flags() const {
	return flags;
}

PackedStringArray ECSComponentSchema::get_supported_type_names() {
	PackedStringArray r;
	for (const SupportedType &t : supported_types) {
		r.push_back(t.name);
	}
	return r;
}

int ECSComponentSchema::type_from_name(const String &p_name) {
	String lower = p_name.to_lower();
	for (const SupportedType &t : supported_types) {
		if (String(t.name).to_lower() == lower) {
			return t.type;
		}
	}
	return -1;
}

String ECSComponentSchema::name_from_type(int p_type) {
	for (const SupportedType &t : supported_types) {
		if (t.type == p_type) {
			return t.name;
		}
	}
	return String();
}

void ECSComponentSchema::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_component_name", "name"), &ECSComponentSchema::set_component_name);
	ClassDB::bind_method(D_METHOD("get_component_name"), &ECSComponentSchema::get_component_name);
	ClassDB::bind_method(D_METHOD("set_fields", "fields"), &ECSComponentSchema::set_fields);
	ClassDB::bind_method(D_METHOD("get_fields"), &ECSComponentSchema::get_fields);
	ClassDB::bind_method(D_METHOD("add_field", "name", "type"), &ECSComponentSchema::add_field);
	ClassDB::bind_method(D_METHOD("remove_field", "name"), &ECSComponentSchema::remove_field);
	ClassDB::bind_method(D_METHOD("has_field", "name"), &ECSComponentSchema::has_field);
	ClassDB::bind_method(D_METHOD("get_field_type", "name"), &ECSComponentSchema::get_field_type);
	ClassDB::bind_method(D_METHOD("get_field_names"), &ECSComponentSchema::get_field_names);
	ClassDB::bind_method(D_METHOD("set_flags", "flags"), &ECSComponentSchema::set_flags);
	ClassDB::bind_method(D_METHOD("get_flags"), &ECSComponentSchema::get_flags);
	ClassDB::bind_static_method("ECSComponentSchema", D_METHOD("get_supported_type_names"), &ECSComponentSchema::get_supported_type_names);
	ClassDB::bind_static_method("ECSComponentSchema", D_METHOD("type_from_name", "name"), &ECSComponentSchema::type_from_name);
	ClassDB::bind_static_method("ECSComponentSchema", D_METHOD("name_from_type", "type"), &ECSComponentSchema::name_from_type);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "component_name"), "set_component_name", "get_component_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flags", PROPERTY_HINT_FLAGS, "Sparse:1,Replicated:2,Owner Authority:4,Unreliable:8"), "set_flags", "get_flags");
	// Edited through the dedicated inspector control; stored as a plain dictionary.
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "fields", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_fields", "get_fields");
}

/* ECSPrefab */

static const String COMPONENTS_PREFIX = "components/";

void ECSPrefab::set_components(const Dictionary &p_components) {
	components = p_components;
	notify_property_list_changed();
	emit_changed();
}

Dictionary ECSPrefab::get_components() const {
	return components;
}

void ECSPrefab::add_component(const StringName &p_name, const Dictionary &p_values) {
	ERR_FAIL_COND_MSG(p_name == StringName(), "Component name can't be empty.");
	if (components.has(p_name)) {
		Dictionary existing = components[p_name];
		existing.merge(p_values, true);
		components[p_name] = existing;
	} else {
		components[p_name] = p_values.duplicate();
	}
	notify_property_list_changed();
	emit_changed();
}

void ECSPrefab::remove_component(const StringName &p_name) {
	if (components.erase(p_name)) {
		notify_property_list_changed();
		emit_changed();
	}
}

bool ECSPrefab::has_component(const StringName &p_name) const {
	return components.has(p_name);
}

PackedStringArray ECSPrefab::get_component_names() const {
	PackedStringArray r;
	for (const KeyValue<Variant, Variant> &kv : components) {
		r.push_back(kv.key);
	}
	return r;
}

Dictionary ECSPrefab::get_component(const StringName &p_name) const {
	if (!components.has(p_name)) {
		return Dictionary();
	}
	return components[p_name];
}

void ECSPrefab::set_component_value(const StringName &p_component, const StringName &p_field, const Variant &p_value) {
	ERR_FAIL_COND_MSG(!components.has(p_component), vformat("Prefab has no component '%s'.", p_component));
	Dictionary values = components[p_component];
	values[p_field] = p_value;
	components[p_component] = values;
	emit_changed();
}

Variant ECSPrefab::get_component_value(const StringName &p_component, const StringName &p_field) const {
	if (!components.has(p_component)) {
		return Variant();
	}
	Dictionary values = components[p_component];
	return values.get(p_field, Variant());
}

void ECSPrefab::set_children(const TypedArray<ECSPrefab> &p_children) {
	children = p_children;
	emit_changed();
}

TypedArray<ECSPrefab> ECSPrefab::get_children() const {
	return children;
}

void ECSPrefab::add_child(const Ref<ECSPrefab> &p_child) {
	ERR_FAIL_COND(p_child.is_null());
	ERR_FAIL_COND_MSG(p_child.ptr() == this, "A prefab can't be its own child.");
	children.push_back(p_child);
	emit_changed();
}

int ECSPrefab::get_child_count() const {
	return children.size();
}

Ref<ECSPrefab> ECSPrefab::get_child(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, children.size(), Ref<ECSPrefab>());
	return children[p_index];
}

Ref<ECSPrefab> ECSPrefab::find_sub_prefab(const String &p_index_path) const {
	Ref<ECSPrefab> current = Ref<ECSPrefab>(const_cast<ECSPrefab *>(this));
	if (p_index_path.is_empty()) {
		return current;
	}
	Vector<String> parts = p_index_path.split("/", false);
	for (const String &part : parts) {
		if (!part.is_valid_int()) {
			return Ref<ECSPrefab>();
		}
		int idx = part.to_int();
		if (current.is_null() || idx < 0 || idx >= current->get_child_count()) {
			return Ref<ECSPrefab>();
		}
		current = current->get_child(idx);
	}
	return current;
}

String ECSPrefab::make_sub_path(const String &p_path, const String &p_index_path) {
	if (p_index_path.is_empty()) {
		return p_path;
	}
	return p_path + "#" + p_index_path;
}

Ref<ECSPrefab> ECSPrefab::load_from_path(const String &p_path) {
	String res_path = p_path;
	String index_path;
	int hash = p_path.rfind_char('#');
	if (hash >= 0) {
		res_path = p_path.substr(0, hash);
		index_path = p_path.substr(hash + 1);
	}
	Ref<ECSPrefab> root = ResourceLoader::load(res_path, "ECSPrefab");
	if (root.is_null()) {
		return root;
	}
	return root->find_sub_prefab(index_path);
}

bool ECSPrefab::_set(const StringName &p_name, const Variant &p_value) {
	String prop = p_name;
	if (!prop.begins_with(COMPONENTS_PREFIX)) {
		return false;
	}
	String rest = prop.substr(COMPONENTS_PREFIX.length());
	int slash = rest.find_char('/');
	if (slash < 0) {
		return false;
	}
	StringName comp = rest.substr(0, slash);
	StringName field = rest.substr(slash + 1);
	if (!components.has(comp)) {
		return false;
	}
	Dictionary values = components[comp];
	values[field] = p_value;
	components[comp] = values;
	emit_changed();
	return true;
}

bool ECSPrefab::_get(const StringName &p_name, Variant &r_ret) const {
	String prop = p_name;
	if (!prop.begins_with(COMPONENTS_PREFIX)) {
		return false;
	}
	String rest = prop.substr(COMPONENTS_PREFIX.length());
	int slash = rest.find_char('/');
	if (slash < 0) {
		return false;
	}
	StringName comp = rest.substr(0, slash);
	StringName field = rest.substr(slash + 1);
	if (!components.has(comp)) {
		return false;
	}
	Dictionary values = components[comp];
	if (values.has(field)) {
		r_ret = values[field];
	} else {
		// Not set yet: show the default for the field type.
		Variant::Type t = ECSComponentLibrary::get_field_type(comp, field);
		Callable::CallError ce;
		if (t == Variant::NIL) {
			r_ret = Variant();
		} else {
			Variant::construct(t, r_ret, nullptr, 0, ce);
		}
	}
	return true;
}

void ECSPrefab::_get_property_list(List<PropertyInfo> *p_list) const {
	for (const KeyValue<Variant, Variant> &kv : components) {
		StringName comp = kv.key;
		Dictionary values = kv.value;
		String prefix = COMPONENTS_PREFIX + String(comp) + "/";
		p_list->push_back(PropertyInfo(Variant::NIL, comp, PROPERTY_HINT_NONE, prefix, PROPERTY_USAGE_GROUP));

		const ECSComponentLibrary::Info *info = ECSComponentLibrary::get_info(comp);
		HashSet<StringName> listed;
		if (info) {
			for (const ECSComponentLibrary::FieldInfo &f : info->fields) {
				if (f.internal) {
					continue;
				}
				listed.insert(f.name);
				PropertyInfo pi(f.type, prefix + String(f.name));
				if (f.type == Variant::NIL) {
					pi.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_NIL_IS_VARIANT;
				} else {
					pi.usage = PROPERTY_USAGE_EDITOR;
				}
				p_list->push_back(pi);
			}
		}
		// Values that are not described by any known type: infer from the value.
		for (const KeyValue<Variant, Variant> &fv : values) {
			StringName field = fv.key;
			if (listed.has(field)) {
				continue;
			}
			PropertyInfo pi(fv.value.get_type(), prefix + String(field));
			pi.usage = PROPERTY_USAGE_EDITOR;
			if (fv.value.get_type() == Variant::NIL) {
				pi.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
			}
			p_list->push_back(pi);
		}
	}
}

bool ECSPrefab::_property_can_revert(const StringName &p_name) const {
	String prop = p_name;
	if (!prop.begins_with(COMPONENTS_PREFIX)) {
		return false;
	}
	String rest = prop.substr(COMPONENTS_PREFIX.length());
	int slash = rest.find_char('/');
	if (slash < 0 || !components.has(StringName(rest.substr(0, slash)))) {
		return false;
	}
	Dictionary values = components[StringName(rest.substr(0, slash))];
	return values.has(StringName(rest.substr(slash + 1)));
}

bool ECSPrefab::_property_get_revert(const StringName &p_name, Variant &r_property) const {
	String prop = p_name;
	if (!prop.begins_with(COMPONENTS_PREFIX)) {
		return false;
	}
	String rest = prop.substr(COMPONENTS_PREFIX.length());
	int slash = rest.find_char('/');
	if (slash < 0) {
		return false;
	}
	Variant::Type t = ECSComponentLibrary::get_field_type(rest.substr(0, slash), rest.substr(slash + 1));
	if (t == Variant::NIL) {
		r_property = Variant();
	} else {
		Callable::CallError ce;
		Variant::construct(t, r_property, nullptr, 0, ce);
	}
	return true;
}

void ECSPrefab::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_components", "components"), &ECSPrefab::set_components);
	ClassDB::bind_method(D_METHOD("get_components"), &ECSPrefab::get_components);
	ClassDB::bind_method(D_METHOD("add_component", "name", "values"), &ECSPrefab::add_component, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("remove_component", "name"), &ECSPrefab::remove_component);
	ClassDB::bind_method(D_METHOD("has_component", "name"), &ECSPrefab::has_component);
	ClassDB::bind_method(D_METHOD("get_component_names"), &ECSPrefab::get_component_names);
	ClassDB::bind_method(D_METHOD("get_component", "name"), &ECSPrefab::get_component);
	ClassDB::bind_method(D_METHOD("set_component_value", "component", "field", "value"), &ECSPrefab::set_component_value);
	ClassDB::bind_method(D_METHOD("get_component_value", "component", "field"), &ECSPrefab::get_component_value);
	ClassDB::bind_method(D_METHOD("set_children", "children"), &ECSPrefab::set_children);
	ClassDB::bind_method(D_METHOD("get_children"), &ECSPrefab::get_children);
	ClassDB::bind_method(D_METHOD("add_child", "child"), &ECSPrefab::add_child);
	ClassDB::bind_method(D_METHOD("get_child_count"), &ECSPrefab::get_child_count);
	ClassDB::bind_method(D_METHOD("get_child", "index"), &ECSPrefab::get_child);
	ClassDB::bind_method(D_METHOD("find_sub_prefab", "index_path"), &ECSPrefab::find_sub_prefab);
	ClassDB::bind_static_method("ECSPrefab", D_METHOD("load_from_path", "path"), &ECSPrefab::load_from_path);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "components", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_components", "get_components");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "children", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("ECSPrefab")), "set_children", "get_children");
}

/* ECSComponentLibrary */

const char *ECSComponentLibrary::SETTING_SCHEMAS = "ecs/components/schemas";

struct LibraryData {
	bool builtins_loaded = false;
	HashMap<StringName, ECSComponentLibrary::Info> infos;
	LocalVector<StringName> order;
};

static LibraryData *library_data = nullptr;

static LibraryData *_library() {
	if (!library_data) {
		library_data = memnew(LibraryData);
	}
	return library_data;
}

void ECSComponentLibrary::cleanup() {
	if (library_data) {
		memdelete(library_data);
		library_data = nullptr;
	}
}

void ECSComponentLibrary::_ensure_builtins() {
	LibraryData *d = _library();
	if (d->builtins_loaded) {
		return;
	}
	d->builtins_loaded = true;
	// A scratch world only registers the built-in component types.
	ecs::World *w = memnew(ecs::World);
	for (uint32_t i = 0; i < w->get_component_type_count(); i++) {
		const ecs::ComponentType *t = w->get_component_type(i);
		Info info;
		info.name = t->name;
		info.flags = t->flags;
		info.builtin = true;
		for (const ecs::ComponentField &f : t->fields) {
			FieldInfo fi;
			fi.name = f.name;
			fi.type = f.type;
			fi.entity = f.kind == ecs::FIELD_KIND_ENTITY;
			fi.internal = f.kind == ecs::FIELD_KIND_TRIVIAL && f.type == Variant::RID;
			info.fields.push_back(fi);
		}
		d->infos.insert(info.name, info);
		d->order.push_back(info.name);
	}
	memdelete(w);
}

void ECSComponentLibrary::add_schema(const Ref<ECSComponentSchema> &p_schema) {
	ERR_FAIL_COND(p_schema.is_null());
	_ensure_builtins();
	LibraryData *d = _library();
	StringName name = p_schema->get_component_name();
	if (name == StringName()) {
		name = p_schema->get_path().get_file().get_basename();
	}
	ERR_FAIL_COND_MSG(name == StringName(), "Schema has no component name.");
	Info info;
	info.name = name;
	info.flags = uint32_t(p_schema->get_flags());
	info.schema = p_schema;
	Dictionary fields = p_schema->get_fields();
	for (const KeyValue<Variant, Variant> &kv : fields) {
		FieldInfo fi;
		fi.name = kv.key;
		int t = kv.value.get_type() == Variant::INT ? int(kv.value) : ECSComponentSchema::type_from_name(kv.value);
		if (t == ecs::FIELD_TYPE_ENTITY) {
			fi.type = Variant::INT;
			fi.entity = true;
		} else if (t >= 0 && t < Variant::VARIANT_MAX) {
			fi.type = Variant::Type(t);
		} else {
			continue;
		}
		info.fields.push_back(fi);
	}
	if (!d->infos.has(name)) {
		d->order.push_back(name);
	}
	d->infos.insert(name, info);
}

void ECSComponentLibrary::remove_schema(const StringName &p_name) {
	LibraryData *d = _library();
	const Info *info = d->infos.getptr(p_name);
	if (!info || info->builtin) {
		return;
	}
	d->infos.erase(p_name);
	d->order.erase(p_name);
}

void ECSComponentLibrary::reload_project_schemas() {
	_ensure_builtins();
	LibraryData *d = _library();
	// Drop previously loaded schemas.
	LocalVector<StringName> to_remove;
	for (const StringName &n : d->order) {
		if (!d->infos[n].builtin) {
			to_remove.push_back(n);
		}
	}
	for (const StringName &n : to_remove) {
		remove_schema(n);
	}
	if (!ProjectSettings::get_singleton() || !ProjectSettings::get_singleton()->has_setting(SETTING_SCHEMAS)) {
		return;
	}
	PackedStringArray paths = GLOBAL_GET(SETTING_SCHEMAS);
	for (const String &path : paths) {
		if (path.is_empty()) {
			continue;
		}
		Ref<ECSComponentSchema> schema = ResourceLoader::load(path, "ECSComponentSchema");
		if (schema.is_null()) {
			WARN_PRINT(vformat("ECS: unable to load component schema '%s'.", path));
			continue;
		}
		add_schema(schema);
	}
}

const ECSComponentLibrary::Info *ECSComponentLibrary::get_info(const StringName &p_name) {
	_ensure_builtins();
	return _library()->infos.getptr(p_name);
}

void ECSComponentLibrary::get_component_names(List<StringName> *r_names) {
	_ensure_builtins();
	for (const StringName &n : _library()->order) {
		r_names->push_back(n);
	}
}

Variant::Type ECSComponentLibrary::get_field_type(const StringName &p_component, const StringName &p_field) {
	const Info *info = get_info(p_component);
	if (!info) {
		return Variant::NIL;
	}
	for (const FieldInfo &f : info->fields) {
		if (f.name == p_field) {
			return f.type;
		}
	}
	return Variant::NIL;
}

void ECSComponentLibrary::register_schemas(ecs::World &p_world) {
	_ensure_builtins();
	LibraryData *d = _library();
	for (const StringName &n : d->order) {
		const Info &info = d->infos[n];
		if (info.builtin || info.schema.is_null()) {
			continue;
		}
		if (p_world.find_component(n) != ecs::INVALID_COMPONENT) {
			continue;
		}
		String err;
		ecs::ComponentId id = p_world.register_script_component(n, info.schema->get_fields(), info.flags, &err);
		if (id == ecs::INVALID_COMPONENT) {
			WARN_PRINT(vformat("ECS: unable to register schema component '%s': %s", n, err));
		}
	}
}
