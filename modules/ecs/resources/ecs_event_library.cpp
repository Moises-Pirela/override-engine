/**************************************************************************/
/*  ecs_event_library.cpp                                                 */
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

#include "ecs_event_library.h"

#include "../core/ecs_world.h"

#include "core/config/project_settings.h"

const char *ECSEventLibrary::SETTING_EVENTS = "ecs/events/custom";

static LocalVector<StringName> *event_list = nullptr;

static LocalVector<StringName> &_events() {
	if (!event_list) {
		event_list = memnew(LocalVector<StringName>);
	}
	return *event_list;
}

void ECSEventLibrary::cleanup() {
	if (event_list) {
		memdelete(event_list);
		event_list = nullptr;
	}
}

bool ECSEventLibrary::is_valid_event_name(const String &p_name, String *r_error) {
	String name = p_name.strip_edges();
	if (name.is_empty()) {
		if (r_error) {
			*r_error = "Event names can't be empty.";
		}
		return false;
	}
	for (int i = 0; i < name.length(); i++) {
		char32_t c = name[i];
		if (c <= ' ' || c == '"' || c == '\'' || c == ',' || c == ';') {
			if (r_error) {
				*r_error = "Event names can't contain whitespace or quotes.";
			}
			return false;
		}
	}
	// Built-in names are reserved.
	if (ecs::World::find_builtin_event(name) != ecs::INVALID_EVENT) {
		if (r_error) {
			*r_error = vformat("'%s' is a built-in event.", name);
		}
		return false;
	}
	return true;
}

bool ECSEventLibrary::add_event(const StringName &p_name) {
	String err;
	ERR_FAIL_COND_V_MSG(!is_valid_event_name(p_name, &err), false, err);
	LocalVector<StringName> &events = _events();
	if (events.find(p_name) >= 0) {
		return false;
	}
	events.push_back(p_name);
	return true;
}

bool ECSEventLibrary::remove_event(const StringName &p_name) {
	return _events().erase(p_name);
}

bool ECSEventLibrary::has_event(const StringName &p_name) {
	return _events().find(p_name) >= 0;
}

void ECSEventLibrary::get_event_names(List<StringName> *r_names) {
	for (const StringName &n : _events()) {
		r_names->push_back(n);
	}
}

PackedStringArray ECSEventLibrary::get_event_names_packed() {
	PackedStringArray r;
	for (const StringName &n : _events()) {
		r.push_back(n);
	}
	return r;
}

void ECSEventLibrary::reload_project_events() {
	_events().clear();
	if (!ProjectSettings::get_singleton() || !ProjectSettings::get_singleton()->has_setting(SETTING_EVENTS)) {
		return;
	}
	PackedStringArray names = GLOBAL_GET(SETTING_EVENTS);
	for (const String &n : names) {
		String name = n.strip_edges();
		if (name.is_empty()) {
			continue;
		}
		String err;
		if (!is_valid_event_name(name, &err)) {
			WARN_PRINT(vformat("ECS: ignoring custom event '%s': %s", name, err));
			continue;
		}
		if (_events().find(StringName(name)) < 0) {
			_events().push_back(StringName(name));
		}
	}
}

Error ECSEventLibrary::save_project_events(const PackedStringArray &p_names) {
	ERR_FAIL_NULL_V(ProjectSettings::get_singleton(), ERR_UNCONFIGURED);
	PackedStringArray clean;
	for (const String &n : p_names) {
		String name = n.strip_edges();
		if (!name.is_empty() && !clean.has(name)) {
			clean.push_back(name);
		}
	}
	ProjectSettings::get_singleton()->set_setting(SETTING_EVENTS, clean);
	Error err = ProjectSettings::get_singleton()->save();
	reload_project_events();
	return err;
}

void ECSEventLibrary::register_events(ecs::World &p_world) {
	for (const StringName &n : _events()) {
		p_world.register_event(n);
	}
}
