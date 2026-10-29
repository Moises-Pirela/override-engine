/**************************************************************************/
/*  test_ecs_builtin.h                                                    */
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

#include "../components/ecs_builtin_components.h"
#include "../core/ecs_query.h"
#include "../core/ecs_world.h"

#include "servers/physics_2d/physics_server_2d.h"
#include "servers/rendering/rendering_server.h"

#include "tests/test_macros.h"

namespace TestECSBuiltin {

using namespace ecs;

TEST_CASE("[ECS][Builtin] Built-in components are registered with reflection") {
	World w;
	CHECK(w.find_component("LocalTransform2D") != INVALID_COMPONENT);
	CHECK(w.find_component("Sprite2D") != INVALID_COMPONENT);
	CHECK(w.find_component("NetworkIdentity") == w.component_id<NetworkIdentity>());
	const ComponentType *t = w.get_component_type(w.component_id<ecs::Sprite2D>());
	CHECK(t->find_field("modulate") >= 0);
	CHECK(t->find_field("z_index") >= 0);
	CHECK(w.get_component_type(w.component_id<Disabled>())->is_sparse());
	CHECK(w.get_component_type(w.component_id<Disabled>())->is_tag());
	CHECK(w.get_component_type(w.component_id<LocalTransform2D>())->is_replicated());

	// Script-style access to a native component.
	Entity e = w.create_entity();
	w.add<LocalTransform2D>(e);
	const ComponentType *lt = w.get_component_type(w.component_id<LocalTransform2D>());
	Dictionary v;
	v["transform"] = Transform2D(0.5, Vector2(3, 4));
	lt->from_dictionary(w.get_component_mut(e, lt->id), v);
	CHECK(w.get<LocalTransform2D>(e)->transform.get_origin() == Vector2(3, 4));
}

TEST_CASE("[ECS][Builtin] Movement and transform propagation") {
	World w;
	register_builtin_systems(w);

	Entity parent = w.create_entity();
	w.add<LocalTransform2D>(parent, { Transform2D(0, Vector2(100, 0)) });
	w.add<GlobalTransform2D>(parent);
	w.add<Velocity2D>(parent, { Vector2(10, 0), 0 });

	Entity child = w.create_entity();
	w.add<LocalTransform2D>(child, { Transform2D(0, Vector2(0, 5)) });
	w.add<GlobalTransform2D>(child);
	w.add<Parent>(child, { parent });

	Entity e3 = w.create_entity();
	w.add<LocalTransform3D>(e3, { Transform3D(Basis(), Vector3(1, 2, 3)) });
	w.add<Velocity3D>(e3, { Vector3(0, 0, 1), Vector3() });

	// Physics stage integrates velocities; render stage propagates transforms.
	w.physics_update(0.5);
	w.update(0.016);
	CHECK(w.get<LocalTransform2D>(parent)->transform.get_origin().is_equal_approx(Vector2(105, 0)));
	CHECK(w.get<GlobalTransform2D>(parent)->transform.get_origin().is_equal_approx(Vector2(105, 0)));
	CHECK(w.get<GlobalTransform2D>(child)->transform.get_origin().is_equal_approx(Vector2(105, 5)));
	CHECK(w.get<LocalTransform3D>(e3)->transform.origin.is_equal_approx(Vector3(1, 2, 3.5)));

	// Disabled (sparse tag) entities are skipped by movement.
	w.add<Disabled>(parent);
	w.physics_update(0.5);
	CHECK(w.get<LocalTransform2D>(parent)->transform.get_origin().is_equal_approx(Vector2(105, 0)));
	w.remove<Disabled>(parent);
	w.physics_update(0.5);
	CHECK(w.get<LocalTransform2D>(parent)->transform.get_origin().is_equal_approx(Vector2(110, 0)));
}

TEST_CASE("[SceneTree][ECS][Builtin] Sprites are pushed to the RenderingServer") {
	World w;
	register_builtin_systems(w);
	RenderingServer *rs = RenderingServer::get_singleton();
	REQUIRE(rs);
	RID canvas = rs->canvas_create();
	w.set_canvas(canvas);

	Entity e = w.create_entity();
	w.add<LocalTransform2D>(e, { Transform2D(0, Vector2(10, 10)) });
	ecs::Sprite2D s;
	s.size = Vector2(16, 16);
	w.add<ecs::Sprite2D>(e, s);
	CHECK_FALSE(w.get<ecs::Sprite2D>(e)->canvas_item.is_valid());
	w.update(0.016);
	RID item = w.get<ecs::Sprite2D>(e)->canvas_item;
	CHECK(item.is_valid());
	w.update(0.016);
	CHECK(w.get<ecs::Sprite2D>(e)->canvas_item == item); // Reused.
	w.destroy_entity(e); // Hook frees the canvas item.
	rs->free_rid(canvas);
}

TEST_CASE("[SceneTree][ECS][Builtin] Physics bodies follow the PhysicsServer2D") {
	PhysicsServer2D *ps = PhysicsServer2D::get_singleton();
	REQUIRE(ps);
	ps->set_active(true);
	RID space = ps->space_create();
	ps->space_set_active(space, true);
	RID shape = ps->rectangle_shape_create();
	ps->shape_set_data(shape, Vector2(5, 5));

	World w;
	register_builtin_systems(w);
	w.set_space_2d(space);

	Entity e = w.create_entity();
	w.add<LocalTransform2D>(e, { Transform2D(0, Vector2(0, 0)) });
	ecs::PhysicsBody2D body;
	body.shape = shape;
	body.mode = PhysicsServer2D::BODY_MODE_RIGID;
	body.linear_velocity = Vector2(100, 0);
	w.add<ecs::PhysicsBody2D>(e, body);

	// Mimic Main::iteration: sync -> physics_process -> end_sync -> step.
	for (int i = 0; i < 10; i++) {
		ps->sync();
		ps->flush_queries();
		w.physics_update(1.0 / 60.0);
		ps->end_sync();
		ps->step(1.0 / 60.0);
	}
	CHECK(w.get<ecs::PhysicsBody2D>(e)->body.is_valid());
	CHECK(w.get<LocalTransform2D>(e)->transform.get_origin().x > 10.0);

	// Teleporting the entity pushes the transform to the body.
	w.get_mut<LocalTransform2D>(e)->transform.set_origin(Vector2(-500, 0));
	ps->sync();
	ps->flush_queries();
	w.physics_update(1.0 / 60.0);
	ps->end_sync();
	ps->step(1.0 / 60.0);
	ps->sync();
	ps->flush_queries();
	w.physics_update(1.0 / 60.0);
	ps->end_sync();
	ps->step(1.0 / 60.0);
	CHECK(w.get<LocalTransform2D>(e)->transform.get_origin().x < -400.0);

	w.destroy_entity(e); // Frees the body.
	ps->free_rid(shape);
	ps->free_rid(space);
}

} // namespace TestECSBuiltin
