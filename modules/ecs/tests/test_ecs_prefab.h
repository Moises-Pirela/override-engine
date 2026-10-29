/**************************************************************************/
/*  test_ecs_prefab.h                                                     */
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
#include "../components/ecs_builtin_components.h"
#include "../resources/ecs_prefab.h"
#include "test_ecs_replication.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestECSPrefab {

static Ref<ECSComponentSchema> _health_schema() {
	Ref<ECSComponentSchema> schema;
	schema.instantiate();
	schema->set_component_name("Health");
	schema->add_field("value", Variant::INT);
	schema->add_field("max", Variant::INT);
	schema->add_field("label", Variant::STRING);
	schema->set_flags(ECSWorld::COMPONENT_FLAG_REPLICATED);
	return schema;
}

static Ref<ECSPrefab> _tank_prefab() {
	Ref<ECSPrefab> tank;
	tank.instantiate();
	Dictionary xf;
	xf["transform"] = Transform2D(0, Vector2(10, 20));
	tank->add_component("LocalTransform2D", xf);
	Dictionary vel;
	vel["linear"] = Vector2(5, 0);
	tank->add_component("Velocity2D", vel);
	Dictionary health;
	health["value"] = 80;
	health["max"] = 100;
	health["label"] = "tank";
	tank->add_component("Health", health);
	Dictionary sprite;
	sprite["size"] = Vector2(32, 32);
	sprite["modulate"] = Color(1, 0, 0);
	tank->add_component("Sprite2D", sprite);

	Ref<ECSPrefab> turret;
	turret.instantiate();
	Dictionary txf;
	txf["transform"] = Transform2D(0, Vector2(0, -8));
	turret->add_component("LocalTransform2D", txf);
	turret->add_component("GlobalTransform2D");
	Dictionary tsprite;
	tsprite["size"] = Vector2(8, 16);
	turret->add_component("Sprite2D", tsprite);
	tank->add_child(turret);
	return tank;
}

TEST_CASE("[ECS][Prefab] Schema describes a component and registers in a world") {
	Ref<ECSComponentSchema> schema = _health_schema();
	CHECK(schema->get_field_names().size() == 3);
	CHECK(schema->get_field_type("value") == Variant::INT);
	CHECK(schema->get_field_type("nope") == -1);
	CHECK(ECSComponentSchema::type_from_name("Vector3") == Variant::VECTOR3);
	CHECK(ECSComponentSchema::type_from_name("entity") == ecs::FIELD_TYPE_ENTITY);
	CHECK(ECSComponentSchema::name_from_type(Variant::COLOR) == "Color");
	CHECK(ECSComponentSchema::get_supported_type_names().has("Transform3D"));

	Ref<ECSWorld> world;
	world.instantiate();
	int64_t id = world->register_schema(schema);
	CHECK(id >= 0);
	CHECK(world->register_schema(schema) == id); // Idempotent.
	Dictionary info = world->get_component_info("Health");
	CHECK(bool(info["replicated"]));
	CHECK(Dictionary(info["fields"]).size() == 3);
}

TEST_CASE("[ECS][Prefab] Dynamic inspector properties") {
	Ref<ECSPrefab> prefab;
	prefab.instantiate();
	Dictionary v;
	v["linear"] = Vector2(1, 2);
	prefab->add_component("Velocity2D", v);
	prefab->add_component("Custom");

	List<PropertyInfo> props;
	prefab->get_property_list(&props);
	bool has_group = false;
	bool has_linear = false;
	bool has_angular = false;
	for (const PropertyInfo &pi : props) {
		if (pi.usage == PROPERTY_USAGE_GROUP && pi.name == "Velocity2D") {
			has_group = true;
			CHECK(pi.hint_string == "components/Velocity2D/");
		}
		if (pi.name == "components/Velocity2D/linear") {
			has_linear = true;
			CHECK(pi.type == Variant::VECTOR2);
		}
		if (pi.name == "components/Velocity2D/angular") {
			has_angular = true; // Reflected from the built-in type even if not set.
			CHECK(pi.type == Variant::FLOAT);
		}
	}
	CHECK(has_group);
	CHECK(has_linear);
	CHECK(has_angular);

	CHECK(Vector2(prefab->get("components/Velocity2D/linear")) == Vector2(1, 2));
	CHECK(double(prefab->get("components/Velocity2D/angular")) == 0.0); // Default for unset fields.
	prefab->set("components/Velocity2D/angular", 3.5);
	CHECK(double(prefab->get_component_value("Velocity2D", "angular")) == 3.5);
	CHECK(prefab->property_can_revert("components/Velocity2D/angular"));
	CHECK_FALSE(prefab->property_can_revert("components/Velocity2D/nothing"));
	Variant revert = prefab->property_get_revert("components/Velocity2D/angular");
	CHECK(revert.get_type() == Variant::FLOAT);

	// Unknown components accept arbitrary values.
	prefab->set_component_value("Custom", "anything", "text");
	CHECK(String(prefab->get("components/Custom/anything")) == "text");

	prefab->remove_component("Custom");
	CHECK_FALSE(prefab->has_component("Custom"));
	CHECK(prefab->get_component_names().size() == 1);
}

