/**************************************************************************/
/*  ecs_world_bind.h                                                      */
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

#include "../core/ecs_query.h"
#include "../core/ecs_world.h"
#include "../net/ecs_replicator.h"
#include "../resources/ecs_event_library.h"
#include "../resources/ecs_prefab.h"
#include "ecs_system_bind.h"

#include "core/io/resource.h"
#include "core/object/ref_counted.h"
#include "scene/main/multiplayer_peer.h"

class Texture2D;
class Mesh;
class Shape2D;
class Shape3D;
class ECSQuery;

// Script-facing wrapper around ecs::World. Entities are plain integers,
// components are addressed by name (StringName) or id (int).
class ECSWorld : public RefCounted, public ecs::Replicator::Listener {
	GDCLASS(ECSWorld, RefCounted);

	friend class ECSQuery;

	ecs::World *world = nullptr;
	Ref<MultiplayerPeer> multiplayer_peer;
	// Resources handed to the helpers below (textures, meshes, shapes) are
	// kept alive here: components only store their RID.
	HashMap<RID, Ref<Resource>> retained_resources;
	LocalVector<Ref<ECSSystem>> system_objects;

	void _retain(const Ref<Resource> &p_resource);
	ecs::Entity _instantiate_prefab(const Ref<ECSPrefab> &p_prefab, ecs::Entity p_parent, const String &p_root_path, const String &p_index_path);
	Ref<ECSPrefab> _create_prefab(ecs::Entity p_entity, bool p_recursive, ecs::Query &p_parents);
	void _apply_prefab_locals(ecs::Entity p_entity);

	ecs::ComponentId _resolve(const Variant &p_component) const;
	ecs::EventId _resolve_event(const Variant &p_event) const;
	bool _event_mask_from(const Variant &p_events, ecs::EventMask &r_mask) const;
	ecs::Replicator *_replicator();
	static Array _mask_to_names(const ecs::World *p_world, const ecs::ComponentMask &p_mask);
	bool _mask_from_array(const Array &p_components, ecs::ComponentMask &r_mask) const;

	// Replicator::Listener.
	virtual void on_peer_connected(int p_peer) override;
	virtual void on_peer_disconnected(int p_peer) override;
	virtual void on_entity_spawned(ecs::Entity p_entity, uint32_t p_net_id) override;
	virtual void on_entity_despawned(ecs::Entity p_entity, uint32_t p_net_id) override;
	virtual void on_authority_changed(ecs::Entity p_entity, int p_peer) override;
	virtual bool on_command(int p_sender, const StringName &p_name, const Array &p_args) override;

protected:
	static void _bind_methods();

public:
	enum Stage {
		STAGE_PRE_UPDATE = ecs::STAGE_PRE_UPDATE,
		STAGE_UPDATE = ecs::STAGE_UPDATE,
		STAGE_POST_UPDATE = ecs::STAGE_POST_UPDATE,
		STAGE_RENDER = ecs::STAGE_RENDER,
		STAGE_PRE_PHYSICS = ecs::STAGE_PRE_PHYSICS,
		STAGE_PHYSICS = ecs::STAGE_PHYSICS,
		STAGE_POST_PHYSICS = ecs::STAGE_POST_PHYSICS,
		STAGE_NETWORK = ecs::STAGE_NETWORK,
	};

	enum ComponentFlags {
		COMPONENT_FLAG_NONE = ecs::COMPONENT_FLAG_NONE,
		COMPONENT_FLAG_SPARSE = ecs::COMPONENT_FLAG_SPARSE,
		COMPONENT_FLAG_REPLICATED = ecs::COMPONENT_FLAG_REPLICATED,
		COMPONENT_FLAG_OWNER_AUTHORITY = ecs::COMPONENT_FLAG_OWNER_AUTHORITY,
		COMPONENT_FLAG_UNRELIABLE = ecs::COMPONENT_FLAG_UNRELIABLE,
	};

