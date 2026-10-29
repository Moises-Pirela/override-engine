/**************************************************************************/
/*  ecs_builtin_physics.cpp                                               */
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

#include "ecs_builtin_components.h"

#include "../core/ecs_query.h"
#include "../core/ecs_world.h"

#ifndef PHYSICS_2D_DISABLED
#include "servers/physics_2d/physics_server_2d.h"
#endif
#ifndef PHYSICS_3D_DISABLED
#include "servers/physics_3d/physics_server_3d.h"
#endif

namespace ecs {

#define ECS_FIELD(m_struct, m_member, m_type) \
	t->add_field(#m_member, Variant::m_type, offsetof(m_struct, m_member), sizeof(m_struct::m_member))

#ifndef PHYSICS_2D_DISABLED
static void _body_2d_removed(World &p_world, Entity p_entity, void *p_component) {
	PhysicsBody2D *b = static_cast<PhysicsBody2D *>(p_component);
	if (b->body.is_valid() && PhysicsServer2D::get_singleton()) {
		PhysicsServer2D::get_singleton()->free_rid(b->body);
	}
	b->body = RID();
	b->applied_shape = RID();
}
#endif

#ifndef PHYSICS_3D_DISABLED
static void _body_3d_removed(World &p_world, Entity p_entity, void *p_component) {
	PhysicsBody3D *b = static_cast<PhysicsBody3D *>(p_component);
	if (b->body.is_valid() && PhysicsServer3D::get_singleton()) {
		PhysicsServer3D::get_singleton()->free_rid(b->body);
	}
	b->body = RID();
	b->applied_shape = RID();
}
#endif

void register_physics_components(World &p_world) {
	ComponentType *t = nullptr;
#ifndef PHYSICS_2D_DISABLED
	p_world.register_component<PhysicsBody2D>("PhysicsBody2D");
	t = p_world.get_component_type_mut(p_world.component_id<PhysicsBody2D>());
	ECS_FIELD(PhysicsBody2D, shape, RID);
	ECS_FIELD(PhysicsBody2D, mode, INT);
	ECS_FIELD(PhysicsBody2D, collision_layer, INT);
	ECS_FIELD(PhysicsBody2D, collision_mask, INT);
	ECS_FIELD(PhysicsBody2D, mass, FLOAT);
	ECS_FIELD(PhysicsBody2D, linear_velocity, VECTOR2);
	ECS_FIELD(PhysicsBody2D, angular_velocity, FLOAT);
	ECS_FIELD(PhysicsBody2D, body, RID);
	t->on_remove = _body_2d_removed;
#endif
#ifndef PHYSICS_3D_DISABLED
	p_world.register_component<PhysicsBody3D>("PhysicsBody3D");
	t = p_world.get_component_type_mut(p_world.component_id<PhysicsBody3D>());
	ECS_FIELD(PhysicsBody3D, shape, RID);
	ECS_FIELD(PhysicsBody3D, mode, INT);
	ECS_FIELD(PhysicsBody3D, collision_layer, INT);
	ECS_FIELD(PhysicsBody3D, collision_mask, INT);
	ECS_FIELD(PhysicsBody3D, mass, FLOAT);
	ECS_FIELD(PhysicsBody3D, linear_velocity, VECTOR3);
	ECS_FIELD(PhysicsBody3D, angular_velocity, VECTOR3);
	ECS_FIELD(PhysicsBody3D, body, RID);
	t->on_remove = _body_3d_removed;
#endif
	(void)t;
}

#undef ECS_FIELD

namespace {

struct PhysicsSystems {
	Query bodies_2d;
	Query bodies_3d;
};

#ifndef PHYSICS_2D_DISABLED
// Runs at the start of every physics tick, i.e. after the physics server
// stepped with the previous state: rigid bodies are read back into the
// entity, entity-side changes (mode, shape, velocities, transform) are pushed
// to the server.
void _physics_2d_sync(SystemContext &p_ctx) {
	PhysicsSystems *ps = (PhysicsSystems *)p_ctx.userdata;
	World &w = *p_ctx.world;
	PhysicsServer2D *server = PhysicsServer2D::get_singleton();
	RID space = w.get_space_2d();
	if (!server || !space.is_valid()) {
		return;
	}
	const ComponentId body_id = w.component_id<PhysicsBody2D>();
	const ComponentId xform_id = w.component_id<LocalTransform2D>();
	const uint32_t last = p_ctx.last_run_tick;
	const uint32_t tick = p_ctx.tick;

	ps->bodies_2d.for_each_archetype([&](Archetype &a) {
		int32_t bc = a.get_column_index(body_id);
		int32_t xc = a.get_column_index(xform_id);
		PhysicsBody2D *bodies = a.column_ptr<PhysicsBody2D>(uint32_t(bc));
		LocalTransform2D *xforms = a.column_ptr<LocalTransform2D>(uint32_t(xc));
		uint32_t *body_ticks = a.get_column(uint32_t(bc)).change_ticks;
		uint32_t *xform_ticks = a.get_column(uint32_t(xc)).change_ticks;
		uint32_t rows = a.size();
		for (uint32_t r = 0; r < rows; r++) {
			PhysicsBody2D &b = bodies[r];
			bool fresh = false;
			if (!b.body.is_valid()) {
				b.body = server->body_create();
				server->body_set_space(b.body, space);
				fresh = true;
			}
			const bool rigid = b.mode == PhysicsServer2D::BODY_MODE_RIGID || b.mode == PhysicsServer2D::BODY_MODE_RIGID_LINEAR;
			if (fresh || body_ticks[r] > last) {
				server->body_set_mode(b.body, PhysicsServer2D::BodyMode(CLAMP(b.mode, 0, 3)));
				server->body_set_collision_layer(b.body, b.collision_layer);
				server->body_set_collision_mask(b.body, b.collision_mask);
				server->body_set_param(b.body, PhysicsServer2D::BODY_PARAM_MASS, b.mass);
				if (b.applied_shape != b.shape) {
					server->body_clear_shapes(b.body);
					if (b.shape.is_valid()) {
						server->body_add_shape(b.body, b.shape);
					}
					b.applied_shape = b.shape;
				}
				server->body_set_state(b.body, PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY, b.linear_velocity);
				server->body_set_state(b.body, PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY, b.angular_velocity);
			}
			if (fresh || xform_ticks[r] > last) {
				server->body_set_state(b.body, PhysicsServer2D::BODY_STATE_TRANSFORM, xforms[r].transform);
			} else if (rigid) {
				xforms[r].transform = server->body_get_state(b.body, PhysicsServer2D::BODY_STATE_TRANSFORM);
				b.linear_velocity = server->body_get_state(b.body, PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY);
				b.angular_velocity = server->body_get_state(b.body, PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY);
				xform_ticks[r] = tick;
			}
		}
	});
}
#endif

#ifndef PHYSICS_3D_DISABLED
void _physics_3d_sync(SystemContext &p_ctx) {
	PhysicsSystems *ps = (PhysicsSystems *)p_ctx.userdata;
	World &w = *p_ctx.world;
	PhysicsServer3D *server = PhysicsServer3D::get_singleton();
	RID space = w.get_space_3d();
	if (!server || !space.is_valid()) {
		return;
	}
	const ComponentId body_id = w.component_id<PhysicsBody3D>();
	const ComponentId xform_id = w.component_id<LocalTransform3D>();
	const uint32_t last = p_ctx.last_run_tick;
	const uint32_t tick = p_ctx.tick;

	ps->bodies_3d.for_each_archetype([&](Archetype &a) {
		int32_t bc = a.get_column_index(body_id);
		int32_t xc = a.get_column_index(xform_id);
		PhysicsBody3D *bodies = a.column_ptr<PhysicsBody3D>(uint32_t(bc));
		LocalTransform3D *xforms = a.column_ptr<LocalTransform3D>(uint32_t(xc));
		uint32_t *body_ticks = a.get_column(uint32_t(bc)).change_ticks;
		uint32_t *xform_ticks = a.get_column(uint32_t(xc)).change_ticks;
		uint32_t rows = a.size();
		for (uint32_t r = 0; r < rows; r++) {
			PhysicsBody3D &b = bodies[r];
			bool fresh = false;
			if (!b.body.is_valid()) {
				b.body = server->body_create();
				server->body_set_space(b.body, space);
				fresh = true;
			}
			const bool rigid = b.mode == PhysicsServer3D::BODY_MODE_RIGID || b.mode == PhysicsServer3D::BODY_MODE_RIGID_LINEAR;
			if (fresh || body_ticks[r] > last) {
				server->body_set_mode(b.body, PhysicsServer3D::BodyMode(CLAMP(b.mode, 0, 3)));
				server->body_set_collision_layer(b.body, b.collision_layer);
				server->body_set_collision_mask(b.body, b.collision_mask);
				server->body_set_param(b.body, PhysicsServer3D::BODY_PARAM_MASS, b.mass);
				if (b.applied_shape != b.shape) {
					server->body_clear_shapes(b.body);
					if (b.shape.is_valid()) {
						server->body_add_shape(b.body, b.shape);
					}
					b.applied_shape = b.shape;
				}
				server->body_set_state(b.body, PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY, b.linear_velocity);
				server->body_set_state(b.body, PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY, b.angular_velocity);
			}
			if (fresh || xform_ticks[r] > last) {
				server->body_set_state(b.body, PhysicsServer3D::BODY_STATE_TRANSFORM, xforms[r].transform);
			} else if (rigid) {
				xforms[r].transform = server->body_get_state(b.body, PhysicsServer3D::BODY_STATE_TRANSFORM);
				b.linear_velocity = server->body_get_state(b.body, PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY);
				b.angular_velocity = server->body_get_state(b.body, PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY);
				xform_ticks[r] = tick;
			}
		}
	});
}
#endif

void _physics_cleanup(World &p_world, void *p_userdata) {
	memdelete((PhysicsSystems *)p_userdata);
}

} // namespace

void register_physics_systems(World &p_world) {
	PhysicsSystems *ps = memnew(PhysicsSystems);
#ifndef PHYSICS_2D_DISABLED
	ps->bodies_2d.init_with<PhysicsBody2D, LocalTransform2D>(&p_world);
	{
		SystemDesc d;
		d.name = "builtin_physics_2d_sync";
		d.stage = STAGE_PRE_PHYSICS;
		d.order = -100;
		d.func = _physics_2d_sync;
		d.userdata = ps;
		d.reads.set(p_world.component_id<PhysicsBody2D>());
		d.writes.set(p_world.component_id<PhysicsBody2D>());
		d.writes.set(p_world.component_id<LocalTransform2D>());
		p_world.add_system(d);
	}
#endif
#ifndef PHYSICS_3D_DISABLED
	ps->bodies_3d.init_with<PhysicsBody3D, LocalTransform3D>(&p_world);
	{
		SystemDesc d;
		d.name = "builtin_physics_3d_sync";
		d.stage = STAGE_PRE_PHYSICS;
		d.order = -100;
		d.func = _physics_3d_sync;
		d.userdata = ps;
		d.reads.set(p_world.component_id<PhysicsBody3D>());
		d.writes.set(p_world.component_id<PhysicsBody3D>());
		d.writes.set(p_world.component_id<LocalTransform3D>());
		p_world.add_system(d);
	}
#endif
	p_world.add_cleanup_callback(_physics_cleanup, ps);
}

} // namespace ecs
