/**************************************************************************/
/*  ecs_world.h                                                           */
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

#include "ecs_archetype.h"
#include "ecs_command_buffer.h"
#include "ecs_component.h"
#include "ecs_entity.h"
#include "ecs_event.h"
#include "ecs_sparse_storage.h"
#include "ecs_system.h"

#include "core/math/vector2.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"

namespace ecs {

class Query;
class Replicator;

struct EntityRecord {
	ArchetypeId archetype = INVALID_ARCHETYPE; // INVALID when the slot is free.
	uint32_t row = 0;
	uint32_t generation = 1;
};

// Observers receive structural events. Used by the network replicator and
// available to user code (e.g. to free server side resources).
struct WorldObserver {
	void (*on_entity_destroyed)(World &p_world, Entity p_entity, void *p_userdata) = nullptr;
	void (*on_component_added)(World &p_world, Entity p_entity, ComponentId p_component, void *p_userdata) = nullptr;
	void (*on_component_removed)(World &p_world, Entity p_entity, ComponentId p_component, void *p_userdata) = nullptr;
	void *userdata = nullptr;
};

// The World owns entities, component storage (archetype tables + sparse
// sets), systems and the network replicator. It is not a Node and does not
// depend on the SceneTree: it talks to the servers (rendering, physics,
// multiplayer peers) directly.
class World {
	friend class Query;
	friend class CommandBuffer;

	// Component registry.
	LocalVector<ComponentType *> component_types;
	HashMap<StringName, ComponentId> component_names;
	LocalVector<ComponentId> native_ids; // TypeIndex<T> -> ComponentId.
	LocalVector<SparseComponentStorage *> sparse_storages; // Indexed by ComponentId, null for table components.
	LocalVector<ComponentId> sparse_component_ids;
	ComponentMask sparse_mask;

	// Entities.
	LocalVector<EntityRecord> records;
	LocalVector<uint32_t> free_list;
	uint32_t alive_count = 0;
	Mutex reserve_mutex;
	uint32_t reserve_next = 0;
	LocalVector<Entity> reserved_entities;

	// Archetypes.
	LocalVector<Archetype *> archetypes;
	HashMap<ComponentMask, Archetype *, ECSBitSetHasher<MAX_COMPONENTS>> archetype_map;
	Archetype *root_archetype = nullptr;

	// Systems.
	struct Timing {
		uint64_t last_us = 0;
		uint64_t total_us = 0;
		uint64_t max_us = 0;
		uint32_t runs = 0;
		_FORCE_INLINE_ void record(uint64_t p_us) {
			last_us = p_us;
			total_us += p_us;
			max_us = MAX(max_us, p_us);
			runs++;
		}
	};
	struct SystemSlot {
		SystemDesc desc;
		bool valid = false;
		uint32_t last_run_tick = 0;
		SubscriptionId subscription = INVALID_SUBSCRIPTION;
		Timing timing;
	};
	struct Batch {
		LocalVector<SystemId> systems;
		bool parallel = false;
	};
	LocalVector<SystemSlot> systems;
	LocalVector<Batch> schedule[STAGE_MAX];
	bool schedule_dirty = true;
	Stage current_stage = STAGE_MAX;

	// Ticks and commands.
	uint32_t tick = 1;
	CommandBuffer command_buffer;

	// Events.
	struct SubscriptionSlot {
		SubscriptionDesc desc;
		bool valid = false;
		uint32_t insertion = 0;
		Timing timing;
	};
	LocalVector<SubscriptionSlot> subscriptions;
	LocalVector<SubscriptionId> subscription_order;
	bool subscriptions_dirty = false;
	uint32_t subscription_counter = 0;
	EventMask interest; // Union of every active subscription mask.
	LocalVector<StringName> event_names;
	HashMap<StringName, EventId> event_ids;
	Mutex event_queue_mutex;
	LocalVector<Event> event_queue;
	int dispatch_depth = 0;

	void _rebuild_subscription_order();
	void _update_interest();
	void _dispatch(const Event &p_event);
	void _run_system_event(SystemId p_id, const Event &p_event);
	static void _system_event_handler(World &p_world, const Event &p_event, void *p_userdata);
	void _register_builtin_events();

	LocalVector<WorldObserver> observers;

	struct CleanupCallback {
		void (*func)(World &p_world, void *p_userdata);
		void *userdata;
	};
	LocalVector<CleanupCallback> cleanup_callbacks;

	Replicator *replicator = nullptr;
	void *user_data = nullptr;
	StringName name;
	uint32_t serial = 0;