	enum Event {
		EVENT_ENTITY_CREATE = ecs::EVENT_ENTITY_CREATE,
		EVENT_ENTITY_DESTROY = ecs::EVENT_ENTITY_DESTROY,
		EVENT_COMPONENT_ADDED = ecs::EVENT_COMPONENT_ADDED,
		EVENT_COMPONENT_REMOVED = ecs::EVENT_COMPONENT_REMOVED,
		EVENT_PRE_UPDATE = ecs::EVENT_PRE_UPDATE,
		EVENT_UPDATE = ecs::EVENT_UPDATE,
		EVENT_LATE_UPDATE = ecs::EVENT_LATE_UPDATE,
		EVENT_RENDER = ecs::EVENT_RENDER,
		EVENT_PRE_FIXED_UPDATE = ecs::EVENT_PRE_FIXED_UPDATE,
		EVENT_FIXED_UPDATE = ecs::EVENT_FIXED_UPDATE,
		EVENT_LATE_FIXED_UPDATE = ecs::EVENT_LATE_FIXED_UPDATE,
		EVENT_PEER_CONNECTED = ecs::EVENT_PEER_CONNECTED,
		EVENT_PEER_DISCONNECTED = ecs::EVENT_PEER_DISCONNECTED,
		EVENT_ENTITY_SPAWNED = ecs::EVENT_ENTITY_SPAWNED,
		EVENT_ENTITY_DESPAWNED = ecs::EVENT_ENTITY_DESPAWNED,
		EVENT_AUTHORITY_CHANGED = ecs::EVENT_AUTHORITY_CHANGED,
	};

	enum {
		FIELD_TYPE_ENTITY = ecs::FIELD_TYPE_ENTITY,
		NULL_ENTITY = 0,
	};

	_FORCE_INLINE_ ecs::World *get_native() const { return world; }

	/* Components */
	int64_t register_component(const StringName &p_name, const Dictionary &p_fields, int p_flags = 0);
	int64_t get_component_id(const StringName &p_name) const;
	StringName get_component_name(int64_t p_id) const;
	PackedStringArray get_component_names() const;
	Dictionary get_component_info(const Variant &p_component) const;
	int64_t get_component_count() const;

	/* Entities */
	int64_t create_entity(const Dictionary &p_components = Dictionary());
	bool destroy_entity(int64_t p_entity);
	bool is_alive(int64_t p_entity) const;
	int64_t get_entity_count() const;
	void clear_entities();
	Array get_entity_components(int64_t p_entity) const;

	bool add_component(int64_t p_entity, const Variant &p_component, const Dictionary &p_values = Dictionary());
	bool remove_component(int64_t p_entity, const Variant &p_component);
	bool has_component(int64_t p_entity, const Variant &p_component) const;
	Dictionary get_component(int64_t p_entity, const Variant &p_component) const;
	bool set_component(int64_t p_entity, const Variant &p_component, const Dictionary &p_values);
	Variant get_field(int64_t p_entity, const Variant &p_component, const StringName &p_field) const;
	bool set_field(int64_t p_entity, const Variant &p_component, const StringName &p_field, const Variant &p_value);
	int64_t get_change_tick(int64_t p_entity, const Variant &p_component) const;

	/* Deferred */
	void defer_destroy_entity(int64_t p_entity);
	void defer_add_component(int64_t p_entity, const Variant &p_component, const Dictionary &p_values = Dictionary());
	void defer_remove_component(int64_t p_entity, const Variant &p_component);
	void flush_commands();

	/* Prefabs and schemas */
	int64_t register_schema(const Ref<ECSComponentSchema> &p_schema);
	int64_t instantiate_prefab(const Ref<ECSPrefab> &p_prefab, int64_t p_parent = 0);
	bool apply_prefab(int64_t p_entity, const Ref<ECSPrefab> &p_prefab, bool p_overwrite = true);
	Ref<ECSPrefab> create_prefab(int64_t p_entity, bool p_recursive = true);
	PackedInt64Array get_children(int64_t p_entity) const;

	/* Queries */
	Ref<ECSQuery> query(const Array &p_include, const Array &p_exclude = Array());

	/* Events */
	int64_t register_event(const StringName &p_name);
	int64_t get_event_id(const StringName &p_name) const;
	StringName get_event_name(int64_t p_event) const;
	PackedStringArray get_event_names() const;
	int64_t subscribe(const Variant &p_events, const Callable &p_callable, const Array &p_filter = Array(), int64_t p_entity = 0, int p_order = 0);
	bool unsubscribe(int64_t p_subscription);
	void set_subscription_enabled(int64_t p_subscription, bool p_enabled);
	void emit_event(const Variant &p_event, const Variant &p_payload = Variant(), int64_t p_entity = 0);
	void queue_event(const Variant &p_event, const Variant &p_payload = Variant(), int64_t p_entity = 0);
	void flush_events();
	int64_t add_system_object(const Ref<ECSSystem> &p_system);
	bool remove_system_object(const Ref<ECSSystem> &p_system);
	Array get_system_objects() const;
	// Removes every system, system object and subscription. Breaks reference
	// cycles between the world and scripts that captured it (queries,
	// lambdas); ECSMainLoop calls it on exit.
	void clear_systems();

