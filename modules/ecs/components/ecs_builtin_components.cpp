/**************************************************************************/
/*  ecs_builtin_components.cpp                                            */
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

#include "servers/rendering/rendering_server.h"

namespace ecs {

#define ECS_FIELD(m_struct, m_member, m_type) \
	t->add_field(#m_member, Variant::m_type, offsetof(m_struct, m_member), sizeof(m_struct::m_member))

static void _free_rid(RID &p_rid) {
	if (p_rid.is_valid() && RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->free_rid(p_rid);
	}
	p_rid = RID();
}

static void _sprite_removed(World &p_world, Entity p_entity, void *p_component) {
	_free_rid(static_cast<Sprite2D *>(p_component)->canvas_item);
}

static void _mesh_removed(World &p_world, Entity p_entity, void *p_component) {
	_free_rid(static_cast<MeshInstance3D *>(p_component)->instance);
}

static void _camera_3d_removed(World &p_world, Entity p_entity, void *p_component) {
	_free_rid(static_cast<Camera3D *>(p_component)->camera);
}

void World::_register_builtin_components() {
	ComponentType *t = nullptr;

	register_component<NetworkIdentity>("NetworkIdentity");
	t = get_component_type_mut(component_id<NetworkIdentity>());
	ECS_FIELD(NetworkIdentity, net_id, INT);
	ECS_FIELD(NetworkIdentity, authority, INT);

	register_component<Parent>("Parent", COMPONENT_FLAG_REPLICATED);
	t = get_component_type_mut(component_id<Parent>());
	t->add_field("parent", Variant::INT, offsetof(Parent, parent), sizeof(Entity), FIELD_KIND_ENTITY);

	register_component<PrefabRef>("Prefab", COMPONENT_FLAG_REPLICATED);
	t = get_component_type_mut(component_id<PrefabRef>());
	t->add_field("path", Variant::STRING, offsetof(PrefabRef, path), sizeof(String), FIELD_KIND_STRING);

	register_component<LocalTransform2D>("LocalTransform2D", COMPONENT_FLAG_REPLICATED | COMPONENT_FLAG_UNRELIABLE);
	t = get_component_type_mut(component_id<LocalTransform2D>());
	ECS_FIELD(LocalTransform2D, transform, TRANSFORM2D);

	register_component<GlobalTransform2D>("GlobalTransform2D");
	t = get_component_type_mut(component_id<GlobalTransform2D>());
	ECS_FIELD(GlobalTransform2D, transform, TRANSFORM2D);

	register_component<LocalTransform3D>("LocalTransform3D", COMPONENT_FLAG_REPLICATED | COMPONENT_FLAG_UNRELIABLE);
	t = get_component_type_mut(component_id<LocalTransform3D>());
	ECS_FIELD(LocalTransform3D, transform, TRANSFORM3D);

	register_component<GlobalTransform3D>("GlobalTransform3D");
	t = get_component_type_mut(component_id<GlobalTransform3D>());
	ECS_FIELD(GlobalTransform3D, transform, TRANSFORM3D);

	register_component<Velocity2D>("Velocity2D", COMPONENT_FLAG_REPLICATED | COMPONENT_FLAG_UNRELIABLE);
	t = get_component_type_mut(component_id<Velocity2D>());
	ECS_FIELD(Velocity2D, linear, VECTOR2);
	ECS_FIELD(Velocity2D, angular, FLOAT);

	register_component<Velocity3D>("Velocity3D", COMPONENT_FLAG_REPLICATED | COMPONENT_FLAG_UNRELIABLE);
	t = get_component_type_mut(component_id<Velocity3D>());
	ECS_FIELD(Velocity3D, linear, VECTOR3);
	ECS_FIELD(Velocity3D, angular, VECTOR3);

	register_component<Sprite2D>("Sprite2D");
	t = get_component_type_mut(component_id<Sprite2D>());
	ECS_FIELD(Sprite2D, texture, RID);
	ECS_FIELD(Sprite2D, size, VECTOR2);
	ECS_FIELD(Sprite2D, offset, VECTOR2);
	ECS_FIELD(Sprite2D, centered, BOOL);
	ECS_FIELD(Sprite2D, visible, BOOL);
	ECS_FIELD(Sprite2D, modulate, COLOR);
	ECS_FIELD(Sprite2D, z_index, INT);
	ECS_FIELD(Sprite2D, canvas_item, RID);
	t->on_remove = _sprite_removed;

	register_component<MeshInstance3D>("MeshInstance3D");
	t = get_component_type_mut(component_id<MeshInstance3D>());
	ECS_FIELD(MeshInstance3D, mesh, RID);
	ECS_FIELD(MeshInstance3D, visible, BOOL);
	ECS_FIELD(MeshInstance3D, instance, RID);
	t->on_remove = _mesh_removed;

	register_component<Camera2D>("Camera2D");
	t = get_component_type_mut(component_id<Camera2D>());
	ECS_FIELD(Camera2D, zoom, VECTOR2);
	ECS_FIELD(Camera2D, offset, VECTOR2);
	ECS_FIELD(Camera2D, current, BOOL);

	register_component<Camera3D>("Camera3D");
	t = get_component_type_mut(component_id<Camera3D>());
	ECS_FIELD(Camera3D, fov, FLOAT);
	ECS_FIELD(Camera3D, near, FLOAT);
	ECS_FIELD(Camera3D, far, FLOAT);
	ECS_FIELD(Camera3D, current, BOOL);
	ECS_FIELD(Camera3D, camera, RID);
	t->on_remove = _camera_3d_removed;

	register_component<Disabled>("Disabled", COMPONENT_FLAG_SPARSE);

	register_physics_components(*this);
}

#undef ECS_FIELD

/* Built-in systems */

namespace {

struct BuiltinSystems {
	World *world = nullptr;
	Query movement_2d;
	Query movement_3d;
	Query propagate_2d;
	Query propagate_3d;
	Query sprites;
	Query meshes;
	Query cameras_2d;
	Query cameras_3d;
};

void _movement_2d(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	const real_t delta = real_t(p_ctx.delta);
	bs->movement_2d.each<LocalTransform2D, const Velocity2D>([&](Entity, LocalTransform2D &p_t, const Velocity2D &p_v) {
		p_t.transform.columns[2] += p_v.linear * delta;
		if (p_v.angular != 0) {
			p_t.transform = p_t.transform.rotated_local(p_v.angular * delta);
		}
	});
}

void _movement_3d(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	const real_t delta = real_t(p_ctx.delta);
	bs->movement_3d.each<LocalTransform3D, const Velocity3D>([&](Entity, LocalTransform3D &p_t, const Velocity3D &p_v) {
		p_t.transform.origin += p_v.linear * delta;
		real_t len = p_v.angular.length();
		if (len > CMP_EPSILON) {
			p_t.transform.basis = Basis(p_v.angular / len, len * delta) * p_t.transform.basis;
		}
	});
}

template <typename LocalT, typename GlobalT, typename XformT>
XformT _resolve_global(World &p_world, Entity p_entity, ComponentId p_parent_id, ComponentId p_local_id, ComponentId p_global_id, int p_depth) {
	const LocalT *local = (const LocalT *)p_world.get_component(p_entity, p_local_id);
	XformT xf = local ? local->transform : XformT();
	if (p_depth > 64) {
		return xf;
	}
	const Parent *parent = (const Parent *)p_world.get_component(p_entity, p_parent_id);
	if (!parent || !p_world.is_alive(parent->parent) || parent->parent == p_entity) {
		return xf;
	}
	XformT parent_global = _resolve_global<LocalT, GlobalT, XformT>(p_world, parent->parent, p_parent_id, p_local_id, p_global_id, p_depth + 1);
	return parent_global * xf;
}

template <typename LocalT, typename GlobalT, typename XformT>
void _propagate(SystemContext &p_ctx, Query &p_query) {
	World &w = *p_ctx.world;
	ComponentId parent_id = w.component_id<Parent>();
	ComponentId local_id = w.component_id<LocalT>();
	ComponentId global_id = w.component_id<GlobalT>();
	p_query.for_each_archetype([&](Archetype &a) {
		int32_t lc = a.get_column_index(local_id);
		int32_t gc = a.get_column_index(global_id);
		int32_t pc = a.get_column_index(parent_id);
		const LocalT *locals = a.column_ptr<LocalT>(uint32_t(lc));
		GlobalT *globals = a.column_ptr<GlobalT>(uint32_t(gc));
		uint32_t *ticks = a.get_column(uint32_t(gc)).change_ticks;
		uint32_t rows = a.size();
		if (pc < 0) {
			for (uint32_t r = 0; r < rows; r++) {
				globals[r].transform = locals[r].transform;
				ticks[r] = p_ctx.tick;
			}
		} else {
			const Parent *parents = a.column_ptr<Parent>(uint32_t(pc));
			const Entity *ents = a.entities_ptr();
			for (uint32_t r = 0; r < rows; r++) {
				if (w.is_alive(parents[r].parent) && parents[r].parent != ents[r]) {
					XformT pg = _resolve_global<LocalT, GlobalT, XformT>(w, parents[r].parent, parent_id, local_id, global_id, 0);
					globals[r].transform = pg * locals[r].transform;
				} else {
					globals[r].transform = locals[r].transform;
				}
				ticks[r] = p_ctx.tick;
			}
		}
	});
}

void _propagate_2d(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	_propagate<LocalTransform2D, GlobalTransform2D, Transform2D>(p_ctx, bs->propagate_2d);
}

void _propagate_3d(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	_propagate<LocalTransform3D, GlobalTransform3D, Transform3D>(p_ctx, bs->propagate_3d);
}

_FORCE_INLINE_ Transform2D _world_xform_2d(Archetype &a, uint32_t r, int32_t gc, int32_t lc) {
	if (gc >= 0) {
		return a.column_ptr<GlobalTransform2D>(uint32_t(gc))[r].transform;
	}
	if (lc >= 0) {
		return a.column_ptr<LocalTransform2D>(uint32_t(lc))[r].transform;
	}
	return Transform2D();
}

_FORCE_INLINE_ Transform3D _world_xform_3d(Archetype &a, uint32_t r, int32_t gc, int32_t lc) {
	if (gc >= 0) {
		return a.column_ptr<GlobalTransform3D>(uint32_t(gc))[r].transform;
	}
	if (lc >= 0) {
		return a.column_ptr<LocalTransform3D>(uint32_t(lc))[r].transform;
	}
	return Transform3D();
}

void _sprite_sync(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	World &w = *p_ctx.world;
	RenderingServer *rs = RenderingServer::get_singleton();
	RID canvas = w.get_canvas();
	if (!rs || !canvas.is_valid()) {
		return;
	}
	ComponentId sprite_id = w.component_id<Sprite2D>();
	ComponentId local_id = w.component_id<LocalTransform2D>();
	ComponentId global_id = w.component_id<GlobalTransform2D>();
	ComponentId disabled_id = w.component_id<Disabled>();
	SparseComponentStorage *disabled = w.get_sparse_storage(disabled_id);
	const uint32_t last = p_ctx.last_run_tick;

	bs->sprites.for_each_archetype([&](Archetype &a) {
		int32_t sc = a.get_column_index(sprite_id);
		int32_t lc = a.get_column_index(local_id);
		int32_t gc = a.get_column_index(global_id);
		Sprite2D *sprites = a.column_ptr<Sprite2D>(uint32_t(sc));
		const uint32_t *sprite_ticks = a.change_ticks_ptr(uint32_t(sc));
		const uint32_t *xform_ticks = gc >= 0 ? a.change_ticks_ptr(uint32_t(gc)) : (lc >= 0 ? a.change_ticks_ptr(uint32_t(lc)) : nullptr);
		const Entity *ents = a.entities_ptr();
		uint32_t rows = a.size();
		for (uint32_t r = 0; r < rows; r++) {
			Sprite2D &s = sprites[r];
			bool fresh = false;
			if (!s.canvas_item.is_valid()) {
				s.canvas_item = rs->canvas_item_create();
				rs->canvas_item_set_parent(s.canvas_item, canvas);
				fresh = true;
			}
			if (fresh || sprite_ticks[r] > last) {
				rs->canvas_item_clear(s.canvas_item);
				if (s.texture.is_valid() && s.size != Vector2()) {
					Vector2 pos = s.offset;
					if (s.centered) {
						pos -= s.size * 0.5;
					}
					rs->canvas_item_add_texture_rect(s.canvas_item, Rect2(pos, s.size), s.texture);
				}
				rs->canvas_item_set_modulate(s.canvas_item, s.modulate);
				rs->canvas_item_set_z_index(s.canvas_item, s.z_index);
			}
			bool visible = s.visible && !(disabled && disabled->has(ents[r].index()));
			if (fresh || sprite_ticks[r] > last || disabled) {
				rs->canvas_item_set_visible(s.canvas_item, visible);
			}
			if (fresh || xform_ticks == nullptr || xform_ticks[r] > last) {
				rs->canvas_item_set_transform(s.canvas_item, _world_xform_2d(a, r, gc, lc));
			}
		}
	});
}

void _mesh_sync(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	World &w = *p_ctx.world;
	RenderingServer *rs = RenderingServer::get_singleton();
	RID scenario = w.get_scenario();
	if (!rs || !scenario.is_valid()) {
		return;
	}
	ComponentId mesh_id = w.component_id<MeshInstance3D>();
	ComponentId local_id = w.component_id<LocalTransform3D>();
	ComponentId global_id = w.component_id<GlobalTransform3D>();
	ComponentId disabled_id = w.component_id<Disabled>();
	SparseComponentStorage *disabled = w.get_sparse_storage(disabled_id);
	const uint32_t last = p_ctx.last_run_tick;

	bs->meshes.for_each_archetype([&](Archetype &a) {
		int32_t mc = a.get_column_index(mesh_id);
		int32_t lc = a.get_column_index(local_id);
		int32_t gc = a.get_column_index(global_id);
		MeshInstance3D *meshes = a.column_ptr<MeshInstance3D>(uint32_t(mc));
		const uint32_t *mesh_ticks = a.change_ticks_ptr(uint32_t(mc));
		const uint32_t *xform_ticks = gc >= 0 ? a.change_ticks_ptr(uint32_t(gc)) : (lc >= 0 ? a.change_ticks_ptr(uint32_t(lc)) : nullptr);
		const Entity *ents = a.entities_ptr();
		uint32_t rows = a.size();
		for (uint32_t r = 0; r < rows; r++) {
			MeshInstance3D &m = meshes[r];
			bool fresh = false;
			if (!m.instance.is_valid()) {
				m.instance = rs->instance_create();
				rs->instance_set_scenario(m.instance, scenario);
				fresh = true;
			}
			if (fresh || mesh_ticks[r] > last) {
				rs->instance_set_base(m.instance, m.mesh);
			}
			bool visible = m.visible && !(disabled && disabled->has(ents[r].index()));
			if (fresh || mesh_ticks[r] > last || disabled) {
				rs->instance_set_visible(m.instance, visible);
			}
			if (fresh || xform_ticks == nullptr || xform_ticks[r] > last) {
				rs->instance_set_transform(m.instance, _world_xform_3d(a, r, gc, lc));
			}
		}
	});
}

void _camera_2d_sync(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	World &w = *p_ctx.world;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs || !w.get_viewport().is_valid() || !w.get_canvas().is_valid()) {
		return;
	}
	ComponentId cam_id = w.component_id<Camera2D>();
	ComponentId local_id = w.component_id<LocalTransform2D>();
	ComponentId global_id = w.component_id<GlobalTransform2D>();
	bool done = false;
	bs->cameras_2d.for_each_archetype([&](Archetype &a) {
		if (done) {
			return;
		}
		int32_t cc = a.get_column_index(cam_id);
		int32_t lc = a.get_column_index(local_id);
		int32_t gc = a.get_column_index(global_id);
		const Camera2D *cams = a.column_ptr<Camera2D>(uint32_t(cc));
		uint32_t rows = a.size();
		for (uint32_t r = 0; r < rows; r++) {
			if (!cams[r].current) {
				continue;
			}
			Transform2D cam = _world_xform_2d(a, r, gc, lc);
			cam.columns[2] += cams[r].offset;
			Transform2D xform = cam.affine_inverse();
			Vector2 zoom = cams[r].zoom;
			if (zoom.x == 0 || zoom.y == 0) {
				zoom = Vector2(1, 1);
			}
			xform = Transform2D().scaled(zoom) * xform;
			xform.columns[2] += w.get_viewport_size() * 0.5;
			rs->viewport_set_canvas_transform(w.get_viewport(), w.get_canvas(), xform);
			done = true;
			return;
		}
	});
}

void _camera_3d_sync(SystemContext &p_ctx) {
	BuiltinSystems *bs = (BuiltinSystems *)p_ctx.userdata;
	World &w = *p_ctx.world;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	ComponentId cam_id = w.component_id<Camera3D>();
	ComponentId local_id = w.component_id<LocalTransform3D>();
	ComponentId global_id = w.component_id<GlobalTransform3D>();
	const uint32_t last = p_ctx.last_run_tick;
	bool attached = false;
	bs->cameras_3d.for_each_archetype([&](Archetype &a) {
		int32_t cc = a.get_column_index(cam_id);
		int32_t lc = a.get_column_index(local_id);
		int32_t gc = a.get_column_index(global_id);
		Camera3D *cams = a.column_ptr<Camera3D>(uint32_t(cc));
		const uint32_t *cam_ticks = a.change_ticks_ptr(uint32_t(cc));
		uint32_t rows = a.size();
		for (uint32_t r = 0; r < rows; r++) {
			Camera3D &c = cams[r];
			bool fresh = false;
			if (!c.camera.is_valid()) {
				c.camera = rs->camera_create();
				fresh = true;
			}
			if (fresh || cam_ticks[r] > last) {
				rs->camera_set_perspective(c.camera, float(c.fov), float(c.near), float(c.far));
			}
			rs->camera_set_transform(c.camera, _world_xform_3d(a, r, gc, lc));
			if (c.current && !attached && w.get_viewport().is_valid()) {
				rs->viewport_attach_camera(w.get_viewport(), c.camera);
				attached = true;
			}
		}
	});
}

void _cleanup(World &p_world, void *p_userdata) {
	memdelete((BuiltinSystems *)p_userdata);
}

} // namespace

void register_builtin_systems(World &p_world) {
	BuiltinSystems *bs = memnew(BuiltinSystems);
	bs->world = &p_world;

	ComponentMask disabled;
	disabled.set(p_world.component_id<Disabled>());

	bs->movement_2d.init_with<LocalTransform2D, Velocity2D>(&p_world, disabled);
	bs->movement_3d.init_with<LocalTransform3D, Velocity3D>(&p_world, disabled);
	bs->propagate_2d.init_with<LocalTransform2D, GlobalTransform2D>(&p_world);
	bs->propagate_3d.init_with<LocalTransform3D, GlobalTransform3D>(&p_world);
	bs->sprites.init_with<Sprite2D>(&p_world);
	bs->meshes.init_with<MeshInstance3D>(&p_world);
	bs->cameras_2d.init_with<Camera2D>(&p_world);
	bs->cameras_3d.init_with<Camera3D>(&p_world);

	auto add = [&](const char *p_name, Stage p_stage, int p_order, SystemFunc p_func, bool p_thread_safe, std::initializer_list<ComponentId> p_reads, std::initializer_list<ComponentId> p_writes) {
		SystemDesc d;
		d.name = p_name;
		d.stage = p_stage;
		d.order = p_order;
		d.func = p_func;
		d.userdata = bs;
		d.thread_safe = p_thread_safe;
		for (ComponentId c : p_reads) {
			d.reads.set(c);
		}
		for (ComponentId c : p_writes) {
			d.writes.set(c);
		}
		p_world.add_system(d);
	};

	const ComponentId lt2 = p_world.component_id<LocalTransform2D>();
	const ComponentId gt2 = p_world.component_id<GlobalTransform2D>();
	const ComponentId lt3 = p_world.component_id<LocalTransform3D>();
	const ComponentId gt3 = p_world.component_id<GlobalTransform3D>();
	const ComponentId v2 = p_world.component_id<Velocity2D>();
	const ComponentId v3 = p_world.component_id<Velocity3D>();
	const ComponentId par = p_world.component_id<Parent>();
	const ComponentId spr = p_world.component_id<Sprite2D>();
	const ComponentId mesh = p_world.component_id<MeshInstance3D>();
	const ComponentId cam2 = p_world.component_id<Camera2D>();
	const ComponentId cam3 = p_world.component_id<Camera3D>();

	add("builtin_movement_2d", STAGE_PHYSICS, 0, _movement_2d, true, { v2 }, { lt2 });
	add("builtin_movement_3d", STAGE_PHYSICS, 0, _movement_3d, true, { v3 }, { lt3 });
	add("builtin_transform_propagation_2d", STAGE_RENDER, -100, _propagate_2d, true, { lt2, par }, { gt2 });
	add("builtin_transform_propagation_3d", STAGE_RENDER, -100, _propagate_3d, true, { lt3, par }, { gt3 });
	add("builtin_sprite_sync", STAGE_RENDER, 0, _sprite_sync, false, { lt2, gt2 }, { spr });
	add("builtin_mesh_sync", STAGE_RENDER, 0, _mesh_sync, false, { lt3, gt3 }, { mesh });
	add("builtin_camera_2d_sync", STAGE_RENDER, 10, _camera_2d_sync, false, { lt2, gt2, cam2 }, {});
	add("builtin_camera_3d_sync", STAGE_RENDER, 10, _camera_3d_sync, false, { lt3, gt3 }, { cam3 });

	register_physics_systems(p_world);

	// Free the shared state when the world goes away.
	p_world.add_cleanup_callback(_cleanup, bs);
}

} // namespace ecs
