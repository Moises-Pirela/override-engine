/**************************************************************************/
/*  ecs_profiler_panel.cpp                                                */
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

#include "ecs_profiler_panel.h"

#include "../bindings/ecs_profiler.h"

#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"

enum SystemColumns {
	COL_INDEX,
	COL_NAME,
	COL_STAGE,
	COL_BATCH,
	COL_TYPE,
	COL_ENABLED,
	COL_ORDER,
	COL_LAST,
	COL_AVG,
	COL_MAX,
	COL_RUNS,
	COL_EVENTS,
	COL_MAX_COLUMNS
};

void ECSProfilerPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_profiles", "profiles"), &ECSProfilerPanel::set_profiles);
	ClassDB::bind_method(D_METHOD("get_profiles"), &ECSProfilerPanel::get_profiles);
	ClassDB::bind_method(D_METHOD("set_local_mode", "enable"), &ECSProfilerPanel::set_local_mode);
	ClassDB::bind_method(D_METHOD("is_local_mode"), &ECSProfilerPanel::is_local_mode);
	ClassDB::bind_method(D_METHOD("get_world_names"), &ECSProfilerPanel::get_world_names);
	ClassDB::bind_method(D_METHOD("get_selected_profile"), &ECSProfilerPanel::get_selected_profile);
	ClassDB::bind_method(D_METHOD("get_system_rows"), &ECSProfilerPanel::get_system_rows);
}

void ECSProfilerPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			summary->add_theme_font_override(SceneStringName(font), get_theme_font(SNAME("bold"), EditorStringName(EditorFonts)));
		} break;
		case NOTIFICATION_READY: {
			if (local_mode) {
				poll_timer->start();
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (local_mode && is_visible_in_tree()) {
				_poll_local();
			}
		} break;
	}
}

void ECSProfilerPanel::set_local_mode(bool p_enable) {
	local_mode = p_enable;
	if (local_mode) {
		if (is_inside_tree()) {
			poll_timer->start();
		}
		status->set_text(TTR("Edit time: worlds living in the editor process. Run the project to profile the game in the debugger's ECS Profiler tab."));
		_poll_local();
	} else {
		poll_timer->stop();
	}
}

void ECSProfilerPanel::_poll_local() {
	if (!local_mode || !is_visible_in_tree()) {
		return;
	}
	LocalVector<ecs::World *> worlds;
	ecs::World::get_all_worlds(worlds);
	if (worlds.is_empty() && preview_world.is_null()) {
		// Nothing lives in the editor yet: show what a new world looks like
		// (built-in systems and their order, schemas, project events).
		preview_world.instantiate();
		preview_world->set_name("Design-time preview");
	}
	set_profiles(ECSProfiler::collect());
}

void ECSProfilerPanel::started() {
	status->set_text(TTR("Profiling the running game."));
}

void ECSProfilerPanel::stopped() {
	status->set_text(TTR("Game stopped. Showing the last snapshot."));
}

void ECSProfilerPanel::set_profiles(const Array &p_profiles) {
	profiles = p_profiles;
	_rebuild();
}

PackedStringArray ECSProfilerPanel::get_world_names() const {
	PackedStringArray r;
	for (int i = 0; i < profiles.size(); i++) {
		Dictionary p = profiles[i];
		r.push_back(p.get("name", ""));
	}
	return r;
}

Dictionary ECSProfilerPanel::_current_profile() const {
	for (int i = 0; i < profiles.size(); i++) {
		Dictionary p = profiles[i];
		if (int64_t(p.get("serial", -1)) == selected_serial) {
			return p;
		}
	}
	if (profiles.size() > 0) {
		return profiles[0];
	}
	return Dictionary();
}

Array ECSProfilerPanel::get_system_rows() const {
	return _current_profile().get("systems", Array());
}

void ECSProfilerPanel::_world_selected(int p_index) {
	if (p_index >= 0 && p_index < profiles.size()) {
		Dictionary p = profiles[p_index];
		selected_serial = int64_t(p.get("serial", -1));
	}
	_rebuild();
}