	/* Systems */
	int64_t add_system(const StringName &p_name, const Callable &p_callable, Stage p_stage = STAGE_UPDATE, int p_order = 0, const Array &p_reads = Array(), const Array &p_writes = Array());
	bool remove_system(const StringName &p_name);
	void set_system_enabled(const StringName &p_name, bool p_enabled);
	bool is_system_enabled(const StringName &p_name) const;
	PackedStringArray get_system_names() const;
	void update(double p_delta);
	void physics_update(double p_delta);
	void run_stage(Stage p_stage, double p_delta);
	int64_t get_tick() const;

	/* Networking */
	void set_multiplayer_peer(const Ref<MultiplayerPeer> &p_peer);
	Ref<MultiplayerPeer> get_multiplayer_peer() const;
	bool is_server() const;
	bool is_network_connected() const;
	int get_unique_id() const;
	PackedInt32Array get_peers() const;
	void replicate(int64_t p_entity, int p_authority = 1, bool p_recursive = false);
	int64_t get_network_id(int64_t p_entity) const;
	int64_t get_entity_by_network_id(int64_t p_net_id) const;
	int get_authority(int64_t p_entity) const;
	void set_authority(int64_t p_entity, int p_peer);
	bool has_authority(int64_t p_entity) const;
	void register_command(const StringName &p_name, const Callable &p_callable);
	void unregister_command(const StringName &p_name);
	void send_command(int p_peer, const StringName &p_name, const Array &p_args = Array(), bool p_reliable = true);
	void set_sync_interval(int p_ticks);
	int get_sync_interval() const;
	Dictionary get_network_stats() const;

	/* Rendering context */
	void set_canvas(RID p_canvas);
	RID get_canvas() const;
	void set_scenario(RID p_scenario);
	RID get_scenario() const;
	void set_viewport(RID p_viewport);
	RID get_viewport() const;
	void set_viewport_size(const Vector2 &p_size);
	Vector2 get_viewport_size() const;
	bool set_sprite_texture(int64_t p_entity, const Ref<Texture2D> &p_texture);
	bool set_mesh(int64_t p_entity, const Ref<Mesh> &p_mesh);

	/* Physics context */
	void set_space_2d(RID p_space);
	RID get_space_2d() const;
	void set_space_3d(RID p_space);
	RID get_space_3d() const;
	bool set_shape_2d(int64_t p_entity, const Ref<Shape2D> &p_shape);
	bool set_shape_3d(int64_t p_entity, const Ref<Shape3D> &p_shape);
	// Drops the reference kept by the helpers above for the given resource.
	void release_resource(const Ref<Resource> &p_resource);

	/* Introspection / profiling */
	void set_name(const StringName &p_name);
	StringName get_name() const;
	int64_t get_archetype_count() const;
	Array get_archetype_info() const;
	int64_t get_memory_usage() const;
	Dictionary get_profile() const;
	// Profiles of every live world in the process (including native ones).
	static Array get_all_profiles();

	ECSWorld();
	~ECSWorld();
};

VARIANT_ENUM_CAST(ECSWorld::Stage);
VARIANT_ENUM_CAST(ECSWorld::Event);
VARIANT_BITFIELD_CAST(ECSWorld::ComponentFlags);

// Cached query over an ECSWorld. Keeps the world alive.
class ECSQuery : public RefCounted {
	GDCLASS(ECSQuery, RefCounted);

	friend class ECSWorld;

	Ref<ECSWorld> world;
	ecs::Query query;

protected:
	static void _bind_methods();

public:
	int64_t count();
	PackedInt64Array get_entities();
	bool matches(int64_t p_entity) const;
	// Calls p_callable(entity) for every matching entity.
	void for_each(const Callable &p_callable);
	// Bulk column access: returns a packed array (when the field type allows
	// it) with the value of the field for every matching entity, in the same
	// order as get_entities().
	Variant get_field_array(const Variant &p_component, const StringName &p_field);
	// Bulk column write, marks the component changed.
	int64_t set_field_array(const Variant &p_component, const StringName &p_field, const Variant &p_values);
	Array get_include() const;
	Array get_exclude() const;
	Ref<ECSWorld> get_world() const { return world; }
};
