/**************************************************************************/
/*  test_ecs_profiler.h                                                   */
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

#include "../bindings/ecs_profiler.h"
#include "../bindings/ecs_world_bind.h"
#include "../core/ecs_query.h"
#include "../core/ecs_world.h"

#include "core/object/callable_method_pointer.h"

#include "tests/test_macros.h"

namespace TestECSProfiler {

using namespace ecs;

struct Position {
	float x = 0;
};

static void _busy_system(SystemContext &p_ctx) {
	volatile double acc = 0;
	for (int i = 0; i < 2000; i++) {
		acc += i * 0.5;
	}
}

static void _noop_event(const Dictionary &) {}

TEST_CASE("[ECS][Profiler] World registry, memory and system order snapshot") {
	LocalVector<World *> before;
	World::get_all_worlds(before);
	World w;
	w.set_name("profiled");
	LocalVector<World *> after;
	World::get_all_worlds(after);
	CHECK(after.size() == before.size() + 1);
	CHECK(after.find(&w) >= 0);

	w.register_component<Position>("Position");
	SystemDesc late;
	late.name = "late_system";
	late.stage = STAGE_POST_UPDATE;
	late.func = _busy_system;
	SystemDesc early;
	early.name = "early_system";
	early.stage = STAGE_PRE_UPDATE;
	early.func = _busy_system;
	SystemDesc fixed;
	fixed.name = "fixed_system";
	fixed.stage = STAGE_PHYSICS;
	fixed.func = _busy_system;
	fixed.enabled = false;
	SubscriptionDesc sub;
	sub.name = "on_update";
	sub.events.set(EVENT_UPDATE);
	sub.callable = callable_mp_static(&_noop_event);
	SystemDesc evsys;
	evsys.name = "event_system";
	evsys.events.set(EVENT_ENTITY_CREATE);
	evsys.event_callable = callable_mp_static(&_noop_event);
	w.add_system(late);
	w.add_system(early);
	w.add_system(fixed);
	w.subscribe(sub);
	w.add_system(evsys);

	ComponentMask m;
	m.set(w.component_id<Position>());
	for (int i = 0; i < 1000; i++) {
		w.create_entity(m);
	}
	World::MemoryStats mem = w.get_memory_usage();
	CHECK(mem.archetype_bytes >= 1000 * (sizeof(Position) + sizeof(uint32_t) + sizeof(Entity)));
	CHECK(mem.entity_bytes >= 1000 * sizeof(EntityRecord));
	CHECK(mem.total() == mem.archetype_bytes + mem.sparse_bytes + mem.entity_bytes);

	w.update(0.016);
	w.physics_update(0.016);
	CHECK(w.get_last_update_usec() > 0);
	CHECK(w.get_events_last_frame() == 0); // Counted for the frame before.
	w.update(0.016);
	CHECK(w.get_events_last_frame() >= 1); // EVENT_UPDATE (+ the queued creates).

	Dictionary p = w.get_profile();
	CHECK(String(p["name"]) == "profiled");
	CHECK(int64_t(p["entities"]) == 1000);
	CHECK(int64_t(p["archetypes"]) == 2);
	CHECK(int64_t(Dictionary(p["memory"])["total"]) == int64_t(mem.total()));
	CHECK(int64_t(p["system_count"]) == 3);
	CHECK(int64_t(p["active_systems"]) == 2);
	CHECK(int64_t(p["subscription_count"]) == 2);

	// Execution order: frame stages first (early before late), the update
	// subscription after the update stage, then fixed stages, then event systems.
	Array rows = p["systems"];
	REQUIRE(rows.size() == 5);
	Vector<String> names;
	for (int i = 0; i < rows.size(); i++) {
		Dictionary r = rows[i];
		names.push_back(r["name"]);
		CHECK(int64_t(r["index"]) == i);
	}
	CHECK(names[0] == "early_system");
	CHECK(names[1] == "on_update");
	CHECK(names[2] == "late_system");
	CHECK(names[3] == "fixed_system");
	CHECK(names[4] == "event_system");
	Dictionary early_row = rows[0];
	CHECK(String(early_row["stage"]) == "pre_update");
	CHECK(String(early_row["type"]) == "system");
	CHECK(bool(early_row["enabled"]));
	CHECK(int64_t(early_row["runs"]) == 2);
	CHECK(int64_t(early_row["last_us"]) >= 0);
	CHECK(double(early_row["avg_us"]) >= 0);
	Dictionary sub_row = rows[1];
	CHECK(String(sub_row["stage"]) == "update (event)");
	CHECK(String(sub_row["type"]) == "subscription");
	CHECK(Array(sub_row["events"]).size() == 1);
	CHECK(String(Array(sub_row["events"])[0]) == "update");
	Dictionary fixed_row = rows[3];
	CHECK_FALSE(bool(fixed_row["enabled"]));
	CHECK(int64_t(fixed_row["runs"]) == 0);
	Dictionary ev_row = rows[4];
	CHECK(String(ev_row["type"]) == "event system");
	CHECK(String(ev_row["stage"]) == "events");
	CHECK(int64_t(ev_row["runs"]) >= 1); // 1000 queued creates were delivered.

	Array archs = p["archetype_list"];
	REQUIRE(archs.size() == 2);
	Dictionary filled = archs[1];
	CHECK(int64_t(filled["entities"]) == 1000);
	CHECK(int64_t(filled["capacity"]) >= 1000);
	CHECK(Array(filled["components"]).size() == 1);
}

TEST_CASE("[ECS][Profiler][Bindings] ECSWorld profiles, names and all_profiles") {
	Ref<ECSWorld> world;
	world.instantiate();
	CHECK(world->get_name() == StringName());
	CHECK(String(world->get_profile()["name"]).begins_with("ECSWorld ")); // Default display name.
	world->set_name("game");
	CHECK(world->get_name() == StringName("game"));
	world->create_entity();
	world->update(0.016);
	Dictionary p = world->get_profile();
	CHECK(String(p["name"]) == "game");
	CHECK(int64_t(p["entities"]) == 1);
	CHECK(world->get_memory_usage() > 0);
	CHECK(int64_t(p["system_count"]) >= 8); // Built-in systems.
	Array rows = p["systems"];
	bool has_builtin = false;
	for (int i = 0; i < rows.size(); i++) {
		if (String(Dictionary(rows[i])["name"]) == "builtin_sprite_sync") {
			has_builtin = true;
			CHECK(String(Dictionary(rows[i])["stage"]) == "render");
		}
	}
	CHECK(has_builtin);

	Array all = ECSWorld::get_all_profiles();
	bool found = false;
	for (int i = 0; i < all.size(); i++) {
		if (String(Dictionary(all[i])["name"]) == "game") {
			found = true;
		}
	}
	CHECK(found);
	CHECK(ECSProfiler::collect().size() == all.size());
}

} // namespace TestECSProfiler