	// Profiling.
	uint64_t last_update_us = 0;
	uint64_t last_physics_us = 0;
	uint32_t events_dispatched = 0; // Since the last update().
	uint32_t events_last_frame = 0;

	// Rendering context used by the built-in systems.
	RID canvas;
	RID scenario;
	RID viewport;
	Size2 viewport_size;
	RID space_2d;
	RID space_3d;

	Archetype *_get_or_create_archetype(const ComponentMask &p_mask);
	Archetype *_archetype_with(Archetype *p_from, ComponentId p_component);
	Archetype *_archetype_without(Archetype *p_from, ComponentId p_component);
	void _move_entity(Entity p_entity, EntityRecord &p_record, Archetype *p_dst, ComponentId p_new_component, const void *p_init, void **r_new_component);
	void _materialize(Entity p_entity, Archetype *p_archetype);
	Entity _allocate_entity();
	void _rebuild_schedule();
	void _run_system(SystemId p_id, double p_delta);
	void _run_batch(Batch &p_batch, double p_delta);
	static void _run_batch_task(void *p_userdata, uint32_t p_index);
	ComponentId _register(ComponentType *p_type);
	void _register_builtin_components();

public:
	/* Component types */

	// Registers a component described by p_type (name, size, callbacks,
	// fields, flags). The World takes ownership of the description and
	// assigns `id`. Returns INVALID_COMPONENT on failure (duplicate name,
	// too many components).
	ComponentId register_component_type(ComponentType *p_type);

	// Registers a script-defined component from a {field_name: type} map.
	ComponentId register_script_component(const StringName &p_name, const Dictionary &p_fields, uint32_t p_flags = 0, String *r_error = nullptr);

	// Registers a native C++ component. Use T = void for tags.
	template <typename T>
	ComponentId register_component(const StringName &p_name, uint32_t p_flags = 0) {
		uint32_t tidx = TypeIndex<T>::get();
		if (tidx < native_ids.size() && native_ids[tidx] != INVALID_COMPONENT) {
			return native_ids[tidx];
		}
		ComponentType *t = memnew(ComponentType);
		t->name = p_name;
		t->flags = p_flags;
		ComponentTraits<T>::fill(*t);
		ComponentId id = register_component_type(t);
		if (id != INVALID_COMPONENT) {
			if (tidx >= native_ids.size()) {
				uint32_t old = native_ids.size();
				native_ids.resize(tidx + 1);
				for (uint32_t i = old; i < native_ids.size(); i++) {
					native_ids[i] = INVALID_COMPONENT;
				}
			}
			native_ids[tidx] = id;
		}
		return id;
	}

	template <typename T>
	_FORCE_INLINE_ ComponentId component_id() const {
		uint32_t tidx = TypeIndex<T>::get();
		return tidx < native_ids.size() ? native_ids[tidx] : INVALID_COMPONENT;
	}

	ComponentId find_component(const StringName &p_name) const {
		const ComponentId *id = component_names.getptr(p_name);
		return id ? *id : INVALID_COMPONENT;
	}
	_FORCE_INLINE_ const ComponentType *get_component_type(ComponentId p_id) const {
		return p_id < component_types.size() ? component_types[p_id] : nullptr;
	}
	// Mutable access, for attaching hooks after registration.
	_FORCE_INLINE_ ComponentType *get_component_type_mut(ComponentId p_id) {
		return p_id < component_types.size() ? component_types[p_id] : nullptr;
	}
	_FORCE_INLINE_ uint32_t get_component_type_count() const { return component_types.size(); }
	_FORCE_INLINE_ bool is_sparse_component(ComponentId p_id) const { return sparse_mask.test(p_id); }
	_FORCE_INLINE_ const ComponentMask &get_sparse_mask() const { return sparse_mask; }

	/* Entities */

	Entity create_entity();
	// Creates an entity directly inside the archetype described by p_mask
	// (table components only; sparse bits are added through the sparse sets).
	Entity create_entity(const ComponentMask &p_mask);
	// Thread safe: reserves an entity id which becomes alive when commands are flushed.
	Entity reserve_entity();
	bool destroy_entity(Entity p_entity);
	_FORCE_INLINE_ bool is_alive(Entity p_entity) const {
		uint32_t idx = p_entity.index();
		return p_entity.is_valid() && idx < records.size() && records[idx].generation == p_entity.generation() && records[idx].archetype != INVALID_ARCHETYPE;
	}
	_FORCE_INLINE_ uint32_t get_entity_count() const { return alive_count; }
	// Returns the live entity stored at a slot index, or a null entity.
	_FORCE_INLINE_ Entity get_entity_at_index(uint32_t p_index) const {
		if (p_index >= records.size() || records[p_index].archetype == INVALID_ARCHETYPE) {
			return Entity();
		}
		return Entity::make(p_index, records[p_index].generation);
	}
	// Destroys every entity (keeps registered components and systems).
	void clear_entities();

