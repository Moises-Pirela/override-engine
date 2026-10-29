/**************************************************************************/
/*  ecs_editor_plugin.cpp                                                 */
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

#include "ecs_editor_plugin.h"

#include "ecs_events_panel.h"
#include "ecs_profiler_panel.h"

#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/separator.h"

/* ECSPrefabComponentsEditor */

void ECSPrefabComponentsEditor::setup(const Ref<ECSPrefab> &p_prefab) {
	prefab = p_prefab;
	_rebuild();
}

void ECSPrefabComponentsEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			title->add_theme_font_override(SceneStringName(font), get_theme_font(SNAME("bold"), EditorStringName(EditorFonts)));
			add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			custom_add->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			_rebuild();
		} break;
	}
}

void ECSPrefabComponentsEditor::_fill_add_menu() {
	PopupMenu *popup = add_button->get_popup();
	popup->clear();
	List<StringName> names;
	ECSComponentLibrary::get_component_names(&names);
	int id = 0;
	bool separator_added = false;
	for (const StringName &n : names) {
		const ECSComponentLibrary::Info *info = ECSComponentLibrary::get_info(n);
		if (prefab.is_valid() && prefab->has_component(n)) {
			id++;
			continue;
		}
		if (info && !info->builtin && !separator_added) {
			popup->add_separator(TTR("Project Schemas"));
			separator_added = true;
		}
		popup->add_item(n, id);
		popup->set_item_metadata(popup->get_item_index(id), n);
		id++;
	}
	if (popup->get_item_count() == 0) {
		popup->add_item(TTR("All known components added"), -1);
		popup->set_item_disabled(0, true);
	}
}

void ECSPrefabComponentsEditor::_rebuild() {
	if (!list) {
		return;
	}
	while (list->get_child_count()) {
		Node *c = list->get_child(0);
		list->remove_child(c);
		c->queue_free();
	}
	if (prefab.is_null()) {
		return;
	}
	PackedStringArray names = prefab->get_component_names();
	if (names.is_empty()) {
		Label *empty = memnew(Label(TTR("No components. Use \"Add Component\" to start.")));
		empty->set_modulate(Color(1, 1, 1, 0.6));
		list->add_child(empty);
	}
	for (const String &name : names) {
		HBoxContainer *row = memnew(HBoxContainer);
		list->add_child(row);

		const ECSComponentLibrary::Info *info = ECSComponentLibrary::get_info(name);
		Label *label = memnew(Label(name));
		label->set_h_size_flags(SIZE_EXPAND_FILL);
		row->add_child(label);

		Label *kind = memnew(Label);
		if (!info) {
			kind->set_text(TTR("unknown"));
			kind->set_tooltip_text(TTR("No built-in component or project schema has this name. Fields are stored as-is."));
		} else if (info->builtin) {
			kind->set_text(TTR("built-in"));
		} else {
			kind->set_text(TTR("schema"));
		}
		kind->set_modulate(Color(1, 1, 1, 0.6));
		row->add_child(kind);

		Button *remove = memnew(Button);
		remove->set_flat(true);
		remove->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
		remove->set_tooltip_text(TTR("Remove component"));
		remove->connect(SceneStringName(pressed), callable_mp(this, &ECSPrefabComponentsEditor::_remove_component).bind(StringName(name)));
		row->add_child(remove);
	}
	_fill_add_menu();
}

void ECSPrefabComponentsEditor::_add_component(const StringName &p_name) {
	if (prefab.is_null() || p_name == StringName() || prefab->has_component(p_name)) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(vformat(TTR("Add Component %s"), p_name));
	undo_redo->add_do_method(prefab.ptr(), "add_component", p_name, Dictionary());
	undo_redo->add_undo_method(prefab.ptr(), "remove_component", p_name);
	undo_redo->commit_action();
}

void ECSPrefabComponentsEditor::_menu_id_pressed(int p_id) {
	PopupMenu *popup = add_button->get_popup();
	int idx = popup->get_item_index(p_id);
	if (idx < 0) {
		return;
	}
	_add_component(popup->get_item_metadata(idx));
}

