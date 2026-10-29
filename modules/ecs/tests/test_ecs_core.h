/**************************************************************************/
/*  test_ecs_core.h                                                       */
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

#include "../core/ecs_bitset.h"
#include "../core/ecs_query.h"
#include "../core/ecs_sparse_set.h"
#include "../core/ecs_world.h"

#include "core/os/os.h"
#include "scene/2d/node_2d.h"

#include "tests/test_macros.h"

namespace TestECSCore {

using namespace ecs;

struct Position {
	float x = 0;
	float y = 0;
};

struct Velocity {
	float dx = 0;
	float dy = 0;
};

struct Health {
	int32_t value = 100;
};

struct Name {
	String name;
};

struct TagA {};
struct Selected {}; // Registered as sparse.

static void _register(World &w) {
	w.register_component<Position>("Position");
	w.register_component<Velocity>("Velocity");
	w.register_component<Health>("Health");
	w.register_component<Name>("Name");
	w.register_component<TagA>("TagA");
	w.register_component<Selected>("Selected", COMPONENT_FLAG_SPARSE);
}

TEST_CASE("[ECS][BitSet] Basic operations") {
	ECSBitSet<256> a;
	CHECK(a.is_empty());
	a.set(3);
	a.set(70);
	a.set(255);
	CHECK(a.test(3));
	CHECK(a.test(70));
	CHECK(a.test(255));
	CHECK_FALSE(a.test(4));
	CHECK(a.count() == 3);
	CHECK(a.find_first() == 3);
	CHECK(a.find_next(4) == 70);
	CHECK(a.find_next(71) == 255);
	CHECK(a.find_next(256) == -1);

	ECSBitSet<256> b;
	b.set(3);
	CHECK(a.contains_all(b));
	CHECK_FALSE(b.contains_all(a));
	CHECK(a.intersects(b));
	b.clear(3);
	b.set(9);
	CHECK_FALSE(a.intersects(b));

	ECSBitSet<256> c = a | b;
	CHECK(c.count() == 4);
	ECSBitSet<256> d = c - a;
	CHECK(d.count() == 1);
	CHECK(d.test(9));
	CHECK((a & b).is_empty());
	CHECK(a == a);
	CHECK(a != b);
	CHECK(a.hash() != b.hash());

	int visited = 0;
	a.for_each([&](uint32_t p_bit) { visited++; });
	CHECK(visited == 3);
}

TEST_CASE("[ECS][SparseSet] Insert, lookup, swap-remove") {
	ECSSparseSet<int> s;
	CHECK(s.is_empty());
	s.insert(5, 50);
	s.insert(100000, 7); // Far away page.
	s.insert(6, 60);
	CHECK(s.size() == 3);
	CHECK(s.has(5));
	CHECK(s.has(100000));
	CHECK_FALSE(s.has(7));
	CHECK(s.get(5) == 50);
	CHECK(s.get(100000) == 7);
	CHECK(*s.getptr(6) == 60);
	CHECK(s.getptr(8) == nullptr);

	// Overwrite.
	s.insert(5, 55);
	CHECK(s.size() == 3);
	CHECK(s.get(5) == 55);

	// Swap remove keeps the set dense and consistent.
	CHECK(s.erase(5));
	CHECK_FALSE(s.erase(5));
	CHECK(s.size() == 2);
	CHECK_FALSE(s.has(5));
	CHECK(s.get(6) == 60);
	CHECK(s.get(100000) == 7);
	for (uint32_t i = 0; i < s.size(); i++) {
		CHECK(s.index_of(s.key_at(i)) == i);
	}

	s.get_or_insert(42) = 1;
	CHECK(s.get(42) == 1);
	s.clear();
	CHECK(s.is_empty());
	CHECK_FALSE(s.has(6));
}

TEST_CASE("[ECS][World] Entity lifecycle and generations") {
	World w;
	Entity a = w.create_entity();
	Entity b = w.create_entity();
	CHECK(a.is_valid());
	CHECK(a != b);
	CHECK(w.is_alive(a));
	CHECK(w.is_alive(b));
	CHECK(w.get_entity_count() == 2);

	CHECK(w.destroy_entity(a));
	CHECK_FALSE(w.is_alive(a));
	CHECK_FALSE(w.destroy_entity(a));
	CHECK(w.get_entity_count() == 1);

	// The slot is recycled with a bumped generation.
	Entity c = w.create_entity();
	CHECK(c.index() == a.index());
	CHECK(c.generation() == a.generation() + 1);
	CHECK(w.is_alive(c));
	CHECK_FALSE(w.is_alive(a));
	CHECK_FALSE(w.is_alive(Entity()));
}

TEST_CASE("[ECS][World] Add, get, remove components and archetype migration") {
	World w;
	_register(w);
	Entity e = w.create_entity();
	Archetype *root = w.get_entity_archetype(e);
	CHECK(root->get_mask().is_empty());

	Position &p = w.add<Position>(e, Position{ 1, 2 });
	CHECK(p.x == 1);
	CHECK(w.has<Position>(e));
	CHECK_FALSE(w.has<Velocity>(e));
	Archetype *a1 = w.get_entity_archetype(e);
	CHECK(a1 != root);
	CHECK(a1->has(w.component_id<Position>()));

	w.add<Velocity>(e, Velocity{ 3, 4 });
	Archetype *a2 = w.get_entity_archetype(e);
	CHECK(a2 != a1);
	CHECK(a2->get_mask().count() == 2);
	// Data survived the migration.
	CHECK(w.get<Position>(e)->x == 1);
	CHECK(w.get<Position>(e)->y == 2);
	CHECK(w.get<Velocity>(e)->dx == 3);

	// Archetype graph edges are cached.
	CHECK(a1->get_add_edge(w.component_id<Velocity>()) == a2);
	CHECK(a2->get_remove_edge(w.component_id<Velocity>()) == a1);

	// Overwrite through add.
	w.add<Position>(e, Position{ 9, 9 });
	CHECK(w.get<Position>(e)->x == 9);
	CHECK(w.get_entity_archetype(e) == a2);

	CHECK(w.remove<Velocity>(e));
	CHECK_FALSE(w.remove<Velocity>(e));
	CHECK(w.get_entity_archetype(e) == a1);
	CHECK(w.get<Position>(e)->x == 9);
	CHECK(w.get<Velocity>(e) == nullptr);

	// Entities in the same archetype share a table; swap-remove keeps rows valid.
	Entity f = w.create_entity();
	w.add<Position>(f, Position{ 5, 5 });
	Entity g = w.create_entity();
	w.add<Position>(g, Position{ 6, 6 });
	CHECK(a1->size() == 3);
	w.destroy_entity(e);
	CHECK(a1->size() == 2);
	CHECK(w.get<Position>(f)->x == 5);
	CHECK(w.get<Position>(g)->x == 6);
	CHECK(a1->get_entity(w.get_record(g)->row) == g);
}

TEST_CASE("[ECS][World] Non trivial components are moved safely") {
	World w;
	_register(w);
	LocalVector<Entity> ents;
	for (int i = 0; i < 100; i++) {
		Entity e = w.create_entity();
		Name n;
		n.name = "entity_" + itos(i);
		w.add<Name>(e, n);
		ents.push_back(e);
	}
	for (int i = 0; i < 100; i += 2) {
		w.add<Position>(ents[i], Position{ float(i), 0 });
	}
	for (int i = 0; i < 100; i++) {
		CHECK(w.get<Name>(ents[i])->name == "entity_" + itos(i));
	}
	for (int i = 0; i < 100; i += 3) {
		w.destroy_entity(ents[i]);
	}
	for (int i = 0; i < 100; i++) {
		if (i % 3 == 0) {
			CHECK_FALSE(w.is_alive(ents[i]));
		} else {
			CHECK(w.get<Name>(ents[i])->name == "entity_" + itos(i));
		}
	}
}

TEST_CASE("[ECS][World] Tags and sparse components") {
	World w;
	_register(w);
	Entity e = w.create_entity();
	w.add<Position>(e);
	Archetype *before = w.get_entity_archetype(e);

	// Tags live in archetypes.
	w.add<TagA>(e);
	CHECK(w.has<TagA>(e));
	CHECK(w.get_entity_archetype(e) != before);
	CHECK(w.get_component_type(w.component_id<TagA>())->is_tag());

	// Sparse components don't migrate the entity.
	Archetype *with_tag = w.get_entity_archetype(e);
	w.add<Selected>(e);
	CHECK(w.has<Selected>(e));
	CHECK(w.get_entity_archetype(e) == with_tag);
	CHECK(w.get_entity_mask(e).test(w.component_id<Selected>()));
	CHECK_FALSE(with_tag->get_mask().test(w.component_id<Selected>()));
	CHECK(w.remove<Selected>(e));
	CHECK_FALSE(w.has<Selected>(e));
	CHECK(w.get_entity_archetype(e) == with_tag);

	// Destroying an entity clears its sparse components.
	w.add<Selected>(e);
	w.destroy_entity(e);
	CHECK(w.get_sparse_storage(w.component_id<Selected>())->size() == 0);
}

TEST_CASE("[ECS][Query] Include / exclude matching and typed iteration") {
	World w;
	_register(w);
	LocalVector<Entity> moving;
	for (int i = 0; i < 10; i++) {
		Entity e = w.create_entity();
		w.add<Position>(e, Position{ float(i), 0 });
		if (i % 2 == 0) {
			w.add<Velocity>(e, Velocity{ 1, 1 });
			moving.push_back(e);
		}
		if (i == 4) {
			w.add<TagA>(e);
		}
	}

	Query q;
	q.init_with<Position, Velocity>(&w);
	CHECK(q.count() == 5);

	ComponentMask exclude;
	exclude.set(w.component_id<TagA>());
	Query q2;
	q2.init_with<Position, Velocity>(&w, exclude);
	CHECK(q2.count() == 4);

	// Typed iteration writes through.
	q.each<Position, const Velocity>([](Entity, Position &p, const Velocity &v) {
		p.x += v.dx * 10;
	});
	for (Entity e : moving) {
		CHECK(w.get<Position>(e)->x >= 10);
	}

	// Queries pick up archetypes created after them.
	Entity late = w.create_entity();
	w.add<Velocity>(late);
	w.add<Position>(late);
	w.add<Health>(late);
	CHECK(q.count() == 6);
	CHECK(q.matches(late));
	CHECK(q.matches(moving[0]));

	LocalVector<Entity> list;
	q.get_entities(list);
	CHECK(list.size() == 6);
}

TEST_CASE("[ECS][Query] Sparse terms are evaluated per entity") {
	World w;
	_register(w);
	Entity a = w.create_entity();
	Entity b = w.create_entity();
	w.add<Position>(a);
	w.add<Position>(b);
	w.add<Selected>(b);

	ComponentMask inc;
	inc.set(w.component_id<Position>());
	inc.set(w.component_id<Selected>());
	Query selected(&w, inc);
	CHECK(selected.has_sparse_terms());
	CHECK(selected.count() == 1);
	CHECK(selected.matches(b));
	CHECK_FALSE(selected.matches(a));

	ComponentMask inc2;
	inc2.set(w.component_id<Position>());
	ComponentMask exc2;
	exc2.set(w.component_id<Selected>());
	Query unselected(&w, inc2, exc2);
	CHECK(unselected.count() == 1);
	CHECK(unselected.matches(a));

	w.remove<Selected>(b);
	CHECK(selected.count() == 0);
	CHECK(unselected.count() == 2);
}

TEST_CASE("[ECS][Query] Parallel iteration covers every row exactly once") {
	World w;
	_register(w);
	const int N = 5000;
	for (int i = 0; i < N; i++) {
		Entity e = w.create_entity();
		w.add<Position>(e, Position{ 0, 0 });
		if (i % 3 == 0) {
			w.add<Health>(e);
		}
	}
	Query q;
	q.init_with<Position>(&w);
	struct Ctx {
		ComponentId pos;
	} ctx{ w.component_id<Position>() };
	q.for_each_parallel([](Archetype &a, uint32_t p_begin, uint32_t p_end, void *p_ud) {
		Ctx *c = (Ctx *)p_ud;
		Position *p = a.column_ptr<Position>(uint32_t(a.get_column_index(c->pos)));
		for (uint32_t r = p_begin; r < p_end; r++) {
			p[r].x += 1;
		}
	},
			&ctx, 256);
	int sum = 0;
	q.each<const Position>([&](Entity, const Position &p) { sum += int(p.x); });
	CHECK(sum == N);
}

TEST_CASE("[ECS][World] Change ticks") {
	World w;
	_register(w);
	Entity e = w.create_entity();
	uint32_t t0 = w.get_tick();
	w.add<Position>(e);
	CHECK(w.get_component_change_tick(e, w.component_id<Position>()) == t0);
	w.advance_tick();
	w.advance_tick();
	CHECK(w.get_component_change_tick(e, w.component_id<Position>()) == t0);
	w.get<Position>(e); // Const access does not mark.
	CHECK(w.get_component_change_tick(e, w.component_id<Position>()) == t0);
	w.get_mut<Position>(e);
	CHECK(w.get_component_change_tick(e, w.component_id<Position>()) == w.get_tick());

	// Migration keeps the tick.
	w.advance_tick();
	w.add<Velocity>(e);
	CHECK(w.get_component_change_tick(e, w.component_id<Position>()) == w.get_tick() - 1);
	CHECK(w.get_component_change_tick(e, w.component_id<Velocity>()) == w.get_tick());
}

static int system_a_runs = 0;
static int system_b_runs = 0;
static LocalVector<int> run_order;

static void _sys_a(SystemContext &p_ctx) {
	system_a_runs++;
	run_order.push_back(1);
}
static void _sys_b(SystemContext &p_ctx) {
	system_b_runs++;
	run_order.push_back(2);
}
static void _sys_spawner(SystemContext &p_ctx) {
	// Structural change from inside a system goes through the command buffer.
	Entity e = p_ctx.world->reserve_entity();
	p_ctx.commands->add_component(e, *p_ctx.world->get_component_type(p_ctx.world->component_id<Position>()), nullptr);
}

TEST_CASE("[ECS][Systems] Ordering, stages, enable/disable and deferred commands") {
	World w;
	_register(w);
	system_a_runs = system_b_runs = 0;
	run_order.clear();

	SystemDesc b;
	b.name = "b";
	b.func = _sys_b;
	b.order = 10;
	SystemDesc a;
	a.name = "a";
	a.func = _sys_a;
	a.order = 0;
	SystemDesc phys;
	phys.name = "spawner";
	phys.func = _sys_spawner;
	phys.stage = STAGE_PHYSICS;

	SystemId sb = w.add_system(b);
	SystemId sa = w.add_system(a);
	w.add_system(phys);
	CHECK(w.find_system("a") == sa);

	w.update(0.016);
	CHECK(system_a_runs == 1);
	CHECK(system_b_runs == 1);
	REQUIRE(run_order.size() == 2);
	CHECK(run_order[0] == 1);
	CHECK(run_order[1] == 2);
	CHECK(w.get_entity_count() == 0); // Physics stage did not run.

	w.physics_update(0.016);
	CHECK(w.get_entity_count() == 1);
	Query q;
	q.init_with<Position>(&w);
	CHECK(q.count() == 1);

	w.set_system_enabled(sb, false);
	w.update(0.016);
	CHECK(system_a_runs == 2);
	CHECK(system_b_runs == 1);

	CHECK(w.remove_system(sa));
	w.update(0.016);
	CHECK(system_a_runs == 2);
}

static void _par_sys(SystemContext &p_ctx) {
	// Each parallel system touches its own component so there is no conflict.
	ComponentId *id = (ComponentId *)p_ctx.userdata;
	Query q;
	ComponentMask m;
	m.set(*id);
	q.init(p_ctx.world, m);
	q.for_each([&](Entity, Archetype &a, uint32_t r) {
		int32_t c = a.get_column_index(*id);
		float *f = (float *)a.get(uint32_t(c), r);
		f[0] += 1;
	});
}

TEST_CASE("[ECS][Systems] Parallel batches respect declared access") {
	World w;
	_register(w);
	ComponentId pid = w.component_id<Position>();
	ComponentId vid = w.component_id<Velocity>();
	for (int i = 0; i < 1000; i++) {
		Entity e = w.create_entity();
		w.add<Position>(e);
		w.add<Velocity>(e);
	}
	SystemDesc s1;
	s1.name = "p";
	s1.func = _par_sys;
	s1.userdata = &pid;
	s1.thread_safe = true;
	s1.writes.set(pid);
	SystemDesc s2 = s1;
	s2.name = "v";
	s2.userdata = &vid;
	s2.writes.clear_all();
	s2.writes.set(vid);
	w.add_system(s1);
	w.add_system(s2);
	for (int i = 0; i < 5; i++) {
		w.update(0.016);
	}
	Query q;
	q.init_with<Position, Velocity>(&w);
	q.each<const Position, const Velocity>([](Entity, const Position &p, const Velocity &v) {
		CHECK(p.x == 5);
		CHECK(v.dx == 5);
	});
}

static int hook_adds = 0;
static int hook_removes = 0;
static void _on_add(World &, Entity, void *) {
	hook_adds++;
}
static void _on_remove(World &, Entity, void *) {
	hook_removes++;
}

TEST_CASE("[ECS][World] Component hooks and observers") {
	World w;
	_register(w);
	hook_adds = hook_removes = 0;
	ComponentType *t = w.get_component_type_mut(w.component_id<Health>());
	t->on_add = _on_add;
	t->on_remove = _on_remove;

	int destroyed = 0;
	WorldObserver obs;
	obs.userdata = &destroyed;
	obs.on_entity_destroyed = [](World &, Entity, void *p_ud) { (*(int *)p_ud)++; };
	w.add_observer(obs);

	Entity e = w.create_entity();
	w.add<Health>(e);
	CHECK(hook_adds == 1);
	w.add<Health>(e); // Overwrite does not re-add.
	CHECK(hook_adds == 1);
	w.remove<Health>(e);
	CHECK(hook_removes == 1);
	w.add<Health>(e);
	w.destroy_entity(e);
	CHECK(hook_removes == 2);
	CHECK(destroyed == 1);
	w.remove_observer(&destroyed);
	Entity f = w.create_entity();
	w.destroy_entity(f);
	CHECK(destroyed == 1);
}

TEST_CASE("[ECS][World] Script-defined components") {
	World w;
	Dictionary fields;
	fields["hp"] = Variant::INT;
	fields["speed"] = Variant::FLOAT;
	fields["pos"] = Variant::VECTOR3;
	fields["label"] = Variant::STRING;
	fields["target"] = "entity";
	fields["extra"] = Variant::NIL;
	String err;
	ComponentId id = w.register_script_component("Stats", fields, 0, &err);
	REQUIRE_MESSAGE(id != INVALID_COMPONENT, err);
	const ComponentType *t = w.get_component_type(id);
	CHECK(t->fields.size() == 6);
	CHECK_FALSE(t->trivial);

	Entity e = w.create_entity();
	Entity other = w.create_entity();
	void *c = w.add_component(e, id);
	REQUIRE(c);
	Dictionary values;
	values["hp"] = 42;
	values["speed"] = 1.5;
	values["pos"] = Vector3(1, 2, 3);
	values["label"] = "hello";
	values["target"] = int64_t(other.id);
	Array extra;
	extra.push_back(1);
	values["extra"] = extra;
	t->from_dictionary(c, values);

	Dictionary out = t->to_dictionary(w.get_component(e, id));
	CHECK(int(out["hp"]) == 42);
	CHECK(double(out["speed"]) == 1.5);
	CHECK(Vector3(out["pos"]) == Vector3(1, 2, 3));
	CHECK(String(out["label"]) == "hello");
	CHECK(uint64_t(int64_t(out["target"])) == other.id);
	CHECK(Array(out["extra"]).size() == 1);

	// Migration keeps non trivial fields intact.
	w.register_component<Position>("Position");
	w.add<Position>(e);
	out = t->to_dictionary(w.get_component(e, id));
	CHECK(String(out["label"]) == "hello");

	// Duplicate registration fails, bad field type fails.
	CHECK(w.register_script_component("Stats", fields, 0, &err) == INVALID_COMPONENT);
	Dictionary bad;
	bad["x"] = "not_a_type";
	CHECK(w.register_script_component("Bad", bad, 0, &err) == INVALID_COMPONENT);
}

TEST_CASE("[ECS][World] Bulk creation in a target archetype") {
	World w;
	_register(w);
	ComponentMask m;
	m.set(w.component_id<Position>());
	m.set(w.component_id<Velocity>());
	for (int i = 0; i < 100; i++) {
		Entity e = w.create_entity(m);
		CHECK(w.has<Position>(e));
		CHECK(w.has<Velocity>(e));
	}
	// Only root + one archetype.
	CHECK(w.get_archetype_count() == 2);
	w.clear_entities();
	CHECK(w.get_entity_count() == 0);
}

TEST_CASE("[SceneTree][ECS][Benchmark] 100k entities: ECS archetypes vs Node2D" * doctest::skip(true)) {
	const int N = 100000;
	const int ITERATIONS = 100;
	constexpr float DELTA = 1.0f / 60.0f;

	// ECS: one archetype, SoA columns, typed iteration.
	World w;
	w.register_component<Position>("Position");
	w.register_component<Velocity>("Velocity");
	ComponentMask m;
	m.set(w.component_id<Position>());
	m.set(w.component_id<Velocity>());
	uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	for (int i = 0; i < N; i++) {
		Entity e = w.create_entity(m);
		Velocity *v = w.get_mut<Velocity>(e);
		v->dx = 1;
		v->dy = 2;
	}
	uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	Query q;
	q.init_with<Position, Velocity>(&w);
	for (int it = 0; it < ITERATIONS; it++) {
		q.each<Position, const Velocity>([](Entity, Position &p, const Velocity &v) {
			p.x += v.dx * DELTA;
			p.y += v.dy * DELTA;
		});
	}
	uint64_t t2 = OS::get_singleton()->get_ticks_usec();
	CHECK(q.count() == N);

	// Nodes: the classic workflow, one Node2D per object with the velocity
	// stored next to it, updated through the property setter.
	struct Mover {
		Node2D *node;
		Vector2 velocity;
	};
	LocalVector<Mover> movers;
	movers.resize(N);
	uint64_t t3 = OS::get_singleton()->get_ticks_usec();
	for (int i = 0; i < N; i++) {
		movers[i].node = memnew(Node2D);
		movers[i].velocity = Vector2(1, 2);
	}
	uint64_t t4 = OS::get_singleton()->get_ticks_usec();
	for (int it = 0; it < ITERATIONS; it++) {
		for (int i = 0; i < N; i++) {
			Node2D *n = movers[i].node;
			n->set_position(n->get_position() + movers[i].velocity * DELTA);
		}
	}
	uint64_t t5 = OS::get_singleton()->get_ticks_usec();
	for (int i = 0; i < N; i++) {
		memdelete(movers[i].node);
	}
	uint64_t t6 = OS::get_singleton()->get_ticks_usec();

	print_line(vformat("ECS   : create %d entities: %8d us | %d iterations: %8d us (%.1f us / iteration) | per entity-iteration: %.2f ns",
			N, int64_t(t1 - t0), ITERATIONS, int64_t(t2 - t1), double(t2 - t1) / ITERATIONS, double(t2 - t1) * 1000.0 / (double(ITERATIONS) * N)));
	print_line(vformat("Node2D: create %d nodes   : %8d us | %d iterations: %8d us (%.1f us / iteration) | per node-iteration:   %.2f ns | free: %d us",
			N, int64_t(t4 - t3), ITERATIONS, int64_t(t5 - t4), double(t5 - t4) / ITERATIONS, double(t5 - t4) * 1000.0 / (double(ITERATIONS) * N), int64_t(t6 - t5)));
}

} // namespace TestECSCore
