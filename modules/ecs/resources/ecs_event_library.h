/**************************************************************************/
/*  ecs_event_library.h                                                   */
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

#include "core/string/string_name.h"
#include "core/templates/list.h"
#include "core/variant/variant.h"

namespace ecs {
class World;
}

// Project level registry of custom event types ("OnPlayerHeal", ...).
//
// Events are plain names stored in the `ecs/events/custom` project setting,
// edited from the ECS Events bottom panel in the editor. They are registered
// in every ECSWorld on creation, so systems can reference them by name in
// ECSSystem._get_events() / ECSWorld.subscribe() and raise them with
// ECSWorld.emit_event() without any code registering them first.
class ECSEventLibrary {
public:
	static const char *SETTING_EVENTS;

	// (Re)loads the names from the project settings.
	static void reload_project_events();
	// Adds an event for the editor / future worlds (not persisted).
	static bool add_event(const StringName &p_name);
	static bool remove_event(const StringName &p_name);
	static bool has_event(const StringName &p_name);
	static void get_event_names(List<StringName> *r_names);
	static PackedStringArray get_event_names_packed();

	// Valid event names: non empty, no whitespace, not a built-in event name.
	static bool is_valid_event_name(const String &p_name, String *r_error = nullptr);

	// Writes the current list to the project settings (editor use).
	static Error save_project_events(const PackedStringArray &p_names);

	// Registers every known event in a world (called by ECSWorld).
	static void register_events(ecs::World &p_world);
	static void cleanup();
};
