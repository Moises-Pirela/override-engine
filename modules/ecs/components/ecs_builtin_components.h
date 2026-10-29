/**************************************************************************/
/*  ecs_builtin_components.h                                              */
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

#include "../core/ecs_entity.h"

#include "core/math/color.h"
#include "core/math/transform_2d.h"
#include "core/math/transform_3d.h"
#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/string/ustring.h"
#include "core/templates/rid.h"

namespace ecs {

class World;

// Built-in components registered in every World. They replace the most
// common scene nodes and talk to the servers directly through the built-in
// systems (see ecs_builtin_systems.cpp).

// Marks an entity as replicated. Assigned by the server; `authority` is the
// peer id allowed to send COMPONENT_FLAG_OWNER_AUTHORITY components.
struct NetworkIdentity {
	uint32_t net_id = 0;
	int32_t authority = 1;
};

struct Parent {
	Entity parent;
};

// Path of the ECSPrefab an entity was instantiated from ("res://x.tres" or
// "res://x.tres#0/2" for nested children). Replicated so that clients can
// apply the prefab's local-only components (sprites, meshes...) on spawn.
struct PrefabRef {
	String path;
};

struct LocalTransform2D {
	Transform2D transform;
};

struct GlobalTransform2D {
	Transform2D transform;
};

struct LocalTransform3D {
	Transform3D transform;
};

struct GlobalTransform3D {
	Transform3D transform;
};

struct Velocity2D {
	Vector2 linear;
	real_t angular = 0;
};

struct Velocity3D {
	Vector3 linear;
	Vector3 angular;
};

// A textured quad on the world canvas (replaces Sprite2D).
struct Sprite2D {
	RID texture;
	Vector2 size; // Zero = texture size.
	Vector2 offset;
	bool centered = true;
	bool visible = true;
	Color modulate = Color(1, 1, 1, 1);
	int32_t z_index = 0;
	RID canvas_item; // Managed by the render system.
};

// A mesh in the world scenario (replaces MeshInstance3D).
struct MeshInstance3D {
	RID mesh;
	bool visible = true;
	RID instance; // Managed by the render system.
};

struct Camera2D {
	Vector2 zoom = Vector2(1, 1);
	Vector2 offset;
	bool current = true;
};

struct Camera3D {
	real_t fov = 75;
	real_t near = 0.05;
	real_t far = 4000;
	bool current = true;
	RID camera; // Managed by the render system.
};

// Marker: entity is not rendered / not updated (sparse, so toggling it is free).
struct Disabled {};

// A body in the world's 2D physics space (replaces StaticBody2D /
// CharacterBody2D / RigidBody2D). `shape` is a PhysicsServer2D shape RID
// (e.g. Shape2D.get_rid()). Rigid bodies write their transform and
// velocities back into the entity every physics tick; static and kinematic
// bodies follow the entity's LocalTransform2D.
struct PhysicsBody2D {
	RID shape;
	int32_t mode = 2; // PhysicsServer2D::BodyMode (0 static, 1 kinematic, 2 rigid, 3 rigid linear).
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	real_t mass = 1;
	Vector2 linear_velocity;
	real_t angular_velocity = 0;
	RID body; // Managed by the physics system.
	RID applied_shape; // Managed by the physics system.
};

struct PhysicsBody3D {
	RID shape;
	int32_t mode = 2; // PhysicsServer3D::BodyMode.
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	real_t mass = 1;
	Vector3 linear_velocity;
	Vector3 angular_velocity;
	RID body; // Managed by the physics system.
	RID applied_shape; // Managed by the physics system.
};

// Registers the built-in systems (movement, transform propagation, render
// sync, physics sync). Called by ECSWorld / ECSMainLoop; optional for bare
// ecs::World users.
void register_builtin_systems(World &p_world);

// Implemented in ecs_builtin_physics.cpp.
void register_physics_components(World &p_world);
void register_physics_systems(World &p_world);

} // namespace ecs