void ECSProfilerPanel::_rebuild() {
	// World selector.
	world_select->clear();
	int selected_index = -1;
	for (int i = 0; i < profiles.size(); i++) {
		Dictionary p = profiles[i];
		String n = p.get("name", "");
		if (n.is_empty()) {
			n = vformat("World %d", int64_t(p.get("serial", 0)));
		}
		world_select->add_item(n, i);
		if (int64_t(p.get("serial", -1)) == selected_serial) {
			selected_index = i;
		}
	}
	if (selected_index < 0 && profiles.size() > 0) {
		selected_index = 0;
		selected_serial = int64_t(Dictionary(profiles[0]).get("serial", -1));
	}
	if (selected_index >= 0) {
		world_select->select(selected_index);
	}
	world_select->set_disabled(profiles.size() <= 1);

	Dictionary profile = _current_profile();
	if (profile.is_empty()) {
		summary->set_text(TTR("No ECS world."));
		systems_tree->clear();
		archetypes_tree->clear();
		return;
	}
	Dictionary mem = profile.get("memory", Dictionary());
	summary->set_text(vformat(TTR("Entities: %d  |  Archetypes: %d  |  Components: %d  |  Systems: %d active / %d  |  Subscriptions: %d  |  Memory: %s (tables %s, sparse %s, entities %s)  |  update: %d µs  physics: %d µs  |  events/frame: %d  |  tick: %d"),
			int64_t(profile.get("entities", 0)), int64_t(profile.get("archetypes", 0)), int64_t(profile.get("components", 0)),
			int64_t(profile.get("active_systems", 0)), int64_t(profile.get("system_count", 0)), int64_t(profile.get("subscription_count", 0)),
			String::humanize_size(uint64_t(int64_t(mem.get("total", 0)))), String::humanize_size(uint64_t(int64_t(mem.get("archetypes", 0)))),
			String::humanize_size(uint64_t(int64_t(mem.get("sparse", 0)))), String::humanize_size(uint64_t(int64_t(mem.get("entities", 0)))),
			int64_t(profile.get("update_us", 0)), int64_t(profile.get("physics_us", 0)), int64_t(profile.get("events_per_frame", 0)), int64_t(profile.get("tick", 0))));
	_rebuild_systems(profile);
	_rebuild_archetypes(profile);
}

void ECSProfilerPanel::_rebuild_systems(const Dictionary &p_profile) {
	systems_tree->clear();
	TreeItem *root = systems_tree->create_item();
	Array rows = p_profile.get("systems", Array());
	const Color disabled_color = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	for (int i = 0; i < rows.size(); i++) {
		Dictionary row = rows[i];
		TreeItem *item = systems_tree->create_item(root);
		bool enabled = row.get("enabled", true);
		bool parallel = row.get("parallel", false);
		int64_t batch = row.get("batch", -1);
		Array events = row.get("events", Array());
		String events_text;
		for (int e = 0; e < events.size(); e++) {
			if (e > 0) {
				events_text += ", ";
			}
			events_text += String(events[e]);
		}
		item->set_text(COL_INDEX, itos(int64_t(row.get("index", i)) + 1));
		item->set_text(COL_NAME, row.get("name", ""));
		item->set_text(COL_STAGE, row.get("stage", ""));
		item->set_text(COL_BATCH, batch < 0 ? String("-") : (parallel ? vformat("%d (parallel)", batch) : itos(batch)));
		item->set_text(COL_TYPE, row.get("type", ""));
		item->set_cell_mode(COL_ENABLED, TreeItem::CELL_MODE_CHECK);
		item->set_checked(COL_ENABLED, enabled);
		item->set_editable(COL_ENABLED, false);
		item->set_text(COL_ORDER, itos(int64_t(row.get("order", 0))));
		item->set_text(COL_LAST, itos(int64_t(row.get("last_us", 0))));
		item->set_text(COL_AVG, vformat("%.1f", double(row.get("avg_us", 0.0))));
		item->set_text(COL_MAX, itos(int64_t(row.get("max_us", 0))));
		item->set_text(COL_RUNS, itos(int64_t(row.get("runs", 0))));
		item->set_text(COL_EVENTS, events_text);
		item->set_tooltip_text(COL_NAME, enabled ? TTR("Running") : TTR("Disabled: the system is registered but skipped."));
		if (!enabled) {
			for (int c = 0; c < COL_MAX_COLUMNS; c++) {
				item->set_custom_color(c, disabled_color);
			}
		}
	}
}

void ECSProfilerPanel::_rebuild_archetypes(const Dictionary &p_profile) {
	archetypes_tree->clear();
	TreeItem *root = archetypes_tree->create_item();
	Array rows = p_profile.get("archetype_list", Array());
	for (int i = 0; i < rows.size(); i++) {
		Dictionary row = rows[i];
		Array comps = row.get("components", Array());
		String comps_text;
		for (int c = 0; c < comps.size(); c++) {
			if (c > 0) {
				comps_text += ", ";
			}
			comps_text += String(comps[c]);
		}
		if (comps_text.is_empty()) {
			comps_text = TTR("(empty entities)");
		}
		TreeItem *item = archetypes_tree->create_item(root);
		item->set_text(0, comps_text);
		item->set_text(1, itos(int64_t(row.get("entities", 0))));
		item->set_text(2, itos(int64_t(row.get("capacity", 0))));
		item->set_text(3, String::humanize_size(uint64_t(int64_t(row.get("memory", 0)))));
	}
}