	_FORCE_INLINE_ const EntityRecord *get_record(Entity p_entity) const {
		return is_alive(p_entity) ? &records[p_entity.index()] : nullptr;
	}
	_FORCE_INLINE_ Archetype *get_entity_archetype(Entity p_entity) const {
		const EntityRecord *r = get_record(p_entity);
		return r ? archetypes[r->archetype] : nullptr;
	}
	// Full component set of the entity (table + sparse components).
	ComponentMask get_entity_mask(Entity p_entity) const;

	/* Components on entities */

	// Adds (or overwrites from p_init) a component and returns a pointer to it.
	void *add_component(Entity p_entity, ComponentId p_component, const void *p_init = nullptr);
	bool remove_component(Entity p_entity, ComponentId p_component);
	bool has_component(Entity p_entity, ComponentId p_component) const;
	// Read access (does not update the change tick).
	void *get_component(Entity p_entity, ComponentId p_component) const;
	// Write access, marks the component changed at the current tick.
	void *get_component_mut(Entity p_entity, ComponentId p_component);
	uint32_t get_component_change_tick(Entity p_entity, ComponentId p_component) const;
	void mark_changed(Entity p_entity, ComponentId p_component);

	template <typename T>
	_FORCE_INLINE_ T &add(Entity p_entity, const T &p_value) {
		if constexpr (std::is_empty_v<T>) {
			add_component(p_entity, component_id<T>(), nullptr);
			static T tag;
			return tag;
		} else {
			return *static_cast<T *>(add_component(p_entity, component_id<T>(), &p_value));
		}
	}
	template <typename T>
	_FORCE_INLINE_ T *add(Entity p_entity) {
		return static_cast<T *>(add_component(p_entity, component_id<T>(), nullptr));
	}
	template <typename T>
	_FORCE_INLINE_ bool remove(Entity p_entity) {
		return remove_component(p_entity, component_id<T>());
	}
	template <typename T>
	_FORCE_INLINE_ bool has(Entity p_entity) const {
		return has_component(p_entity, component_id<T>());
	}
	template <typename T>
	_FORCE_INLINE_ const T *get(Entity p_entity) const {
		return static_cast<const T *>(get_component(p_entity, component_id<T>()));
	}
	template <typename T>
	_FORCE_INLINE_ T *get_mut(Entity p_entity) {
		return static_cast<T *>(get_component_mut(p_entity, component_id<T>()));
	}

	_FORCE_INLINE_ SparseComponentStorage *get_sparse_storage(ComponentId p_component) const {
		return p_component < sparse_storages.size() ? sparse_storages[p_component] : nullptr;
	}

	/* Archetypes */

	_FORCE_INLINE_ uint32_t get_archetype_count() const { return archetypes.size(); }
	_FORCE_INLINE_ Archetype *get_archetype(ArchetypeId p_id) const { return archetypes[p_id]; }
	_FORCE_INLINE_ const LocalVector<Archetype *> &get_archetypes() const { return archetypes; }

	/* Systems */

	SystemId add_system(const SystemDesc &p_desc);
	bool remove_system(SystemId p_id);
	// Removes every system and subscription (drops their callables / user data).
	void clear_systems();
	SystemId find_system(const StringName &p_name) const;
	void set_system_enabled(SystemId p_id, bool p_enabled);
	bool is_system_enabled(SystemId p_id) const;
	_FORCE_INLINE_ uint32_t get_system_count() const { return systems.size(); }
	const SystemDesc *get_system(SystemId p_id) const;

	// Runs all frame stages (PRE_UPDATE, UPDATE, POST_UPDATE, RENDER).
	void update(double p_delta);
	// Runs all fixed stages (PRE_PHYSICS, PHYSICS, POST_PHYSICS, NETWORK) and
	// then the network replicator.
	void physics_update(double p_delta);
	void run_stage(Stage p_stage, double p_delta);
	_FORCE_INLINE_ Stage get_current_stage() const { return current_stage; }

	_FORCE_INLINE_ uint32_t get_tick() const { return tick; }
	// Advances the tick without running systems.
	_FORCE_INLINE_ void advance_tick() { tick++; }

