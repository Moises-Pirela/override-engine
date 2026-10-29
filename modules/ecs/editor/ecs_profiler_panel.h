/**************************************************************************/
/*  ecs_profiler_panel.h                                                  */
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

#include "../bindings/ecs_world_bind.h"

#include "core/object/script_language.h"
#include "editor/debugger/editor_debugger_plugin.h"
#include "scene/gui/box_container.h"

class Label;
class OptionButton;
class TabContainer;
class Timer;
class Tree;

// Profiler view for ECS worlds: entity / archetype / component counters,
// memory consumption, the systems in the order they run (with per-system
// timings, batch and enabled state), event subscriptions and archetypes.
//
// Used in two places: as the "ECS Profiler" bottom panel at edit time (it
// polls the worlds living in the editor process, or a design-time preview
// world showing the schedule a new world gets), and as a tab of every
// debugger session, fed with "ecs:profile" messages from the running game.
class ECSProfilerPanel : public VBoxContainer {
	GDCLASS(ECSProfilerPanel, VBoxContainer);

	bool local_mode = false;
	Array profiles;
	int64_t selected_serial = -1;

	OptionButton *world_select = nullptr;
	Label *summary = nullptr;
	Label *status = nullptr;
	TabContainer *tabs = nullptr;
	Tree *systems_tree = nullptr;
	Tree *archetypes_tree = nullptr;
	Timer *poll_timer = nullptr;
	Ref<ECSWorld> preview_world;

	void _poll_local();
	void _world_selected(int p_index);
	void _rebuild();
	void _rebuild_systems(const Dictionary &p_profile);
	void _rebuild_archetypes(const Dictionary &p_profile);
	Dictionary _current_profile() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Feeds a snapshot (Array of world profiles, see ECSWorld.get_profile()).
	void set_profiles(const Array &p_profiles);
	Array get_profiles() const { return profiles; }
	// Polls ecs::World::get_all_worlds() of this process periodically.
	void set_local_mode(bool p_enable);
	bool is_local_mode() const { return local_mode; }
	void started();
	void stopped();

	PackedStringArray get_world_names() const;
	Dictionary get_selected_profile() const { return _current_profile(); }
	// Rows of the systems table for the selected world, in execution order.
	Array get_system_rows() const;

	ECSProfilerPanel();
};

// Receives "ecs:profile" messages from running games and shows them in a
// per-session ECSProfilerPanel tab.
class ECSEditorDebugger : public EditorDebuggerPlugin {
	GDCLASS(ECSEditorDebugger, EditorDebuggerPlugin);

	HashMap<int, ECSProfilerPanel *> panels;

	void _session_started(int p_session_id);
	void _session_stopped(int p_session_id);

public:
	virtual bool has_capture(const String &p_capture) const override;
	virtual bool capture(const String &p_message, const Array &p_data, int p_session_id) override;
	virtual void setup_session(int p_session_id) override;
};