void ECSPrefabComponentsEditor::_custom_add_pressed() {
	_custom_submitted(custom_name->get_text());
}

void ECSPrefabComponentsEditor::_custom_submitted(const String &p_text) {
	String name = p_text.strip_edges();
	if (name.is_empty()) {
		return;
	}
	custom_name->clear();
	_add_component(name);
}

void ECSPrefabComponentsEditor::_remove_component(const StringName &p_name) {
	if (prefab.is_null() || !prefab->has_component(p_name)) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(vformat(TTR("Remove Component %s"), p_name));
	undo_redo->add_do_method(prefab.ptr(), "remove_component", p_name);
	undo_redo->add_undo_method(prefab.ptr(), "add_component", p_name, prefab->get_component(p_name).duplicate());
	undo_redo->commit_action();
}

ECSPrefabComponentsEditor::ECSPrefabComponentsEditor() {
	title = memnew(Label(TTR("Components")));
	add_child(title);

	list = memnew(VBoxContainer);
	add_child(list);

	HBoxContainer *actions = memnew(HBoxContainer);
	add_child(actions);

	add_button = memnew(MenuButton);
	add_button->set_text(TTR("Add Component"));
	add_button->set_flat(false);
	add_button->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &ECSPrefabComponentsEditor::_menu_id_pressed));
	add_button->connect(SNAME("about_to_popup"), callable_mp(this, &ECSPrefabComponentsEditor::_fill_add_menu));
	actions->add_child(add_button);

	custom_name = memnew(LineEdit);
	custom_name->set_placeholder(TTR("Custom component name"));
	custom_name->set_h_size_flags(SIZE_EXPAND_FILL);
	custom_name->connect(SceneStringName(text_submitted), callable_mp(this, &ECSPrefabComponentsEditor::_custom_submitted));
	actions->add_child(custom_name);

	custom_add = memnew(Button);
	custom_add->set_tooltip_text(TTR("Add a component that is registered at runtime (not described by a built-in type or a schema)."));
	custom_add->connect(SceneStringName(pressed), callable_mp(this, &ECSPrefabComponentsEditor::_custom_add_pressed));
	actions->add_child(custom_add);

	add_child(memnew(HSeparator));
}

/* ECSComponentSchemaEditor */

void ECSComponentSchemaEditor::setup(const Ref<ECSComponentSchema> &p_schema) {
	if (schema.is_valid()) {
		schema->disconnect(CoreStringName(changed), callable_mp(this, &ECSComponentSchemaEditor::_schema_changed));
	}
	schema = p_schema;
	if (schema.is_valid()) {
		schema->connect(CoreStringName(changed), callable_mp(this, &ECSComponentSchemaEditor::_schema_changed));
	}
	_rebuild();
}

void ECSComponentSchemaEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			title->add_theme_font_override(SceneStringName(font), get_theme_font(SNAME("bold"), EditorStringName(EditorFonts)));
			add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			_rebuild();
		} break;
		case NOTIFICATION_PREDELETE: {
			if (schema.is_valid()) {
				schema->disconnect(CoreStringName(changed), callable_mp(this, &ECSComponentSchemaEditor::_schema_changed));
				schema.unref();
			}
		} break;
	}
}

void ECSComponentSchemaEditor::_schema_changed() {
	// Rebuild once the current signal / input has been processed.
	callable_mp(this, &ECSComponentSchemaEditor::_rebuild).call_deferred();
}