	_FORCE_INLINE_ CommandBuffer &commands() { return command_buffer; }
	void flush_commands();

	/* Events */

	// Registers a user event (returns the existing id for a known name).
	EventId register_event(const StringName &p_name);
	EventId find_event(const StringName &p_name) const;
	StringName get_event_name(EventId p_event) const;
	static const char *get_builtin_event_name(EventId p_event);
	static EventId find_builtin_event(const String &p_name);
	_FORCE_INLINE_ uint32_t get_event_count() const { return event_names.size(); }

	SubscriptionId subscribe(const SubscriptionDesc &p_desc);
	bool unsubscribe(SubscriptionId p_id);
	void set_subscription_enabled(SubscriptionId p_id, bool p_enabled);
	const SubscriptionDesc *get_subscription(SubscriptionId p_id) const;
	_FORCE_INLINE_ uint32_t get_subscription_count() const { return subscriptions.size(); }
	// True when at least one subscription listens to the event (lets
	// emitters skip building events nobody wants).
	_FORCE_INLINE_ bool has_subscribers(EventId p_event) const { return p_event < MAX_EVENTS && interest.test(p_event); }

	// Delivers the event right away to every matching subscription. Main
	// thread only.
	void emit_event(const Event &p_event);
	// Thread safe: stores the event for the next flush_events() (which runs
	// after every system batch and at the end of each stage).
	void queue_event(const Event &p_event);
	void flush_events();

	// Script representation of an event.
	Dictionary event_to_dictionary(const Event &p_event) const;

	/* Observers */

	void add_observer(const WorldObserver &p_observer);
	void remove_observer(void *p_userdata);

	// Invoked (in registration order) when the world is destroyed, before
	// entities are cleared. Lets systems free their shared state.
	void add_cleanup_callback(void (*p_func)(World &p_world, void *p_userdata), void *p_userdata);

	/* Networking */

	Replicator *get_replicator();

	/* Misc */

	_FORCE_INLINE_ void set_user_data(void *p_data) { user_data = p_data; }
	_FORCE_INLINE_ void *get_user_data() const { return user_data; }
	_FORCE_INLINE_ void set_name(const StringName &p_name) { name = p_name; }
	_FORCE_INLINE_ const StringName &get_name() const { return name; }
	_FORCE_INLINE_ uint32_t get_serial() const { return serial; }

	/* Profiling */

	struct MemoryStats {
		uint64_t archetype_bytes = 0; // Tables, change ticks, entity lists.
		uint64_t sparse_bytes = 0; // Sparse component storages.
		uint64_t entity_bytes = 0; // Entity records and free list.
		uint64_t total() const { return archetype_bytes + sparse_bytes + entity_bytes; }
	};
	MemoryStats get_memory_usage() const;
	_FORCE_INLINE_ uint64_t get_last_update_usec() const { return last_update_us; }
	_FORCE_INLINE_ uint64_t get_last_physics_usec() const { return last_physics_us; }
	_FORCE_INLINE_ uint32_t get_events_last_frame() const { return events_last_frame; }
	// Snapshot for profilers: entity/archetype/memory counters, the systems
	// in execution order with timings, event subscriptions and archetypes.
	Dictionary get_profile();
	// Every live world of the process (thread safe copy).
	static void get_all_worlds(LocalVector<World *> &r_worlds);

	_FORCE_INLINE_ void set_canvas(RID p_canvas) { canvas = p_canvas; }
	_FORCE_INLINE_ RID get_canvas() const { return canvas; }
	_FORCE_INLINE_ void set_scenario(RID p_scenario) { scenario = p_scenario; }
	_FORCE_INLINE_ RID get_scenario() const { return scenario; }
	_FORCE_INLINE_ void set_viewport(RID p_viewport) { viewport = p_viewport; }
	_FORCE_INLINE_ RID get_viewport() const { return viewport; }
	_FORCE_INLINE_ void set_viewport_size(const Size2 &p_size) { viewport_size = p_size; }
	_FORCE_INLINE_ const Size2 &get_viewport_size() const { return viewport_size; }
	_FORCE_INLINE_ void set_space_2d(RID p_space) { space_2d = p_space; }
	_FORCE_INLINE_ RID get_space_2d() const { return space_2d; }
	_FORCE_INLINE_ void set_space_3d(RID p_space) { space_3d = p_space; }
	_FORCE_INLINE_ RID get_space_3d() const { return space_3d; }

	World();
	~World();

	World(const World &) = delete;
	World &operator=(const World &) = delete;
};

} // namespace ecs