TEST_CASE("[ECS][Prefab] Instantiate with hierarchy, apply and round trip") {
	Ref<ECSWorld> world;
	world.instantiate();
	world->register_schema(_health_schema());
	Ref<ECSPrefab> tank = _tank_prefab();

	int64_t e = world->instantiate_prefab(tank);
	REQUIRE(world->is_alive(e));
	CHECK(world->has_component(e, "LocalTransform2D"));
	CHECK(int(world->get_field(e, "Health", "value")) == 80);
	CHECK(String(world->get_field(e, "Health", "label")) == "tank");
	CHECK(Color(world->get_field(e, "Sprite2D", "modulate")) == Color(1, 0, 0));
	CHECK_FALSE(world->has_component(e, "Prefab")); // Not a saved resource: no path.
	CHECK_FALSE(world->has_component(e, "Parent"));

	PackedInt64Array children = world->get_children(e);
	REQUIRE(children.size() == 1);
	int64_t turret = children[0];
	CHECK(world->has_component(turret, "Parent"));
	CHECK(int64_t(world->get_field(turret, "Parent", "parent")) == e);
	CHECK(Vector2(world->get_field(turret, "Sprite2D", "size")) == Vector2(8, 16));

	// Transform propagation works on the instantiated hierarchy.
	world->update(0.016);
	CHECK(Transform2D(world->get_field(turret, "GlobalTransform2D", "transform")).get_origin().is_equal_approx(Vector2(10, 12)));

	// apply_prefab without overwrite only fills missing components.
	int64_t other = world->create_entity();
	Dictionary hv;
	hv["value"] = 5;
	world->add_component(other, "Health", hv);
	CHECK(world->apply_prefab(other, tank, false));
	CHECK(int(world->get_field(other, "Health", "value")) == 5);
	CHECK(world->has_component(other, "Velocity2D"));
	CHECK(world->apply_prefab(other, tank, true));
	CHECK(int(world->get_field(other, "Health", "value")) == 80);

	// Entity -> prefab round trip.
	Ref<ECSPrefab> copy = world->create_prefab(e, true);
	REQUIRE(copy.is_valid());
	CHECK(copy->has_component("Health"));
	CHECK(copy->has_component("Sprite2D"));
	CHECK_FALSE(copy->has_component("Parent"));
	CHECK(int(copy->get_component_value("Health", "value")) == 80);
	CHECK_FALSE(copy->get_component("Sprite2D").has("canvas_item")); // RIDs are not captured.
	REQUIRE(copy->get_child_count() == 1);
	CHECK(Vector2(copy->get_child(0)->get_component_value("Sprite2D", "size")) == Vector2(8, 16));
	CHECK_FALSE(copy->get_child(0)->has_component("Parent"));

	// Sub prefab lookup.
	CHECK(tank->find_sub_prefab("0") == tank->get_child(0));
	CHECK(tank->find_sub_prefab("").ptr() == tank.ptr());
	CHECK(tank->find_sub_prefab("3").is_null());
	CHECK(ECSPrefab::make_sub_path("res://a.tres", "0/1") == "res://a.tres#0/1");
}

TEST_CASE("[ECS][Prefab] Save, load and instantiate from disk with prefab references") {
	Ref<ECSPrefab> tank = _tank_prefab();
	String path = TestUtils::get_temp_path("tank_prefab.tres");
	REQUIRE(ResourceSaver::save(tank, path) == OK);

	Ref<ECSPrefab> loaded = ResourceLoader::load(path, "ECSPrefab", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());
	CHECK(loaded->get_component_names().size() == 4);
	CHECK(int(loaded->get_component_value("Health", "value")) == 80);
	CHECK(Transform2D(loaded->get_component_value("LocalTransform2D", "transform")).get_origin() == Vector2(10, 20));
	REQUIRE(loaded->get_child_count() == 1);
	CHECK(loaded->get_child(0)->has_component("Sprite2D"));

	Ref<ECSWorld> world;
	world.instantiate();
	world->register_schema(_health_schema());
	int64_t e = world->instantiate_prefab(loaded);
	REQUIRE(world->is_alive(e));
	CHECK(String(world->get_field(e, "Prefab", "path")) == path);
	int64_t turret = world->get_children(e)[0];
	CHECK(String(world->get_field(turret, "Prefab", "path")) == path + "#0");

	Ref<ECSPrefab> sub = ECSPrefab::load_from_path(path + "#0");
	REQUIRE(sub.is_valid());
	CHECK(sub->has_component("Sprite2D"));
	CHECK(ECSPrefab::load_from_path(path + "#7").is_null());
}

