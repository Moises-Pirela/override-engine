/**************************************************************************/
/*  ecs_events_panel.cpp                                                  */
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

#include "ecs_events_panel.h"

#include "../core/ecs_world.h"
#include "../resources/ecs_event_library.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/separator.h"

void ECSEventsPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			remove_button->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
			script_button->set_button_icon(get_editor_theme_icon(SNAME("ScriptCreate")));
		} break;
		case NOTIFICATION_ENTER_TREE: {
			if (ProjectSettings::get_singleton()) {
				ProjectSettings::get_singleton()->connect("settings_changed", callable_mp(this, &ECSEventsPanel::_settings_changed));
			}
			_refresh();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (ProjectSettings::get_singleton()) {
				ProjectSettings::get_singleton()->disconnect("settings_changed", callable_mp(this, &ECSEventsPanel::_settings_changed));
			}
		} break;
	}
}

void ECSEventsPanel::_settings_changed() {
	ECSEventLibrary::reload_project_events();
	_refresh();
}

void ECSEventsPanel::_set_status(const String &p_text, bool p_error) {
	status->set_text(p_text);
	status->set_modulate(p_error ? get_theme_color(SNAME("error_color"), EditorStringName(Editor)) : Color(1, 1, 1, 0.7));
}

PackedStringArray ECSEventsPanel::get_events() const {
	return ECSEventLibrary::get_event_names_packed();
}

PackedStringArray ECSEventsPanel::get_selected_events() const {
	PackedStringArray r;
	for (int idx : custom_list->get_selected_items()) {
		r.push_back(custom_list->get_item_text(idx));
	}
	for (int idx : builtin_list->get_selected_items()) {
		r.push_back(builtin_list->get_item_text(idx));
	}
	return r;
}

void ECSEventsPanel::_refresh() {
	PackedStringArray selected = get_selected_events();
	custom_list->clear();
	PackedStringArray names = ECSEventLibrary::get_event_names_packed();
	for (int i = 0; i < names.size(); i++) {
		custom_list->add_item(names[i]);
		if (selected.has(names[i])) {
			custom_list->select(i, false);
		}
	}
	if (builtin_list->get_item_count() == 0) {
		for (uint32_t i = 0; i < ecs::EVENT_BUILTIN_MAX; i++) {
			builtin_list->add_item(ecs::World::get_builtin_event_name(i));
		}
	}
	_selection_changed();
}

bool ECSEventsPanel::add_event(const String &p_name) {
	String name = p_name.strip_edges();
	String err;
	if (!ECSEventLibrary::is_valid_event_name(name, &err)) {
		_set_status(err, true);
		return false;
	}
	PackedStringArray names = get_events();
	if (names.has(name)) {
		_set_status(vformat(TTR("Event '%s' already exists."), name), true);
		return false;
	}
	names.push_back(name);
	Error save_err = ECSEventLibrary::save_project_events(names);
	if (save_err != OK) {
		_set_status(TTR("Unable to save the project settings."), true);
		return false;
	}
	_refresh();
	for (int i = 0; i < custom_list->get_item_count(); i++) {
		if (custom_list->get_item_text(i) == name) {
			custom_list->select(i);
		}
	}
	_selection_changed();
	_set_status(vformat(TTR("Added event '%s'. Systems can now subscribe to it by name."), name), false);
	return true;
}

bool ECSEventsPanel::remove_event(const String &p_name) {
	PackedStringArray names = get_events();
	int idx = names.find(p_name);
	if (idx < 0) {
		return false;
	}
	names.remove_at(idx);
	if (ECSEventLibrary::save_project_events(names) != OK) {
		_set_status(TTR("Unable to save the project settings."), true);
		return false;
	}
	_refresh();
	_set_status(vformat(TTR("Removed event '%s'."), p_name), false);
	return true;
}

void ECSEventsPanel::_add_pressed() {
	if (add_event(name_edit->get_text())) {
		name_edit->clear();
	}
}

void ECSEventsPanel::_name_submitted(const String &p_text) {
	if (add_event(p_text)) {
		name_edit->clear();
	}
}

