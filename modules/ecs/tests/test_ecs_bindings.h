/**************************************************************************/
/*  test_ecs_bindings.h                                                   */
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

#include "core/object/callable_method_pointer.h"

#include "tests/test_macros.h"

namespace TestECSBindings {

static int script_system_calls = 0;
static double script_system_delta = 0;

static void _script_system(double p_delta) {
	script_system_calls++;
	script_system_delta = p_delta;
}

static Dictionary _fields(std::initializer_list<std::pair<const char *, int>> p_fields) {
	Dictionary d;
	for (const auto &f : p_fields) {
		d[StringName(f.first)] = f.second;
	}
	return d;
}

TEST_CASE("[ECS][Bindings] ECSWorld: components, entities and fields") {
	Ref<ECSWorld> world;
	world.instantiate();

	int64_t health = world->register_component("Health", _fields({ { "value", Variant::INT }, { "max", Variant::INT } }));
	CHECK(health >= 0);
	CHECK(world->get_component_id("Health") == health);
	CHECK(world->get_component_id("Nope") == -1);
	CHECK(world->get_component_names().has("LocalTransform2D"));

	ERR_PRINT_OFF;
	CHECK(world->register_component("Health", Dictionary()) == -1); // Duplicate.
	ERR_PRINT_ON;

	Dictionary comps;
	Dictionary hv;
	hv["value"] = 7;
	hv["max"] = 10;
	comps["Health"] = hv;
	Dictionary tv;
	tv["transform"] = Transform2D(0, Vector2(1, 2));
	comps["LocalTransform2D"] = tv;
	comps["Velocity2D"] = Variant();
	int64_t e = world->create_entity(comps);
	CHECK(e != 0);
	CHECK(world->is_alive(e));
	CHECK(world->get_entity_count() == 1);
	CHECK(world->has_component(e, "Health"));
	CHECK(world->has_component(e, health));
	CHECK(world->has_component(e, "Velocity2D"));
	CHECK(int(world->get_field(e, "Health", "value")) == 7);
	CHECK(Transform2D(world->get_field(e, "LocalTransform2D", "transform")).get_origin() == Vector2(1, 2));
	CHECK(world->get_entity_components(e).size() == 3);

	CHECK(world->set_field(e, "Health", "value", 3));
	CHECK(int(world->get_component(e, "Health")["value"]) == 3);
	Dictionary upd;
	upd["max"] = 99;
	CHECK(world->set_component(e, "Health", upd));
	CHECK(int(world->get_field(e, "Health", "max")) == 99);
	CHECK(int(world->get_field(e, "Health", "value")) == 3);

	CHECK(world->remove_component(e, "Velocity2D"));
	CHECK_FALSE(world->has_component(e, "Velocity2D"));
	CHECK(world->destroy_entity(e));
	CHECK_FALSE(world->is_alive(e));
	CHECK(world->get_entity_count() == 0);

	Dictionary info = world->get_component_info("Health");
	CHECK(int(info["size"]) == 16);
	CHECK(Dictionary(info["fields"]).size() == 2);
}

TEST_CASE("[ECS][Bindings] ECSQuery: bulk field arrays and deferred commands") {
	Ref<ECSWorld> world;
	world.instantiate();
	world->register_component("Score", _fields({ { "points", Variant::FLOAT }, { "tag", Variant::STRING } }));
	world->register_component("Dead", Dictionary(), ECSWorld::COMPONENT_FLAG_SPARSE);

	for (int i = 0; i < 10; i++) {
		Dictionary comps;
		Dictionary sv;
		sv["points"] = double(i);
		comps["Score"] = sv;
		comps["LocalTransform2D"] = Variant();
		int64_t e = world->create_entity(comps);
		if (i % 2) {
			world->add_component(e, "Dead");
		}
	}
	Array inc;
	inc.push_back("Score");
	Array exc;
	exc.push_back("Dead");
	Ref<ECSQuery> alive = world->query(inc, exc);
	REQUIRE(alive.is_valid());
	CHECK(alive->count() == 5);
	Ref<ECSQuery> all = world->query(inc);
	CHECK(all->count() == 10);

	Variant points = alive->get_field_array("Score", "points");
	REQUIRE(points.get_type() == Variant::PACKED_FLOAT64_ARRAY);
	PackedFloat64Array pf = points;
	CHECK(pf.size() == 5);
	double sum = 0;
	for (double v : pf) {
		sum += v;
	}
	CHECK(sum == 0 + 2 + 4 + 6 + 8);

	// Bulk write, then read back through the single-entity API.
	PackedFloat64Array doubled;
	for (double v : pf) {
		doubled.push_back(v * 2);
	}
	CHECK(alive->set_field_array("Score", "points", doubled) == 5);
	PackedInt64Array ents = alive->get_entities();
	CHECK(ents.size() == 5);
	double sum2 = 0;
	for (int64_t e : ents) {
		sum2 += double(world->get_field(e, "Score", "points"));
	}
	CHECK(sum2 == sum * 2);

	Variant tags = alive->get_field_array("Score", "tag");
	CHECK(tags.get_type() == Variant::ARRAY);
	CHECK(Array(tags).size() == 5);

	// Deferred commands.
	for (int64_t e : ents) {
		world->defer_add_component(e, "Dead");
	}
	CHECK(alive->count() == 5);
	world->flush_commands();
	CHECK(alive->count() == 0);
	CHECK(all->count() == 10);
	world->defer_destroy_entity(ents[0]);
	world->flush_commands();
	CHECK(all->count() == 9);
	CHECK_FALSE(world->is_alive(ents[0]));

	Array archetypes = world->get_archetype_info();
	CHECK(archetypes.size() == world->get_archetype_count());
}

TEST_CASE("[ECS][Bindings] Script systems run on their stage with the delta") {
	Ref<ECSWorld> world;
	world.instantiate();
	script_system_calls = 0;
	int64_t id = world->add_system("my_system", callable_mp_static(&_script_system), ECSWorld::STAGE_PHYSICS, 5);
	CHECK(id >= 0);
	CHECK(world->get_system_names().has("my_system"));
	CHECK(world->get_system_names().has("builtin_sprite_sync"));
	world->update(0.5);
	CHECK(script_system_calls == 0);
	world->physics_update(0.25);
	CHECK(script_system_calls == 1);
	CHECK(script_system_delta == 0.25);
	world->set_system_enabled("my_system", false);
	world->physics_update(0.25);
	CHECK(script_system_calls == 1);
	CHECK(world->remove_system("my_system"));
	CHECK_FALSE(world->get_system_names().has("my_system"));
	CHECK(world->get_tick() > 1);
}

} // namespace TestECSBindings