void ECSComponentSchemaEditor::_rebuild() {
	if (!rows) {
		return;
	}
	while (rows->get_child_count()) {
		Node *c = rows->get_child(0);
		rows->remove_child(c);
		c->queue_free();
	}
	if (schema.is_null()) {
		return;
	}
	PackedStringArray type_names = ECSComponentSchema::get_supported_type_names();
	Dictionary fields = schema->get_fields();
	if (fields.is_empty()) {
		Label *empty = memnew(Label(TTR("No fields. A component without fields is a tag.")));
		empty->set_modulate(Color(1, 1, 1, 0.6));
		rows->add_child(empty);
	}
	for (const KeyValue<Variant, Variant> &kv : fields) {
		StringName name = kv.key;
		int type = kv.value.get_type() == Variant::INT ? int(kv.value) : ECSComponentSchema::type_from_name(kv.value);

		HBoxContainer *row = memnew(HBoxContainer);
		rows->add_child(row);

		LineEdit *name_edit = memnew(LineEdit);
		name_edit->set_text(name);
		name_edit->set_h_size_flags(SIZE_EXPAND_FILL);
		name_edit->connect(SceneStringName(text_submitted), callable_mp(this, &ECSComponentSchemaEditor::_rename_field).bind(name, name_edit));
		name_edit->connect(SceneStringName(focus_exited), callable_mp(this, &ECSComponentSchemaEditor::_rename_field_unfocused).bind(name, name_edit));
		row->add_child(name_edit);

		OptionButton *type_button = memnew(OptionButton);
		type_button->set_custom_minimum_size(Size2(110 * EDSCALE, 0));
		int selected = -1;
		for (int i = 0; i < type_names.size(); i++) {
			type_button->add_item(type_names[i], i);
			if (ECSComponentSchema::type_from_name(type_names[i]) == type) {
				selected = i;
			}
		}
		if (selected >= 0) {
			type_button->select(selected);
		}
		type_button->connect(SceneStringName(item_selected), callable_mp(this, &ECSComponentSchemaEditor::_type_selected).bind(name, type_button));
		row->add_child(type_button);

		Button *remove = memnew(Button);
		remove->set_flat(true);
		remove->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
		remove->set_tooltip_text(TTR("Remove field"));
		remove->connect(SceneStringName(pressed), callable_mp(this, &ECSComponentSchemaEditor::_remove_field).bind(name));
		row->add_child(remove);
	}
}

void ECSComponentSchemaEditor::_apply_fields(const Dictionary &p_new_fields, const String &p_action) {
	if (schema.is_null()) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action);
	undo_redo->add_do_method(schema.ptr(), "set_fields", p_new_fields);
	undo_redo->add_undo_method(schema.ptr(), "set_fields", schema->get_fields().duplicate());
	undo_redo->commit_action();
}

void ECSComponentSchemaEditor::_add_field() {
	if (schema.is_null()) {
		return;
	}
	Dictionary fields = schema->get_fields().duplicate();
	String base = "field";
	String name = base;
	int n = 1;
	while (fields.has(name)) {
		name = base + "_" + itos(n++);
	}
	fields[StringName(name)] = int(Variant::FLOAT);
	_apply_fields(fields, TTR("Add Field"));
}

void ECSComponentSchemaEditor::_remove_field(const StringName &p_name) {
	if (schema.is_null()) {
		return;
	}
	Dictionary fields = schema->get_fields().duplicate();
	fields.erase(p_name);
	_apply_fields(fields, vformat(TTR("Remove Field %s"), p_name));
}

void ECSComponentSchemaEditor::_rename_field(const String &p_new_name, const StringName &p_old_name, LineEdit *p_edit) {
	if (schema.is_null()) {
		return;
	}
	StringName new_name = p_new_name.strip_edges();
	if (new_name == p_old_name) {
		return;
	}
	Dictionary fields = schema->get_fields();
	if (new_name == StringName() || fields.has(new_name)) {
		p_edit->set_text(p_old_name);
		return;
	}
	// Rebuild the dictionary to keep the field order.
	Dictionary renamed;
	for (const KeyValue<Variant, Variant> &kv : fields) {
		if (StringName(kv.key) == p_old_name) {
			renamed[new_name] = kv.value;
		} else {
			renamed[kv.key] = kv.value;
		}
	}
	_apply_fields(renamed, vformat(TTR("Rename Field %s"), p_old_name));
}

void ECSComponentSchemaEditor::_rename_field_unfocused(const StringName &p_old_name, LineEdit *p_edit) {
	_rename_field(p_edit->get_text(), p_old_name, p_edit);
}