void ECSEventsPanel::_remove_pressed() {
	Vector<int> selected = custom_list->get_selected_items();
	PackedStringArray names;
	for (int idx : selected) {
		names.push_back(custom_list->get_item_text(idx));
	}
	for (const String &n : names) {
		remove_event(n);
	}
}

void ECSEventsPanel::_selection_changed() {
	remove_button->set_disabled(custom_list->get_selected_items().is_empty());
	script_button->set_disabled(get_selected_events().is_empty());
}

String ECSEventsPanel::make_system_script(const PackedStringArray &p_events, const String &p_class_name) {
	String events;
	String cases;
	for (int i = 0; i < p_events.size(); i++) {
		String name = p_events[i];
		if (i > 0) {
			events += ", ";
		}
		ecs::EventId builtin = ecs::World::find_builtin_event(name);
		if (builtin != ecs::INVALID_EVENT) {
			String constant = "ECSWorld.EVENT_" + name.to_upper();
			events += constant;
			cases += "\t\t" + constant + ":\n\t\t\tpass # " + name + "\n";
		} else {
			events += "&\"" + name + "\"";
		}
	}
	bool has_custom = false;
	for (const String &name : p_events) {
		if (ecs::World::find_builtin_event(name) == ecs::INVALID_EVENT) {
			has_custom = true;
		}
	}
	String script;
	if (!p_class_name.is_empty()) {
		script += "class_name " + p_class_name + "\n";
	}
	script += "extends ECSSystem\n\n";
	script += "# Event driven system: declare the events in _get_events() and handle them in _process_event().\n";
	script += "# Register with: get_world().add_system_object(" + (p_class_name.is_empty() ? String("load(\"res://...\").new()") : p_class_name + ".new()") + ")\n\n\n";
	script += "func _get_events() -> Array:\n\treturn [" + events + "]\n\n\n";
	script += "func _get_component_filter() -> Array:\n\treturn [] # e.g. [&\"Health\"] to only receive entity events for entities with Health.\n\n\n";
	script += "func _process_event(event: Dictionary) -> void:\n";
	script += "\t# get_world() gives access to the ECSWorld (components, queries, emit_event...).\n";
	script += "\tmatch event.event:\n";
	script += cases;
	script += "\t\t_:\n";
	if (has_custom) {
		script += "\t\t\tmatch event.name:\n";
		for (const String &name : p_events) {
			if (ecs::World::find_builtin_event(name) == ecs::INVALID_EVENT) {
				script += "\t\t\t\t&\"" + name + "\":\n\t\t\t\t\tpass # payload: event.payload, entity: event.entity\n";
			}
		}
	} else {
		script += "\t\t\tpass\n";
	}
	return script;
}

Error ECSEventsPanel::create_system_script(const String &p_path, const PackedStringArray &p_events) {
	String class_name = p_path.get_file().get_basename().to_pascal_case();
	if (!class_name.is_valid_ascii_identifier()) {
		class_name = String();
	}
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, vformat("Unable to write '%s'.", p_path));
	f->store_string(make_system_script(p_events, class_name));
	f.unref();
	if (EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->update_file(p_path);
	}
	return OK;
}

void ECSEventsPanel::_script_pressed() {
	if (get_selected_events().is_empty()) {
		return;
	}
	if (!script_dialog) {
		script_dialog = memnew(EditorFileDialog);
		script_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
		script_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
		script_dialog->add_filter("*.gd", "GDScript");
		script_dialog->set_title(TTR("Create ECS System Script"));
		script_dialog->connect("file_selected", callable_mp(this, &ECSEventsPanel::_script_path_selected));
		add_child(script_dialog);
	}
	script_dialog->set_current_file("new_system.gd");
	script_dialog->popup_file_dialog();
}

void ECSEventsPanel::_script_path_selected(const String &p_path) {
	PackedStringArray events = get_selected_events();
	Error err = create_system_script(p_path, events);
	if (err != OK) {
		_set_status(vformat(TTR("Unable to create '%s'."), p_path), true);
		return;
	}
	_set_status(vformat(TTR("Created %s subscribed to %d event(s)."), p_path, events.size()), false);
	Ref<Script> script = ResourceLoader::load(p_path, "Script");
	if (script.is_valid()) {
		EditorInterface::get_singleton()->edit_script(script);
	}
}

void ECSEventsPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_event", "name"), &ECSEventsPanel::add_event);
	ClassDB::bind_method(D_METHOD("remove_event", "name"), &ECSEventsPanel::remove_event);
	ClassDB::bind_method(D_METHOD("get_events"), &ECSEventsPanel::get_events);
	ClassDB::bind_method(D_METHOD("get_selected_events"), &ECSEventsPanel::get_selected_events);
	ClassDB::bind_method(D_METHOD("create_system_script", "path", "events"), &ECSEventsPanel::create_system_script);
	ClassDB::bind_static_method("ECSEventsPanel", D_METHOD("make_system_script", "events", "class_name"), &ECSEventsPanel::make_system_script);
}

ECSEventsPanel::ECSEventsPanel() {
	set_custom_minimum_size(Size2(0, 180) * EDSCALE);

	VBoxContainer *left = memnew(VBoxContainer);
	left->set_h_size_flags(SIZE_EXPAND_FILL);
	left->set_stretch_ratio(2);
	add_child(left);

	Label *title = memnew(Label(TTR("Custom Events")));
	left->add_child(title);

	custom_list = memnew(ItemList);
	custom_list->set_select_mode(ItemList::SELECT_MULTI);
	custom_list->set_v_size_flags(SIZE_EXPAND_FILL);
	custom_list->set_auto_height(false);
	custom_list->connect("multi_selected", callable_mp(this, &ECSEventsPanel::_selection_changed).unbind(2));
	custom_list->connect("empty_clicked", callable_mp(this, &ECSEventsPanel::_selection_changed).unbind(2));
	left->add_child(custom_list);

	HBoxContainer *actions = memnew(HBoxContainer);
	left->add_child(actions);

	name_edit = memnew(LineEdit);
	name_edit->set_placeholder(TTR("New event name, e.g. OnPlayerHeal"));
	name_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	name_edit->connect(SceneStringName(text_submitted), callable_mp(this, &ECSEventsPanel::_name_submitted));
	actions->add_child(name_edit);

	add_button = memnew(Button);
	add_button->set_text(TTR("Add Event"));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &ECSEventsPanel::_add_pressed));
	actions->add_child(add_button);

	remove_button = memnew(Button);
	remove_button->set_text(TTR("Remove"));
	remove_button->set_disabled(true);
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &ECSEventsPanel::_remove_pressed));
	actions->add_child(remove_button);

	script_button = memnew(Button);
	script_button->set_text(TTR("New System Script..."));
	script_button->set_tooltip_text(TTR("Creates a GDScript extending ECSSystem subscribed to the selected events."));
	script_button->set_disabled(true);
	script_button->connect(SceneStringName(pressed), callable_mp(this, &ECSEventsPanel::_script_pressed));
	actions->add_child(script_button);

	status = memnew(Label);
	status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status->set_text(TTR("Events are stored in Project Settings > ECS > Events > Custom and registered in every ECSWorld. Reference them by name in ECSSystem._get_events(), ECSWorld.subscribe() and ECSWorld.emit_event()."));
	status->set_modulate(Color(1, 1, 1, 0.7));
	left->add_child(status);

	add_child(memnew(VSeparator));

	VBoxContainer *right = memnew(VBoxContainer);
	right->set_h_size_flags(SIZE_EXPAND_FILL);
	add_child(right);

	Label *builtin_title = memnew(Label(TTR("Engine Lifecycle Events")));
	right->add_child(builtin_title);

	builtin_list = memnew(ItemList);
	builtin_list->set_select_mode(ItemList::SELECT_MULTI);
	builtin_list->set_v_size_flags(SIZE_EXPAND_FILL);
	builtin_list->set_auto_height(false);
	builtin_list->connect("multi_selected", callable_mp(this, &ECSEventsPanel::_selection_changed).unbind(2));
	builtin_list->connect("empty_clicked", callable_mp(this, &ECSEventsPanel::_selection_changed).unbind(2));
	right->add_child(builtin_list);
}