TEST_CASE("[ECS][Prefab] Component library knows built-ins and schemas") {
	List<StringName> names;
	ECSComponentLibrary::get_component_names(&names);
	CHECK(names.find("Sprite2D") != nullptr);
	CHECK(names.find("Prefab") != nullptr);
	const ECSComponentLibrary::Info *sprite = ECSComponentLibrary::get_info("Sprite2D");
	REQUIRE(sprite);
	CHECK(sprite->builtin);
	bool rid_internal = false;
	for (const ECSComponentLibrary::FieldInfo &f : sprite->fields) {
		if (f.name == StringName("canvas_item")) {
			rid_internal = f.internal;
		}
	}
	CHECK(rid_internal);
	CHECK(ECSComponentLibrary::get_field_type("Sprite2D", "modulate") == Variant::COLOR);

	Ref<ECSComponentSchema> schema;
	schema.instantiate();
	schema->set_component_name("LibTest");
	schema->add_field("pos", Variant::VECTOR3);
	ECSComponentLibrary::add_schema(schema);
	CHECK(ECSComponentLibrary::get_field_type("LibTest", "pos") == Variant::VECTOR3);
	names.clear();
	ECSComponentLibrary::get_component_names(&names);
	CHECK(names.find("LibTest") != nullptr);

	// New worlds pick up library schemas.
	Ref<ECSWorld> world;
	world.instantiate();
	CHECK(world->get_component_id("LibTest") >= 0);
	ECSComponentLibrary::remove_schema("LibTest");
	CHECK(ECSComponentLibrary::get_info("LibTest") == nullptr);
	CHECK(ECSComponentLibrary::get_info("Sprite2D") != nullptr); // Built-ins can't be removed.
}

TEST_CASE("[ECS][Prefab] Local-only prefab components are re-created on clients") {
	using TestECSReplication::LoopbackPeer;
	if (!ClassDB::class_exists("LoopbackPeer")) {
		GDREGISTER_CLASS(LoopbackPeer);
	}
	Ref<ECSPrefab> tank = _tank_prefab();
	String path = TestUtils::get_temp_path("tank_prefab_net.tres");
	REQUIRE(ResourceSaver::save(tank, path) == OK);
	Ref<ECSPrefab> loaded = ResourceLoader::load(path, "ECSPrefab", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());

	Ref<ECSWorld> server;
	server.instantiate();
	Ref<ECSWorld> client;
	client.instantiate();
	server->register_schema(_health_schema());
	client->register_schema(_health_schema());
	Ref<LoopbackPeer> sp;
	sp.instantiate();
	sp->setup(1);
	Ref<LoopbackPeer> cp;
	cp.instantiate();
	cp->setup(2);
	server->set_multiplayer_peer(sp);
	client->set_multiplayer_peer(cp);
	LoopbackPeer::link(sp.ptr(), cp.ptr());
	client->physics_update(0.016);

	int64_t e = server->instantiate_prefab(loaded);
	server->replicate(e, 1, true);
	server->physics_update(0.016);
	client->physics_update(0.016);

	Array inc;
	inc.push_back("Prefab");
	Ref<ECSQuery> q = client->query(inc);
	CHECK(q->count() == 2);
	int64_t root = client->get_entity_by_network_id(server->get_network_id(e));
	REQUIRE(client->is_alive(root));
	CHECK(int(client->get_field(root, "Health", "value")) == 80); // Replicated component.
	REQUIRE(client->has_component(root, "Sprite2D")); // Local-only component from the prefab.
	CHECK(Vector2(client->get_field(root, "Sprite2D", "size")) == Vector2(32, 32));
	PackedInt64Array children = client->get_children(root);
	REQUIRE(children.size() == 1); // Parent references were remapped.
	CHECK(Vector2(client->get_field(children[0], "Sprite2D", "size")) == Vector2(8, 16));
	CHECK(String(client->get_field(children[0], "Prefab", "path")) == path + "#0");
}

} // namespace TestECSPrefab
