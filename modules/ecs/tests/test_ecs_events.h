/**************************************************************************/
/*  test_ecs_events.h                                                     */
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
#include "../core/ecs_event.h"
#include "../core/ecs_world.h"
#include "../resources/ecs_event_library.h"
#include "test_ecs_replication.h"

#include "core/config/project_settings.h"
#include "core/object/callable_method_pointer.h"

#include "tests/test_macros.h"

namespace TestECSEvents {

using namespace ecs;

struct Position {
	float x = 0;
};
struct Health {
	int hp = 10;
};

struct Log {
	LocalVector<Event> events;
	static void handler(World &p_world, const Event &p_event, void *p_userdata) {
		((Log *)p_userdata)->events.push_back(p_event);
	}
	int count(EventId p_id) const {
		int n = 0;
		for (const Event &e : events) {
			if (e.id == p_id) {
				n++;
			}
		}
		return n;
	}
};

static SubscriptionId _sub(World &w, Log &log, std::initializer_list<EventId> p_events, const ComponentMask &p_filter = ComponentMask(), Entity p_entity = Entity(), int p_order = 0) {
	SubscriptionDesc d;
	for (EventId e : p_events) {
		d.events.set(e);
	}
	d.func = Log::handler;
	d.userdata = &log;
	d.filter = p_filter;
	d.entity = p_entity;
	d.order = p_order;
	return w.subscribe(d);
}

TEST_CASE("[ECS][Events] Entity lifecycle events: create is deferred, destroy is immediate") {
	World w;
	w.register_component<Position>("Position");
	w.register_component<Health>("Health");
	CHECK_FALSE(w.has_subscribers(EVENT_ENTITY_CREATE));
	Log log;
	SubscriptionId s = _sub(w, log, { EVENT_ENTITY_CREATE, EVENT_ENTITY_DESTROY, EVENT_COMPONENT_ADDED, EVENT_COMPONENT_REMOVED });
	CHECK(s != INVALID_SUBSCRIPTION);
	CHECK(w.has_subscribers(EVENT_ENTITY_CREATE));

	Entity e = w.create_entity();
	w.add<Position>(e, { 1 });
	CHECK(log.events.is_empty()); // Queued until flush (entity fully initialized).
	w.flush_events();
	REQUIRE(log.events.size() == 2);
	CHECK(log.events[0].id == EVENT_ENTITY_CREATE);
	CHECK(log.events[0].entity == e);
	CHECK(log.events[1].id == EVENT_COMPONENT_ADDED);
	CHECK(log.events[1].component == w.component_id<Position>());

	log.events.clear();
	w.remove<Position>(e);
	REQUIRE(log.events.size() == 1); // Immediate, before the data is gone.
	CHECK(log.events[0].id == EVENT_COMPONENT_REMOVED);
	log.events.clear();

	struct Probe {
		static void handler(World &p_world, const Event &p_event, void *p_userdata) {
			// The entity is still alive and readable during the destroy event.
			*(bool *)p_userdata = p_world.is_alive(p_event.entity);
		}
	};
	bool alive_in_handler = false;
	SubscriptionDesc pd;
	pd.events.set(EVENT_ENTITY_DESTROY);
	pd.func = Probe::handler;
	pd.userdata = &alive_in_handler;
	w.subscribe(pd);
	w.destroy_entity(e);
	CHECK(alive_in_handler);
	CHECK(log.count(EVENT_ENTITY_DESTROY) == 1);

	// Queued events for entities destroyed before the flush are dropped.
	log.events.clear();
	Entity f = w.create_entity();
	w.destroy_entity(f);
	w.flush_events();
	CHECK(log.count(EVENT_ENTITY_CREATE) == 0);
	CHECK(log.count(EVENT_ENTITY_DESTROY) == 1);

	CHECK(w.unsubscribe(s));
	CHECK_FALSE(w.unsubscribe(s));
}

TEST_CASE("[ECS][Events] Component filter and entity scoped subscriptions") {
	World w;
	w.register_component<Position>("Position");
	w.register_component<Health>("Health");
	ComponentMask filter;
	filter.set(w.component_id<Health>());
	Log filtered;
	_sub(w, filtered, { EVENT_ENTITY_CREATE, EVENT_COMPONENT_ADDED }, filter);

	ComponentMask both;
	both.set(w.component_id<Position>());
	both.set(w.component_id<Health>());
	Entity hero = w.create_entity(both); // Created straight into its archetype.
	Entity rock = w.create_entity();
	w.add<Position>(rock);
	w.flush_events();
	CHECK(filtered.count(EVENT_ENTITY_CREATE) == 1);
	CHECK(filtered.events[0].entity == hero);
	CHECK(filtered.count(EVENT_COMPONENT_ADDED) == 2); // Position and Health of the hero only.

	Log scoped;
	_sub(w, scoped, { EVENT_COMPONENT_ADDED, EVENT_ENTITY_DESTROY }, ComponentMask(), rock);
	w.add<Health>(hero);
	w.add<Health>(rock);
	w.flush_events();
	CHECK(scoped.count(EVENT_COMPONENT_ADDED) == 1);
	CHECK(scoped.events[0].entity == rock);
	w.destroy_entity(hero);
	w.destroy_entity(rock);
	CHECK(scoped.count(EVENT_ENTITY_DESTROY) == 1);
}

TEST_CASE("[ECS][Events] Update, FixedUpdate and LateUpdate follow the stages") {
	World w;
	Log log;
	_sub(w, log, { EVENT_PRE_UPDATE, EVENT_UPDATE, EVENT_LATE_UPDATE, EVENT_RENDER, EVENT_PRE_FIXED_UPDATE, EVENT_FIXED_UPDATE, EVENT_LATE_FIXED_UPDATE });
	w.update(0.25);
	REQUIRE(log.events.size() == 4);
	CHECK(log.events[0].id == EVENT_PRE_UPDATE);
	CHECK(log.events[1].id == EVENT_UPDATE);
	CHECK(log.events[2].id == EVENT_LATE_UPDATE);
	CHECK(log.events[3].id == EVENT_RENDER);
	CHECK(log.events[1].delta == 0.25);
	log.events.clear();
	w.physics_update(0.5);
	REQUIRE(log.events.size() == 3);
	CHECK(log.events[0].id == EVENT_PRE_FIXED_UPDATE);
	CHECK(log.events[1].id == EVENT_FIXED_UPDATE);
	CHECK(log.events[2].id == EVENT_LATE_FIXED_UPDATE);
	CHECK(log.events[2].delta == 0.5);
}

static LocalVector<int> order_log;
static void _order_a(World &, const Event &, void *) {
	order_log.push_back(1);
}
static void _order_b(World &, const Event &, void *) {
	order_log.push_back(2);
}
static void _stage_system(SystemContext &p_ctx) {
	order_log.push_back(0);
}

TEST_CASE("[ECS][Events] Stage systems run before the stage event; subscriptions are ordered") {
	World w;
	order_log.clear();
	SystemDesc sys;
	sys.name = "staged";
	sys.stage = STAGE_UPDATE;
	sys.func = _stage_system;
	w.add_system(sys);
	SubscriptionDesc b;
	b.events.set(EVENT_UPDATE);
	b.func = _order_b;
	b.order = 10;
	w.subscribe(b);
	SubscriptionDesc a;
	a.events.set(EVENT_UPDATE);
	a.func = _order_a;
	a.order = -5;
	w.subscribe(a);
	w.update(0.016);
	REQUIRE(order_log.size() == 3);
	CHECK(order_log[0] == 0);
	CHECK(order_log[1] == 1);
	CHECK(order_log[2] == 2);
}

static int custom_hits = 0;
static Variant custom_payload;
static void _custom_handler(World &, const Event &p_event, void *) {
	custom_hits++;
	custom_payload = p_event.payload;
}

TEST_CASE("[ECS][Events] Custom events: register, emit, queue") {
	World w;
	custom_hits = 0;
	EventId damage = w.register_event("damage");
	CHECK(damage >= EVENT_CUSTOM_BASE);
	CHECK(w.register_event("damage") == damage);
	CHECK(w.find_event("damage") == damage);
	CHECK(w.get_event_name(damage) == StringName("damage"));
	CHECK(w.get_event_name(EVENT_UPDATE) == StringName("update"));
	CHECK(w.find_event("fixed_update") == EVENT_FIXED_UPDATE);

	SubscriptionDesc d;
	d.events.set(damage);
	d.func = _custom_handler;
	w.subscribe(d);

	Event ev(damage);
	ev.payload = 42;
	w.emit_event(ev);
	CHECK(custom_hits == 1);
	CHECK(int(custom_payload) == 42);

	ev.payload = "later";
	w.queue_event(ev);
	CHECK(custom_hits == 1);
	w.update(0.016); // Flushed at the start of the update.
	CHECK(custom_hits == 2);
	CHECK(String(custom_payload) == "later");

	// Events nobody listens to cost nothing and are ignored.
	EventId silent = w.register_event("silent");
	CHECK_FALSE(w.has_subscribers(silent));
	w.emit_event(Event(silent));
	CHECK(custom_hits == 2);
}

static int system_event_hits = 0;
static double system_event_delta = 0;
static void _system_events(SystemContext &p_ctx, const Event &p_event) {
	system_event_hits++;
	system_event_delta = p_ctx.delta;
	(*(int *)p_ctx.userdata)++;
}

TEST_CASE("[ECS][Events] Systems with an event mask (CryEngine style)") {
	World w;
	w.register_component<Health>("Health");
	system_event_hits = 0;
	int userdata_hits = 0;
	SystemDesc d;
	d.name = "health_watcher";
	d.events.set(EVENT_COMPONENT_ADDED);
	d.events.set(EVENT_FIXED_UPDATE);
	d.event_filter.set(w.component_id<Health>());
	d.event_func = _system_events;
	d.userdata = &userdata_hits;
	SystemId id = w.add_system(d);
	REQUIRE(id != INVALID_SYSTEM);
	CHECK(w.get_subscription_count() == 1);

	Entity e = w.create_entity();
	w.add<Health>(e);
	w.physics_update(0.1); // Flushes the COMPONENT_ADDED event, then emits FIXED_UPDATE.
	CHECK(system_event_hits == 2);
	CHECK(userdata_hits == 2);
	CHECK(system_event_delta == 0.1);

	w.set_system_enabled(id, false);
	w.physics_update(0.1);
	CHECK(system_event_hits == 2);
	w.set_system_enabled(id, true);
	w.physics_update(0.1);
	CHECK(system_event_hits == 3);

	CHECK(w.remove_system(id));
	CHECK(w.get_subscription(0) == nullptr);
	w.physics_update(0.1);
	CHECK(system_event_hits == 3);

	// A system needs at least one entry point.
	ERR_PRINT_OFF;
	CHECK(w.add_system(SystemDesc()) == INVALID_SYSTEM);
	ERR_PRINT_ON;
}

TEST_CASE("[ECS][Events] Network events are delivered through the bus") {
	using TestECSReplication::LoopbackPeer;
	if (!ClassDB::class_exists("LoopbackPeer")) {
		GDREGISTER_CLASS(LoopbackPeer);
	}
	World server;
	World client;
	Log slog;
	Log clog;
	_sub(server, slog, { EVENT_PEER_CONNECTED, EVENT_ENTITY_SPAWNED, EVENT_AUTHORITY_CHANGED });
	_sub(client, clog, { EVENT_PEER_CONNECTED, EVENT_ENTITY_SPAWNED, EVENT_ENTITY_DESPAWNED, EVENT_AUTHORITY_CHANGED });
	Ref<LoopbackPeer> sp;
	sp.instantiate();
	sp->setup(1);
	Ref<LoopbackPeer> cp;
	cp.instantiate();
	cp->setup(2);
	server.get_replicator()->set_peer(sp);
	client.get_replicator()->set_peer(cp);
	LoopbackPeer::link(sp.ptr(), cp.ptr());
	client.physics_update(0.016);
	CHECK(slog.count(EVENT_PEER_CONNECTED) == 1);
	CHECK(slog.events[0].peer == 2);
	CHECK(clog.count(EVENT_PEER_CONNECTED) == 1);

	Entity e = server.create_entity();
	server.add<LocalTransform2D>(e);
	server.get_replicator()->replicate(e);
	server.physics_update(0.016);
	client.physics_update(0.016);
	CHECK(slog.count(EVENT_ENTITY_SPAWNED) == 1);
	CHECK(clog.count(EVENT_ENTITY_SPAWNED) == 1);
	uint32_t net_id = server.get_replicator()->get_net_id(e);
	CHECK(clog.events[clog.events.size() - 1].net_id == net_id);

	server.get_replicator()->set_authority(e, 2);
	server.physics_update(0.016);
	client.physics_update(0.016);
	CHECK(slog.count(EVENT_AUTHORITY_CHANGED) == 1);
	CHECK(clog.count(EVENT_AUTHORITY_CHANGED) == 1);
	CHECK(clog.events[clog.events.size() - 1].peer == 2);

	server.destroy_entity(e);
	server.physics_update(0.016);
	client.physics_update(0.016);
	CHECK(clog.count(EVENT_ENTITY_DESPAWNED) == 1);
}

static int script_hits = 0;
static Dictionary script_event;
static void _script_handler(const Dictionary &p_event) {
	script_hits++;
	script_event = p_event;
}

TEST_CASE("[ECS][Events][Bindings] ECSWorld subscribe / emit with callables") {
	Ref<ECSWorld> world;
	world.instantiate();
	script_hits = 0;
	int64_t hit = world->register_event("hit");
	CHECK(hit >= 0);
	CHECK(world->get_event_id("hit") == hit);
	CHECK(world->get_event_name(ECSWorld::EVENT_UPDATE) == StringName("update"));

	Array events;
	events.push_back("hit");
	events.push_back(ECSWorld::EVENT_COMPONENT_ADDED);
	Array filter;
	filter.push_back("Velocity2D");
	int64_t sub = world->subscribe(events, callable_mp_static(&_script_handler), filter);
	CHECK(sub >= 0);

	int64_t e = world->create_entity();
	world->add_component(e, "LocalTransform2D");
	world->add_component(e, "Velocity2D");
	int64_t other = world->create_entity();
	world->add_component(other, "LocalTransform2D");
	world->flush_events();
	// Filters are evaluated on delivery: both components of `e` are reported
	// (it has Velocity2D by then), nothing for `other`.
	CHECK(script_hits == 2);
	CHECK(String(script_event["name"]) == "component_added");
	CHECK(String(script_event["component"]) == "Velocity2D");
	CHECK(int64_t(script_event["entity"]) == e);

	Dictionary payload;
	payload["amount"] = 7;
	world->emit_event("hit", payload, e); // Immediate; e has Velocity2D so it passes the filter.
	CHECK(script_hits == 3);
	CHECK(String(script_event["name"]) == "hit");
	CHECK(int(Dictionary(script_event["payload"])["amount"]) == 7);
	world->emit_event("hit", payload, 0); // No subject entity: delivered as a global event.
	CHECK(script_hits == 4);

	world->set_subscription_enabled(sub, false);
	world->emit_event("hit", payload, e);
	CHECK(script_hits == 4);
	world->set_subscription_enabled(sub, true);
	world->queue_event(hit, payload, e);
	CHECK(script_hits == 4);
	world->update(0.016);
	CHECK(script_hits == 5);
	CHECK(world->unsubscribe(sub));
	CHECK_FALSE(world->unsubscribe(sub));

	ERR_PRINT_OFF;
	CHECK(world->subscribe("no_such_event", callable_mp_static(&_script_handler)) == -1);
	ERR_PRINT_ON;

	// clear_systems drops subscriptions and script systems but keeps the built-in ones.
	int64_t sub2 = world->subscribe(ECSWorld::EVENT_UPDATE, callable_mp_static(&_script_handler));
	CHECK(sub2 >= 0);
	world->add_system("noop", callable_mp_static(&_script_handler), ECSWorld::STAGE_UPDATE);
	world->clear_systems();
	CHECK(world->get_native()->get_subscription_count() == 0);
	CHECK_FALSE(world->get_system_names().has("noop"));
	CHECK(world->get_system_names().has("builtin_sprite_sync"));
	world->update(0.016);
	CHECK(script_hits == 5);
}

static int heal_hits = 0;
static void _heal_handler(const Dictionary &p_event) {
	heal_hits++;
}

TEST_CASE("[ECS][Events] Project event types are registered in every world") {
	// Names declared in the project (as the ECS Events panel does).
	String err;
	CHECK(ECSEventLibrary::is_valid_event_name("OnPlayerHeal", &err));
	CHECK_FALSE(ECSEventLibrary::is_valid_event_name("", &err));
	CHECK_FALSE(ECSEventLibrary::is_valid_event_name("has space", &err));
	CHECK_FALSE(ECSEventLibrary::is_valid_event_name("update", &err)); // Built-in.
	CHECK(err.contains("built-in"));

	PackedStringArray names;
	names.push_back("OnPlayerHeal");
	names.push_back("OnPlayerHeal"); // Duplicates are dropped.
	names.push_back(" OnLevelLoaded ");
	ProjectSettings::get_singleton()->set_setting(ECSEventLibrary::SETTING_EVENTS, names);
	ECSEventLibrary::reload_project_events();
	CHECK(ECSEventLibrary::has_event("OnPlayerHeal"));
	CHECK(ECSEventLibrary::has_event("OnLevelLoaded"));
	CHECK(ECSEventLibrary::get_event_names_packed().size() == 2);

	heal_hits = 0;
	Ref<ECSWorld> world;
	world.instantiate();
	int64_t heal = world->get_event_id("OnPlayerHeal");
	CHECK(heal >= int64_t(EVENT_CUSTOM_BASE));
	CHECK(world->get_event_names().has("OnPlayerHeal"));
	CHECK(world->get_event_names().has("fixed_update"));
	// A system referencing the event by name, like the generated scripts do.
	CHECK(world->subscribe("OnPlayerHeal", callable_mp_static(&_heal_handler)) >= 0);
	world->emit_event("OnPlayerHeal", 25);
	CHECK(heal_hits == 1);
	world->queue_event(heal, 5);
	world->update(0.016);
	CHECK(heal_hits == 2);

	// Runtime additions (without saving the project) reach new worlds too.
	CHECK(ECSEventLibrary::add_event("OnBossDefeated"));
	CHECK_FALSE(ECSEventLibrary::add_event("OnBossDefeated"));
	Ref<ECSWorld> other;
	other.instantiate();
	CHECK(other->get_event_id("OnBossDefeated") >= 0);
	CHECK(ECSEventLibrary::remove_event("OnBossDefeated"));

	ProjectSettings::get_singleton()->set_setting(ECSEventLibrary::SETTING_EVENTS, PackedStringArray());
	ECSEventLibrary::reload_project_events();
	CHECK(ECSEventLibrary::get_event_names_packed().is_empty());
}

} // namespace TestECSEvents
