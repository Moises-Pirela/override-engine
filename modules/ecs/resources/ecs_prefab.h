/**************************************************************************/
/*  ecs_prefab.h                                                          */
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

#include "../core/ecs_component.h"

#include "core/io/resource.h"
#include "core/templates/hash_map.h"
#include "core/variant/typed_array.h"

namespace ecs {
class World;
}

// A component type declared as a resource: a name, a {field: type} map and
// flags. Schemas listed in the `ecs/components/schemas` project setting are
// registered in every ECSWorld automatically and are offered by the prefab
// editor, so components can be designed in the editor without any code.
class ECSComponentSchema : public Resource {
	GDCLASS(ECSComponentSchema, Resource);

	StringName component_name;
	Dictionary fields; // name -> Variant::Type (int) or ECSWorld.FIELD_TYPE_ENTITY.
	int flags = 0;

protected:
	static void _bind_methods();

public:
	void set_component_name(const StringName &p_name);
	StringName get_component_name() const;

	void set_fields(const Dictionary &p_fields);
	Dictionary get_fields() const;
	void add_field(const StringName &p_name, int p_type);
	void remove_field(const StringName &p_name);
	bool has_field(const StringName &p_name) const;
	int get_field_type(const StringName &p_name) const;
	PackedStringArray get_field_names() const;

	void set_flags(int p_flags);
	int get_flags() const;

	// Human readable type names accepted by add_field / register_component.
	static PackedStringArray get_supported_type_names();
	static int type_from_name(const String &p_name);
	static String name_from_type(int p_type);
};

// Describes an entity (a set of components with values) and optionally a
// hierarchy of child prefabs. Prefabs are plain resources: create them from
// the editor (New Resource > ECSPrefab), edit components in the inspector,
// then spawn them with ECSWorld.instantiate_prefab().
//
// Components are exposed to the inspector as dynamic properties
// ("components/<Component>/<field>") typed from the component reflection
// (built-in components and schemas), so every field gets the right editor.
class ECSPrefab : public Resource {
	GDCLASS(ECSPrefab, Resource);

	Dictionary components; // name -> Dictionary of field values (insertion ordered).
	TypedArray<ECSPrefab> children;

protected:
	static void _bind_methods();
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _property_can_revert(const StringName &p_name) const;
	bool _property_get_revert(const StringName &p_name, Variant &r_property) const;

public:
	void set_components(const Dictionary &p_components);
	Dictionary get_components() const;

	void add_component(const StringName &p_name, const Dictionary &p_values = Dictionary());
	void remove_component(const StringName &p_name);
	bool has_component(const StringName &p_name) const;
	PackedStringArray get_component_names() const;
	Dictionary get_component(const StringName &p_name) const;
	void set_component_value(const StringName &p_component, const StringName &p_field, const Variant &p_value);
	Variant get_component_value(const StringName &p_component, const StringName &p_field) const;

	void set_children(const TypedArray<ECSPrefab> &p_children);
	TypedArray<ECSPrefab> get_children() const;
	void add_child(const Ref<ECSPrefab> &p_child);
	int get_child_count() const;
	Ref<ECSPrefab> get_child(int p_index) const;
	// Resolves a "i/j/k" index path into the nested child prefab.
	Ref<ECSPrefab> find_sub_prefab(const String &p_index_path) const;

	// Prefab path used by the built-in `Prefab` component: resource path plus
	// an optional "#i/j" child index path.
	static String make_sub_path(const String &p_path, const String &p_index_path);
	static Ref<ECSPrefab> load_from_path(const String &p_path);
};

// Edit-time and runtime knowledge about component types: the built-in
// components (reflected from a scratch ecs::World) and the project schemas.
class ECSComponentLibrary {
public:
	struct FieldInfo {
		StringName name;
		Variant::Type type = Variant::NIL;
		bool entity = false; // Entity reference (shown as int).
		bool internal = false; // Runtime handle (RID), not editable.
	};
	struct Info {
		StringName name;
		Vector<FieldInfo> fields;
		uint32_t flags = 0;
		bool builtin = false;
		Ref<ECSComponentSchema> schema;
	};

	static const char *SETTING_SCHEMAS;

	// (Re)loads the schemas listed in the project settings.
	static void reload_project_schemas();
	// Registers a schema for the editor / future worlds (not persisted).
	static void add_schema(const Ref<ECSComponentSchema> &p_schema);
	static void remove_schema(const StringName &p_name);

	static const Info *get_info(const StringName &p_name);
	static void get_component_names(List<StringName> *r_names);
	static Variant::Type get_field_type(const StringName &p_component, const StringName &p_field);

	// Registers every known schema in a world (called by ECSWorld).
	static void register_schemas(ecs::World &p_world);
	static void cleanup();

private:
	static void _ensure_builtins();
};