ECSProfilerPanel::ECSProfilerPanel() {
	set_custom_minimum_size(Size2(0, 220) * EDSCALE);

	HBoxContainer *top = memnew(HBoxContainer);
	add_child(top);
	Label *world_label = memnew(Label(TTR("World:")));
	top->add_child(world_label);
	world_select = memnew(OptionButton);
	world_select->set_custom_minimum_size(Size2(180 * EDSCALE, 0));
	world_select->connect(SceneStringName(item_selected), callable_mp(this, &ECSProfilerPanel::_world_selected));
	top->add_child(world_select);
	status = memnew(Label);
	status->set_h_size_flags(SIZE_EXPAND_FILL);
	status->set_modulate(Color(1, 1, 1, 0.7));
	top->add_child(status);

	summary = memnew(Label);
	summary->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(summary);

	tabs = memnew(TabContainer);
	tabs->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(tabs);

	systems_tree = memnew(Tree);
	systems_tree->set_name(TTR("Systems"));
	systems_tree->set_hide_root(true);
	systems_tree->set_columns(COL_MAX_COLUMNS);
	systems_tree->set_column_titles_visible(true);
	const char *titles[COL_MAX_COLUMNS] = { "#", "System", "Stage", "Batch", "Type", "Active", "Order", "Last (µs)", "Avg (µs)", "Max (µs)", "Runs", "Events" };
	for (int c = 0; c < COL_MAX_COLUMNS; c++) {
		systems_tree->set_column_title(c, TTR(titles[c]));
		systems_tree->set_column_expand(c, c == COL_NAME || c == COL_EVENTS);
		systems_tree->set_column_custom_minimum_width(c, (c == COL_NAME ? 160 : 60) * EDSCALE);
	}
	systems_tree->set_column_title_tooltip_text(COL_INDEX, TTR("Execution order within a frame: frame stages, then fixed stages, then event subscriptions."));
	systems_tree->set_column_title_tooltip_text(COL_BATCH, TTR("Systems of the same batch run in parallel on the worker thread pool."));
	tabs->add_child(systems_tree);

	archetypes_tree = memnew(Tree);
	archetypes_tree->set_name(TTR("Archetypes"));
	archetypes_tree->set_hide_root(true);
	archetypes_tree->set_columns(4);
	archetypes_tree->set_column_titles_visible(true);
	archetypes_tree->set_column_title(0, TTR("Components"));
	archetypes_tree->set_column_title(1, TTR("Entities"));
	archetypes_tree->set_column_title(2, TTR("Capacity"));
	archetypes_tree->set_column_title(3, TTR("Memory"));
	archetypes_tree->set_column_expand(0, true);
	for (int c = 1; c < 4; c++) {
		archetypes_tree->set_column_expand(c, false);
		archetypes_tree->set_column_custom_minimum_width(c, 90 * EDSCALE);
	}
	tabs->add_child(archetypes_tree);

	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(0.5);
	poll_timer->set_one_shot(false);
	poll_timer->connect("timeout", callable_mp(this, &ECSProfilerPanel::_poll_local));
	add_child(poll_timer);
}

/* ECSEditorDebugger */

bool ECSEditorDebugger::has_capture(const String &p_capture) const {
	return p_capture == "ecs";
}

bool ECSEditorDebugger::capture(const String &p_message, const Array &p_data, int p_session_id) {
	if (p_message != "ecs:profile") {
		return false;
	}
	ECSProfilerPanel **panel = panels.getptr(p_session_id);
	if (panel && *panel) {
		(*panel)->set_profiles(p_data);
	}
	return true;
}

void ECSEditorDebugger::_session_started(int p_session_id) {
	Ref<EditorDebuggerSession> session = get_session(p_session_id);
	if (session.is_valid()) {
		session->toggle_profiler("ecs", true);
	}
	ECSProfilerPanel **panel = panels.getptr(p_session_id);
	if (panel && *panel) {
		(*panel)->started();
	}
}

void ECSEditorDebugger::_session_stopped(int p_session_id) {
	ECSProfilerPanel **panel = panels.getptr(p_session_id);
	if (panel && *panel) {
		(*panel)->stopped();
	}
}

void ECSEditorDebugger::setup_session(int p_session_id) {
	Ref<EditorDebuggerSession> session = get_session(p_session_id);
	ERR_FAIL_COND(session.is_null());
	ECSProfilerPanel *panel = memnew(ECSProfilerPanel);
	panel->set_name(TTRC("ECS Profiler"));
	session->connect("started", callable_mp(this, &ECSEditorDebugger::_session_started).bind(p_session_id));
	session->connect("stopped", callable_mp(this, &ECSEditorDebugger::_session_stopped).bind(p_session_id));
	session->add_session_tab(panel);
	panels[p_session_id] = panel;
}
