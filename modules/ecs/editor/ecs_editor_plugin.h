/**************************************************************************/
/*  ecs_editor_plugin.h                                                   */
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

#include "../resources/ecs_prefab.h"

#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"

class Button;
class Label;
class LineEdit;
class MenuButton;
class OptionButton;

// Inspector control listing the components of an ECSPrefab with add / remove
// actions. Field values are edited through the regular inspector properties
// that the prefab exposes ("components/<Component>/<field>").
class ECSPrefabComponentsEditor : public VBoxContainer {
	GDCLASS(ECSPrefabComponentsEditor, VBoxContainer);

	Ref<ECSPrefab> prefab;
	Label *title = nullptr;
	MenuButton *add_button = nullptr;
	LineEdit *custom_name = nullptr;
	Button *custom_add = nullptr;
	VBoxContainer *list = nullptr;

	void _rebuild();
	void _fill_add_menu();
	void _add_component(const StringName &p_name);
	void _menu_id_pressed(int p_id);
	void _custom_add_pressed();
	void _custom_submitted(const String &p_text);
	void _remove_component(const StringName &p_name);

protected:
	void _notification(int p_what);

public:
	void setup(const Ref<ECSPrefab> &p_prefab);
	ECSPrefabComponentsEditor();
};

// Inspector control editing the fields of an ECSComponentSchema.
class ECSComponentSchemaEditor : public VBoxContainer {
	GDCLASS(ECSComponentSchemaEditor, VBoxContainer);

	Ref<ECSComponentSchema> schema;
	Label *title = nullptr;
	VBoxContainer *rows = nullptr;
	Button *add_button = nullptr;
	bool rebuilding = false;

	void _rebuild();
	void _schema_changed();
	void _apply_fields(const Dictionary &p_new_fields, const String &p_action);
	void _add_field();
	void _remove_field(const StringName &p_name);
	void _rename_field(const String &p_new_name, const StringName &p_old_name, LineEdit *p_edit);
	void _rename_field_unfocused(const StringName &p_old_name, LineEdit *p_edit);
	void _type_selected(int p_index, const StringName &p_name, OptionButton *p_button);

protected:
	void _notification(int p_what);

public:
	void setup(const Ref<ECSComponentSchema> &p_schema);
	ECSComponentSchemaEditor();
};

class ECSPrefabInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(ECSPrefabInspectorPlugin, EditorInspectorPlugin);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};

class ECSComponentSchemaInspectorPlugin : public EditorInspectorPlugin {
	GDCLASS(ECSComponentSchemaInspectorPlugin, EditorInspectorPlugin);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};

class ECSEventsPanel;
class ECSProfilerPanel;
class ECSEditorDebugger;

class ECSEditorPlugin : public EditorPlugin {
	GDCLASS(ECSEditorPlugin, EditorPlugin);

	ECSEventsPanel *events_panel = nullptr;
	ECSProfilerPanel *profiler_panel = nullptr;
	Ref<ECSEditorDebugger> debugger;

	void _reload_schemas();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "ECS"; }
	ECSEventsPanel *get_events_panel() const { return events_panel; }
	ECSProfilerPanel *get_profiler_panel() const { return profiler_panel; }

	ECSEditorPlugin();
	~ECSEditorPlugin();
};