void ECSComponentSchemaEditor::_type_selected(int p_index, const StringName &p_name, OptionButton *p_button) {
	if (schema.is_null()) {
		return;
	}
	int type = ECSComponentSchema::type_from_name(p_button->get_item_text(p_index));
	Dictionary fields = schema->get_fields().duplicate();
	if (!fields.has(p_name)) {
		return;
	}
	fields[p_name] = type;
	_apply_fields(fields, vformat(TTR("Change Type of %s"), p_name));
}

ECSComponentSchemaEditor::ECSComponentSchemaEditor() {
	title = memnew(Label(TTR("Fields")));
	add_child(title);

	rows = memnew(VBoxContainer);
	add_child(rows);

	add_button = memnew(Button);
	add_button->set_text(TTR("Add Field"));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &ECSComponentSchemaEditor::_add_field));
	add_child(add_button);

	Label *hint = memnew(Label(TTR("List this resource in Project Settings > ECS > Components > Schemas so every ECSWorld registers it and prefabs can use it.")));
	hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	hint->set_modulate(Color(1, 1, 1, 0.6));
	add_child(hint);

	add_child(memnew(HSeparator));
}

/* Inspector plugins */

bool ECSPrefabInspectorPlugin::can_handle(Object *p_object) {
	return Object::cast_to<ECSPrefab>(p_object) != nullptr;
}

void ECSPrefabInspectorPlugin::parse_begin(Object *p_object) {
	ECSPrefab *prefab = Object::cast_to<ECSPrefab>(p_object);
	if (!prefab) {
		return;
	}
	ECSComponentLibrary::reload_project_schemas();
	ECSPrefabComponentsEditor *editor = memnew(ECSPrefabComponentsEditor);
	editor->setup(Ref<ECSPrefab>(prefab));
	add_custom_control(editor);
}

bool ECSComponentSchemaInspectorPlugin::can_handle(Object *p_object) {
	return Object::cast_to<ECSComponentSchema>(p_object) != nullptr;
}

void ECSComponentSchemaInspectorPlugin::parse_begin(Object *p_object) {
	ECSComponentSchema *schema = Object::cast_to<ECSComponentSchema>(p_object);
	if (!schema) {
		return;
	}
	ECSComponentSchemaEditor *editor = memnew(ECSComponentSchemaEditor);
	editor->setup(Ref<ECSComponentSchema>(schema));
	add_custom_control(editor);
}

/* Plugin */

void ECSEditorPlugin::_reload_schemas() {
	ECSComponentLibrary::reload_project_schemas();
}

ECSEditorPlugin::ECSEditorPlugin() {
	Ref<ECSPrefabInspectorPlugin> prefab_plugin;
	prefab_plugin.instantiate();
	add_inspector_plugin(prefab_plugin);

	Ref<ECSComponentSchemaInspectorPlugin> schema_plugin;
	schema_plugin.instantiate();
	add_inspector_plugin(schema_plugin);

	add_tool_menu_item(TTR("Reload ECS Component Schemas"), callable_mp(this, &ECSEditorPlugin::_reload_schemas));

	events_panel = memnew(ECSEventsPanel);
	add_control_to_bottom_panel(events_panel, TTR("ECS Events"));

	profiler_panel = memnew(ECSProfilerPanel);
	profiler_panel->set_local_mode(true);
	add_control_to_bottom_panel(profiler_panel, TTR("ECS Profiler"));

	debugger.instantiate();
}

void ECSEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_debugger_plugin(debugger);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_debugger_plugin(debugger);
		} break;
	}
}

ECSEditorPlugin::~ECSEditorPlugin() {
	if (events_panel) {
		remove_control_from_bottom_panel(events_panel);
		memdelete(events_panel);
		events_panel = nullptr;
	}
	if (profiler_panel) {
		remove_control_from_bottom_panel(profiler_panel);
		memdelete(profiler_panel);
		profiler_panel = nullptr;
	}
}
