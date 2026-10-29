/**************************************************************************/
/*  ecs_events_panel.h                                                    */
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

#include "scene/gui/box_container.h"

class Button;
class EditorFileDialog;
class ItemList;
class Label;
class LineEdit;

// Bottom panel listing the project's custom ECS event types (stored in the
// `ecs/events/custom` project setting). Events are added by name / tag
// ("OnPlayerHeal") and can be referenced by systems right away: every
// ECSWorld registers them on creation. The panel can also generate an
// ECSSystem script subscribed to the selected events.
class ECSEventsPanel : public HBoxContainer {
	GDCLASS(ECSEventsPanel, HBoxContainer);

	ItemList *custom_list = nullptr;
	ItemList *builtin_list = nullptr;
	LineEdit *name_edit = nullptr;
	Button *add_button = nullptr;
	Button *remove_button = nullptr;
	Button *script_button = nullptr;
	Label *status = nullptr;
	EditorFileDialog *script_dialog = nullptr;

	void _refresh();
	void _add_pressed();
	void _name_submitted(const String &p_text);
	void _remove_pressed();
	void _selection_changed();
	void _script_pressed();
	void _script_path_selected(const String &p_path);
	void _settings_changed();
	void _set_status(const String &p_text, bool p_error);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Adds an event to the project (returns false and shows the reason if invalid).
	bool add_event(const String &p_name);
	bool remove_event(const String &p_name);
	PackedStringArray get_events() const;
	PackedStringArray get_selected_events() const;
	// Generates a GDScript extending ECSSystem that subscribes to p_events.
	static String make_system_script(const PackedStringArray &p_events, const String &p_class_name);
	Error create_system_script(const String &p_path, const PackedStringArray &p_events);

	ECSEventsPanel();
};
