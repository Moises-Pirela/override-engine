/**************************************************************************/
/*  ecs_world.cpp                                                         */
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

#include "ecs_world.h"

#include "../net/ecs_replicator.h"
#include "ecs_query.h"

#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/variant/variant.h"

namespace ecs {

/* World registry */

static Mutex world_list_mutex;
static LocalVector<World *> world_list;
static uint32_t world_serial_counter = 0;

void World::get_all_worlds(LocalVector<World *> &r_worlds) {
	MutexLock lock(world_list_mutex);
	r_worlds = world_list;
}

/* Registration */

ComponentId World::_register(ComponentType *p_type) {
	ERR_FAIL_NULL_V(p_type, INVALID_COMPONENT);
	if (component_names.has(p_type->name)) {
		ERR_FAIL_V_MSG(INVALID_COMPONENT, vformat("A component named '%s' is already registered.", p_type->name));
	}
	if (component_types.size() >= MAX_COMPONENTS) {
		ERR_FAIL_V_MSG(INVALID_COMPONENT, vformat("Too many component types (max %d).", MAX_COMPONENTS));
	}
	if (p_type->size == 0) {
		p_type->flags |= COMPONENT_FLAG_TAG;
	}
	if (p_type->flags & COMPONENT_FLAG_OWNER_AUTHORITY) {
		p_type->flags |= COMPONENT_FLAG_REPLICATED;
	}
	ComponentId id = component_types.size();
	p_type->id = id;
	component_types.push_back(p_type);
	component_names.insert(p_type->name, id);
	sparse_storages.push_back(nullptr);
	if (p_type->is_sparse()) {
		sparse_storages[id] = memnew(SparseComponentStorage(p_type));
		sparse_mask.set(id);
		sparse_component_ids.push_back(id);
	}
	return id;
}

ComponentId World::register_component_type(ComponentType *p_type) {
	ComponentId id = _register(p_type);
	if (id == INVALID_COMPONENT) {
		memdelete(p_type);
	}
	return id;
}

ComponentId World::register_script_component(const StringName &p_name, const Dictionary &p_fields, uint32_t p_flags, String *r_error) {
	if (component_names.has(p_name)) {
		if (r_error) {
			*r_error = vformat("A component named '%s' is already registered.", p_name);
		}
		return INVALID_COMPONENT;
	}
	ComponentType *t = memnew(ComponentType);
	t->name = p_name;
	t->flags = p_flags;
	if (!ComponentType::build_script_layout(*t, p_fields, r_error)) {
		memdelete(t);
		return INVALID_COMPONENT;
	}
	return register_component_type(t);
}

/* Archetypes */

Archetype *World::_get_or_create_archetype(const ComponentMask &p_mask) {
	Archetype **existing = archetype_map.getptr(p_mask);
	if (existing) {
		return *existing;
	}
	LocalVector<const ComponentType *> types;
	p_mask.for_each([&](uint32_t p_bit) {
		types.push_back(component_types[p_bit]);
	});
	Archetype *a = memnew(Archetype(archetypes.size(), p_mask, types));
	archetypes.push_back(a);
	archetype_map.insert(p_mask, a);
	return a;
}

Archetype *World::_archetype_with(Archetype *p_from, ComponentId p_component) {
	Archetype *to = p_from->get_add_edge(p_component);
	if (!to) {
		ComponentMask m = p_from->get_mask();
		m.set(p_component);
		to = _get_or_create_archetype(m);
		p_from->set_add_edge(p_component, to);
		to->set_remove_edge(p_component, p_from);
	}
	return to;
}

Archetype *World::_archetype_without(Archetype *p_from, ComponentId p_component) {
	Archetype *to = p_from->get_remove_edge(p_component);
	if (!to) {
		ComponentMask m = p_from->get_mask();
		m.clear(p_component);
		to = _get_or_create_archetype(m);
		p_from->set_remove_edge(p_component, to);
		to->set_add_edge(p_component, p_from);
	}
	return to;
}

// Moves an entity between archetypes. Components present in both are moved;
// if p_new_component is valid it is constructed (from p_init when given) in
// the destination and its pointer returned in r_new_component.
void World::_move_entity(Entity p_entity, EntityRecord &p_record, Archetype *p_dst, ComponentId p_new_component, const void *p_init, void **r_new_component) {
	Archetype *src = archetypes[p_record.archetype];
	uint32_t src_row = p_record.row;
	uint32_t dst_row = p_dst->alloc_row(p_entity, tick);

	for (uint32_t c = 0; c < p_dst->get_column_count(); c++) {
		Archetype::Column &dcol = p_dst->get_column(c);
		ComponentId cid = dcol.type->id;
		void *dptr = p_dst->get(c, dst_row);
		int32_t sc = src->get_column_index(cid);
		if (sc >= 0) {
			dcol.type->move_construct(dptr, src->get(uint32_t(sc), src_row));
			dcol.change_ticks[dst_row] = src->get_change_tick(uint32_t(sc), src_row);
		} else {
			DEV_ASSERT(cid == p_new_component);
			if (p_init) {
				dcol.type->copy_construct(dptr, p_init);
			} else {
				dcol.type->construct_at(dptr);
			}
			if (r_new_component) {
				*r_new_component = dptr;
			}
		}
	}

	Entity moved = src->remove_row(src_row);
	if (moved.is_valid()) {
		records[moved.index()].row = src_row;
	}
	p_record.archetype = p_dst->get_id();
	p_record.row = dst_row;
}

/* Entities */

void World::_materialize(Entity p_entity, Archetype *p_archetype) {
	uint32_t idx = p_entity.index();
	if (idx >= records.size()) {
		uint32_t old = records.size();
		records.resize(idx + 1);
		for (uint32_t i = old; i < records.size(); i++) {
			records[i].archetype = INVALID_ARCHETYPE;
			records[i].generation = 1;
		}
	}
	EntityRecord &r = records[idx];
	DEV_ASSERT(r.archetype == INVALID_ARCHETYPE);
	r.generation = p_entity.generation();
	r.archetype = p_archetype->get_id();
	r.row = p_archetype->add_row(p_entity, tick);
	alive_count++;
	if (interest.test(EVENT_ENTITY_CREATE)) {
		queue_event(Event::for_entity(EVENT_ENTITY_CREATE, p_entity));
	}
}

Entity World::_allocate_entity() {
	MutexLock lock(reserve_mutex);
	if (!free_list.is_empty()) {
		uint32_t idx = free_list[free_list.size() - 1];
		free_list.resize(free_list.size() - 1);
		return Entity::make(idx, records[idx].generation);
	}
	uint32_t idx = reserve_next++;
	return Entity::make(idx, 1);
}

Entity World::reserve_entity() {
	Entity e = _allocate_entity();
	MutexLock lock(reserve_mutex);
	reserved_entities.push_back(e);
	return e;
}

Entity World::create_entity() {
	Entity e = _allocate_entity();
	_materialize(e, root_archetype);
	return e;
}

Entity World::create_entity(const ComponentMask &p_mask) {
	ERR_FAIL_COND_V_MSG(p_mask.intersects(sparse_mask), Entity(), "create_entity(mask) only accepts table components.");
	// The entity is materialized straight into its archetype: no root
	// archetype round trip, no per component migration.
	Archetype *dst = _get_or_create_archetype(p_mask);
	Entity e = _allocate_entity();
	_materialize(e, dst);
	if (p_mask.is_empty()) {
		return e;
	}
	const bool notify = interest.test(EVENT_COMPONENT_ADDED);
	for (uint32_t c = 0; c < dst->get_column_count(); c++) {
		const ComponentType *t = dst->get_column(c).type;
		if (t->on_add) {
			t->on_add(*this, e, dst->get(c, records[e.index()].row));
		}
		for (const WorldObserver &o : observers) {
			if (o.on_component_added) {
				o.on_component_added(*this, e, t->id, o.userdata);
			}
		}
		if (notify) {
			Event ev = Event::for_entity(EVENT_COMPONENT_ADDED, e);
			ev.component = t->id;
			queue_event(ev);
		}
	}
	return e;
}

bool World::destroy_entity(Entity p_entity) {
	if (!is_alive(p_entity)) {
		return false;
	}
	for (const WorldObserver &o : observers) {
		if (o.on_entity_destroyed) {
			o.on_entity_destroyed(*this, p_entity, o.userdata);
		}
	}
	if (interest.test(EVENT_ENTITY_DESTROY)) {
		emit_event(Event::for_entity(EVENT_ENTITY_DESTROY, p_entity));
		if (!is_alive(p_entity)) {
			return true; // A handler destroyed it already.
		}
	}
	EntityRecord &r = records[p_entity.index()];
	Archetype *a = archetypes[r.archetype];
	// Hooks.
	for (uint32_t c = 0; c < a->get_column_count(); c++) {
		const ComponentType *t = a->get_column(c).type;
		if (t->on_remove) {
			t->on_remove(*this, p_entity, a->get(c, r.row));
		}
	}
	for (ComponentId sid : sparse_component_ids) {
		SparseComponentStorage *s = sparse_storages[sid];
		if (s->has(p_entity.index())) {
			if (s->get_type()->on_remove) {
				s->get_type()->on_remove(*this, p_entity, s->get(p_entity.index()));
			}
			s->erase(p_entity.index());
		}
	}
	Entity moved = a->remove_row(r.row);
	if (moved.is_valid()) {
		records[moved.index()].row = r.row;
	}
	r.archetype = INVALID_ARCHETYPE;
	r.generation++;
	if (r.generation == 0) {
		r.generation = 1;
	}
	{
		MutexLock lock(reserve_mutex);
		free_list.push_back(p_entity.index());
	}
	alive_count--;
	return true;
}

void World::clear_entities() {
	for (uint32_t i = 0; i < records.size(); i++) {
		if (records[i].archetype != INVALID_ARCHETYPE) {
			destroy_entity(Entity::make(i, records[i].generation));
		}
	}
}

ComponentMask World::get_entity_mask(Entity p_entity) const {
	ComponentMask m;
	const EntityRecord *r = get_record(p_entity);
	if (!r) {
		return m;
	}
	m = archetypes[r->archetype]->get_mask();
	for (ComponentId sid : sparse_component_ids) {
		if (sparse_storages[sid]->has(p_entity.index())) {
			m.set(sid);
		}
	}
	return m;
}

/* Components */

void *World::add_component(Entity p_entity, ComponentId p_component, const void *p_init) {
	ERR_FAIL_COND_V_MSG(!is_alive(p_entity), nullptr, "Entity is not alive.");
	const ComponentType *t = get_component_type(p_component);
	ERR_FAIL_NULL_V_MSG(t, nullptr, "Invalid component id.");

	EntityRecord &r = records[p_entity.index()];
	void *ptr = nullptr;
	bool added = false;

	if (t->is_sparse()) {
		SparseComponentStorage *s = sparse_storages[p_component];
		added = !s->has(p_entity.index());
		ptr = s->insert(p_entity.index(), p_init, tick);
	} else {
		Archetype *a = archetypes[r.archetype];
		int32_t col = a->get_column_index(p_component);
		if (col >= 0) {
			ptr = a->get(uint32_t(col), r.row);
			if (p_init) {
				t->copy_assign(ptr, p_init);
			}
			a->mark_changed(uint32_t(col), r.row, tick);
		} else {
			Archetype *dst = _archetype_with(a, p_component);
			_move_entity(p_entity, r, dst, p_component, p_init, &ptr);
			added = true;
		}
	}

	if (added) {
		if (t->on_add) {
			t->on_add(*this, p_entity, ptr);
		}
		for (const WorldObserver &o : observers) {
			if (o.on_component_added) {
				o.on_component_added(*this, p_entity, p_component, o.userdata);
			}
		}
		if (interest.test(EVENT_COMPONENT_ADDED)) {
			Event ev = Event::for_entity(EVENT_COMPONENT_ADDED, p_entity);
			ev.component = p_component;
			queue_event(ev);
		}
	}
	// Hooks may have moved the entity; refetch for safety when sparse hooks
	// or observers mutate the world.
	if (t->is_sparse()) {
		return sparse_storages[p_component]->get(p_entity.index());
	}
	const EntityRecord &r2 = records[p_entity.index()];
	Archetype *a2 = archetypes[r2.archetype];
	int32_t col2 = a2->get_column_index(p_component);
	return col2 >= 0 ? a2->get(uint32_t(col2), r2.row) : nullptr;
}

bool World::remove_component(Entity p_entity, ComponentId p_component) {
	if (!is_alive(p_entity)) {
		return false;
	}
	const ComponentType *t = get_component_type(p_component);
	ERR_FAIL_NULL_V(t, false);

	EntityRecord &r = records[p_entity.index()];
	if (t->is_sparse()) {
		SparseComponentStorage *s = sparse_storages[p_component];
		if (!s->has(p_entity.index())) {
			return false;
		}
		if (interest.test(EVENT_COMPONENT_REMOVED)) {
			Event ev = Event::for_entity(EVENT_COMPONENT_REMOVED, p_entity);
			ev.component = p_component;
			emit_event(ev);
			if (!s->has(p_entity.index())) {
				return true;
			}
		}
		if (t->on_remove) {
			t->on_remove(*this, p_entity, s->get(p_entity.index()));
		}
		for (const WorldObserver &o : observers) {
			if (o.on_component_removed) {
				o.on_component_removed(*this, p_entity, p_component, o.userdata);
			}
		}
		return s->erase(p_entity.index());
	}

	Archetype *a = archetypes[r.archetype];
	int32_t col = a->get_column_index(p_component);
	if (col < 0) {
		return false;
	}
	if (interest.test(EVENT_COMPONENT_REMOVED)) {
		Event ev = Event::for_entity(EVENT_COMPONENT_REMOVED, p_entity);
		ev.component = p_component;
		emit_event(ev);
		a = archetypes[r.archetype];
		col = a->get_column_index(p_component);
		if (col < 0) {
			return true;
		}
	}
	if (t->on_remove) {
		t->on_remove(*this, p_entity, a->get(uint32_t(col), r.row));
	}
	for (const WorldObserver &o : observers) {
		if (o.on_component_removed) {
			o.on_component_removed(*this, p_entity, p_component, o.userdata);
		}
	}
	// Re-read in case a hook moved the entity.
	a = archetypes[r.archetype];
	if (a->get_column_index(p_component) < 0) {
		return true;
	}
	Archetype *dst = _archetype_without(a, p_component);
	_move_entity(p_entity, r, dst, INVALID_COMPONENT, nullptr, nullptr);
	return true;
}

bool World::has_component(Entity p_entity, ComponentId p_component) const {
	const EntityRecord *r = get_record(p_entity);
	if (!r || p_component >= component_types.size()) {
		return false;
	}
	if (sparse_mask.test(p_component)) {
		return sparse_storages[p_component]->has(p_entity.index());
	}
	return archetypes[r->archetype]->has(p_component);
}

void *World::get_component(Entity p_entity, ComponentId p_component) const {
	const EntityRecord *r = get_record(p_entity);
	if (!r || p_component >= component_types.size()) {
		return nullptr;
	}
	if (sparse_mask.test(p_component)) {
		return sparse_storages[p_component]->get(p_entity.index());
	}
	Archetype *a = archetypes[r->archetype];
	int32_t col = a->get_column_index(p_component);
	return col < 0 ? nullptr : a->get(uint32_t(col), r->row);
}

void *World::get_component_mut(Entity p_entity, ComponentId p_component) {
	const EntityRecord *r = get_record(p_entity);
	if (!r || p_component >= component_types.size()) {
		return nullptr;
	}
	if (sparse_mask.test(p_component)) {
		SparseComponentStorage *s = sparse_storages[p_component];
		s->mark_changed(p_entity.index(), tick);
		return s->get(p_entity.index());
	}
	Archetype *a = archetypes[r->archetype];
	int32_t col = a->get_column_index(p_component);
	if (col < 0) {
		return nullptr;
	}
	a->mark_changed(uint32_t(col), r->row, tick);
	return a->get(uint32_t(col), r->row);
}

uint32_t World::get_component_change_tick(Entity p_entity, ComponentId p_component) const {
	const EntityRecord *r = get_record(p_entity);
	if (!r || p_component >= component_types.size()) {
		return 0;
	}
	if (sparse_mask.test(p_component)) {
		return sparse_storages[p_component]->get_change_tick(p_entity.index());
	}
	Archetype *a = archetypes[r->archetype];
	int32_t col = a->get_column_index(p_component);
	return col < 0 ? 0 : a->get_change_tick(uint32_t(col), r->row);
}

void World::mark_changed(Entity p_entity, ComponentId p_component) {
	get_component_mut(p_entity, p_component);
}

/* Systems */

SystemId World::add_system(const SystemDesc &p_desc) {
	const bool has_stage_entry = p_desc.func != nullptr || p_desc.callable.is_valid();
	const bool has_event_entry = !p_desc.events.is_empty() && (p_desc.event_func != nullptr || p_desc.event_callable.is_valid());
	ERR_FAIL_COND_V_MSG(!has_stage_entry && !has_event_entry, INVALID_SYSTEM, "A system needs a stage function / Callable, or an event mask with an event handler.");
	ERR_FAIL_COND_V(p_desc.stage >= STAGE_MAX, INVALID_SYSTEM);
	SystemId id = INVALID_SYSTEM;
	for (uint32_t i = 0; i < systems.size(); i++) {
		if (!systems[i].valid) {
			id = i;
			break;
		}
	}
	if (id == INVALID_SYSTEM) {
		id = systems.size();
		systems.push_back(SystemSlot());
	}
	SystemSlot &slot = systems[id];
	slot.desc = p_desc;
	slot.valid = true;
	slot.last_run_tick = 0;
	slot.subscription = INVALID_SUBSCRIPTION;
	if (slot.desc.func == nullptr) {
		slot.desc.thread_safe = false; // Script callables always run on the main thread.
	}
	if (!has_stage_entry) {
		slot.desc.stage = STAGE_MAX; // Event-only system: never scheduled.
	}
	if (has_event_entry) {
		SubscriptionDesc sub;
		sub.name = p_desc.name;
		sub.events = p_desc.events;
		sub.filter = p_desc.event_filter;
		sub.order = p_desc.order;
		sub.func = _system_event_handler;
		sub.userdata = (void *)(uintptr_t)id;
		slot.subscription = subscribe(sub);
	}
	schedule_dirty = true;
	return id;
}

bool World::remove_system(SystemId p_id) {
	if (p_id >= systems.size() || !systems[p_id].valid) {
		return false;
	}
	if (systems[p_id].subscription != INVALID_SUBSCRIPTION) {
		unsubscribe(systems[p_id].subscription);
	}
	systems[p_id].valid = false;
	systems[p_id].desc = SystemDesc();
	systems[p_id].subscription = INVALID_SUBSCRIPTION;
	schedule_dirty = true;
	return true;
}

void World::clear_systems() {
	for (uint32_t i = 0; i < systems.size(); i++) {
		if (systems[i].valid) {
			remove_system(i);
		}
	}
	for (uint32_t i = 0; i < subscriptions.size(); i++) {
		if (subscriptions[i].valid) {
			unsubscribe(i);
		}
	}
	systems.clear();
	subscriptions.clear();
	subscription_order.clear();
	schedule_dirty = true;
}

SystemId World::find_system(const StringName &p_name) const {
	for (uint32_t i = 0; i < systems.size(); i++) {
		if (systems[i].valid && systems[i].desc.name == p_name) {
			return i;
		}
	}
	return INVALID_SYSTEM;
}

void World::set_system_enabled(SystemId p_id, bool p_enabled) {
	ERR_FAIL_COND(p_id >= systems.size() || !systems[p_id].valid);
	systems[p_id].desc.enabled = p_enabled;
	if (systems[p_id].subscription != INVALID_SUBSCRIPTION) {
		set_subscription_enabled(systems[p_id].subscription, p_enabled);
	}
}

bool World::is_system_enabled(SystemId p_id) const {
	if (p_id >= systems.size() || !systems[p_id].valid) {
		return false;
	}
	return systems[p_id].desc.enabled;
}

const SystemDesc *World::get_system(SystemId p_id) const {
	if (p_id >= systems.size() || !systems[p_id].valid) {
		return nullptr;
	}
	return &systems[p_id].desc;
}

struct SystemSortKey {
	int32_t order;
	uint32_t insertion;
	SystemId id;
	bool operator<(const SystemSortKey &p_o) const {
		if (order != p_o.order) {
			return order < p_o.order;
		}
		return insertion < p_o.insertion;
	}
};

void World::_rebuild_schedule() {
	for (uint32_t s = 0; s < STAGE_MAX; s++) {
		schedule[s].clear();
		LocalVector<SystemSortKey> keys;
		for (uint32_t i = 0; i < systems.size(); i++) {
			if (systems[i].valid && systems[i].desc.stage == s) {
				keys.push_back({ systems[i].desc.order, i, i });
			}
		}
		keys.sort();

		// Greedy batching: systems with the same `order` are packed into
		// batches of mutually non conflicting systems. A system conflicts
		// with a batch if it writes something the batch reads or writes, or
		// reads something the batch writes.
		uint32_t i = 0;
		while (i < keys.size()) {
			int32_t order = keys[i].order;
			LocalVector<Batch> group;
			LocalVector<ComponentMask> group_reads;
			LocalVector<ComponentMask> group_writes;
			while (i < keys.size() && keys[i].order == order) {
				const SystemDesc &d = systems[keys[i].id].desc;
				bool placed = false;
				if (d.thread_safe) {
					// Systems of a batch are unordered, so conflicts with
					// any earlier batch of the group also prevent placement
					// there (to preserve insertion order semantics we only
					// ever append to the last batch or open a new one).
					if (group.size() && group[group.size() - 1].parallel) {
						uint32_t b = group.size() - 1;
						bool conflict = d.writes.intersects(group_reads[b]) || d.writes.intersects(group_writes[b]) || d.reads.intersects(group_writes[b]);
						if (!conflict) {
							group[b].systems.push_back(keys[i].id);
							group_reads[b] |= d.reads;
							group_writes[b] |= d.writes;
							placed = true;
						}
					}
				}
				if (!placed) {
					Batch b;
					b.systems.push_back(keys[i].id);
					b.parallel = d.thread_safe;
					group.push_back(b);
					group_reads.push_back(d.reads);
					group_writes.push_back(d.writes);
				}
				i++;
			}
			for (const Batch &b : group) {
				schedule[s].push_back(b);
			}
		}
	}
	schedule_dirty = false;
}

void World::_run_system(SystemId p_id, double p_delta) {
	// Copy what is needed: a system may add systems and reallocate `systems`.
	const SystemSlot &slot = systems[p_id];
	if (!slot.valid || !slot.desc.enabled) {
		return;
	}
	SystemFunc func = slot.desc.func;
	Callable callable = slot.desc.callable;
	StringName name = slot.desc.name;
	SystemContext ctx;
	ctx.world = this;
	ctx.delta = p_delta;
	ctx.tick = tick;
	ctx.last_run_tick = slot.last_run_tick;
	ctx.userdata = slot.desc.userdata;
	ctx.commands = &command_buffer;
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	if (func) {
		func(ctx);
	} else if (callable.is_valid()) {
		Variant delta = p_delta;
		const Variant *args[1] = { &delta };
		Variant ret;
		Callable::CallError ce;
		callable.callp(args, 1, ret, ce);
		if (ce.error != Callable::CallError::CALL_OK) {
			ERR_PRINT(vformat("Error calling ECS system '%s': %s", name, Variant::get_callable_error_text(callable, args, 1, ce)));
		}
	}
	if (p_id < systems.size() && systems[p_id].valid) {
		systems[p_id].last_run_tick = tick;
		systems[p_id].timing.record(OS::get_singleton()->get_ticks_usec() - start);
	}
}

struct BatchTask {
	World *world;
	const SystemId *systems;
	double delta;
};

void World::_run_batch_task(void *p_userdata, uint32_t p_index) {
	BatchTask *t = (BatchTask *)p_userdata;
	t->world->_run_system(t->systems[p_index], t->delta);
}

void World::_run_batch(Batch &p_batch, double p_delta) {
	uint32_t n = p_batch.systems.size();
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	if (n == 1 || !p_batch.parallel || pool == nullptr || pool->get_thread_count() <= 1) {
		for (uint32_t i = 0; i < n; i++) {
			_run_system(p_batch.systems[i], p_delta);
		}
		return;
	}
	BatchTask task;
	task.world = this;
	task.systems = p_batch.systems.ptr();
	task.delta = p_delta;
	WorkerThreadPool::GroupID group = pool->add_native_group_task(&World::_run_batch_task, &task, int(n), -1, true, "ECSSystems");
	pool->wait_for_group_task_completion(group);
}

void World::run_stage(Stage p_stage, double p_delta) {
	ERR_FAIL_COND(p_stage >= STAGE_MAX);
	if (schedule_dirty) {
		_rebuild_schedule();
	}
	Stage prev = current_stage;
	current_stage = p_stage;
	flush_commands();
	LocalVector<Batch> &batches = schedule[p_stage];
	for (uint32_t b = 0; b < batches.size(); b++) {
		_run_batch(batches[b], p_delta);
		flush_commands();
		flush_events();
	}
	// Lifecycle event of the stage (Update / FixedUpdate / LateUpdate...).
	static const EventId stage_events[STAGE_MAX] = {
		EVENT_PRE_UPDATE, EVENT_UPDATE, EVENT_LATE_UPDATE, EVENT_RENDER,
		EVENT_PRE_FIXED_UPDATE, EVENT_FIXED_UPDATE, EVENT_LATE_FIXED_UPDATE, INVALID_EVENT
	};
	EventId stage_event = stage_events[p_stage];
	if (stage_event != INVALID_EVENT && interest.test(stage_event)) {
		Event ev(stage_event);
		ev.delta = p_delta;
		emit_event(ev);
		flush_commands();
		flush_events();
	}
	current_stage = prev;
}

// The tick is advanced *after* each update so that changes made between
// updates (e.g. from scripts) are attributed to the next tick and are
// reliably picked up by change detection (`change_tick > last_seen_tick`).
void World::update(double p_delta) {
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	events_last_frame = events_dispatched;
	events_dispatched = 0;
	flush_commands();
	flush_events();
	run_stage(STAGE_PRE_UPDATE, p_delta);
	run_stage(STAGE_UPDATE, p_delta);
	run_stage(STAGE_POST_UPDATE, p_delta);
	run_stage(STAGE_RENDER, p_delta);
	tick++;
	last_update_us = OS::get_singleton()->get_ticks_usec() - start;
}

void World::physics_update(double p_delta) {
	const uint64_t start = OS::get_singleton()->get_ticks_usec();
	flush_commands();
	flush_events();
	if (replicator) {
		replicator->poll();
		flush_events();
	}
	run_stage(STAGE_PRE_PHYSICS, p_delta);
	run_stage(STAGE_PHYSICS, p_delta);
	run_stage(STAGE_POST_PHYSICS, p_delta);
	run_stage(STAGE_NETWORK, p_delta);
	if (replicator) {
		replicator->sync();
	}
	tick++;
	last_physics_us = OS::get_singleton()->get_ticks_usec() - start;
}

void World::flush_commands() {
	LocalVector<Entity> pending;
	{
		MutexLock lock(reserve_mutex);
		pending = reserved_entities;
		reserved_entities.clear();
	}
	for (const Entity &e : pending) {
		_materialize(e, root_archetype);
	}
	if (!command_buffer.is_empty()) {
		command_buffer.flush(*this);
	}
}

/* Observers */

void World::add_observer(const WorldObserver &p_observer) {
	observers.push_back(p_observer);
}

void World::remove_observer(void *p_userdata) {
	for (uint32_t i = 0; i < observers.size(); i++) {
		if (observers[i].userdata == p_userdata) {
			observers.remove_at(i);
			return;
		}
	}
}

void World::add_cleanup_callback(void (*p_func)(World &p_world, void *p_userdata), void *p_userdata) {
	CleanupCallback c;
	c.func = p_func;
	c.userdata = p_userdata;
	cleanup_callbacks.push_back(c);
}

/* Networking */

Replicator *World::get_replicator() {
	if (!replicator) {
		replicator = memnew(Replicator(this));
	}
	return replicator;
}

/* Lifecycle */

World::World() {
	{
		MutexLock lock(world_list_mutex);
		serial = ++world_serial_counter;
		world_list.push_back(this);
	}
	root_archetype = _get_or_create_archetype(ComponentMask());
	_register_builtin_events();
	_register_builtin_components();
}

World::~World() {
	{
		MutexLock lock(world_list_mutex);
		world_list.erase(this);
	}
	if (replicator) {
		memdelete(replicator);
		replicator = nullptr;
	}
	// Systems go first so their cleanup callbacks can still inspect entities.
	systems.clear();
	subscriptions.clear();
	subscription_order.clear();
	interest.clear_all();
	event_queue.clear();
	for (const CleanupCallback &c : cleanup_callbacks) {
		c.func(*this, c.userdata);
	}
	cleanup_callbacks.clear();
	clear_entities();
	flush_commands();
	for (Archetype *a : archetypes) {
		memdelete(a);
	}
	for (SparseComponentStorage *s : sparse_storages) {
		if (s) {
			memdelete(s);
		}
	}
	for (ComponentType *t : component_types) {
		memdelete(t);
	}
}

/* Events */

static const char *builtin_event_names[EVENT_BUILTIN_MAX] = {
	"entity_create", "entity_destroy", "component_added", "component_removed",
	"pre_update", "update", "late_update", "render",
	"pre_fixed_update", "fixed_update", "late_fixed_update",
	"peer_connected", "peer_disconnected", "entity_spawned", "entity_despawned", "authority_changed"
};

const char *World::get_builtin_event_name(EventId p_event) {
	return p_event < EVENT_BUILTIN_MAX ? builtin_event_names[p_event] : nullptr;
}

EventId World::find_builtin_event(const String &p_name) {
	for (uint32_t i = 0; i < EVENT_BUILTIN_MAX; i++) {
		if (p_name == builtin_event_names[i]) {
			return i;
		}
	}
	return INVALID_EVENT;
}

void World::_register_builtin_events() {
	event_names.resize(EVENT_CUSTOM_BASE);
	for (uint32_t i = 0; i < EVENT_CUSTOM_BASE; i++) {
		if (i < EVENT_BUILTIN_MAX) {
			event_names[i] = StringName(builtin_event_names[i]);
			event_ids.insert(event_names[i], i);
		}
	}
}

EventId World::register_event(const StringName &p_name) {
	ERR_FAIL_COND_V_MSG(p_name == StringName(), INVALID_EVENT, "Event name can't be empty.");
	const EventId *existing = event_ids.getptr(p_name);
	if (existing) {
		return *existing;
	}
	ERR_FAIL_COND_V_MSG(event_names.size() >= MAX_EVENTS, INVALID_EVENT, vformat("Too many events (max %d).", MAX_EVENTS));
	EventId id = event_names.size();
	event_names.push_back(p_name);
	event_ids.insert(p_name, id);
	return id;
}

EventId World::find_event(const StringName &p_name) const {
	const EventId *id = event_ids.getptr(p_name);
	return id ? *id : INVALID_EVENT;
}

StringName World::get_event_name(EventId p_event) const {
	return p_event < event_names.size() ? event_names[p_event] : StringName();
}

void World::_update_interest() {
	interest.clear_all();
	for (const SubscriptionSlot &s : subscriptions) {
		if (s.valid && s.desc.enabled) {
			interest |= s.desc.events;
		}
	}
}

struct SubscriptionSortKey {
	int32_t order;
	uint32_t insertion;
	SubscriptionId id;
	bool operator<(const SubscriptionSortKey &p_o) const {
		if (order != p_o.order) {
			return order < p_o.order;
		}
		return insertion < p_o.insertion;
	}
};

void World::_rebuild_subscription_order() {
	LocalVector<SubscriptionSortKey> keys;
	for (uint32_t i = 0; i < subscriptions.size(); i++) {
		if (subscriptions[i].valid) {
			keys.push_back({ subscriptions[i].desc.order, subscriptions[i].insertion, i });
		}
	}
	keys.sort();
	subscription_order.clear();
	for (const SubscriptionSortKey &k : keys) {
		subscription_order.push_back(k.id);
	}
	subscriptions_dirty = false;
}

SubscriptionId World::subscribe(const SubscriptionDesc &p_desc) {
	ERR_FAIL_COND_V_MSG(p_desc.events.is_empty(), INVALID_SUBSCRIPTION, "A subscription needs at least one event.");
	ERR_FAIL_COND_V_MSG(p_desc.func == nullptr && !p_desc.callable.is_valid(), INVALID_SUBSCRIPTION, "A subscription needs a native handler or a valid Callable.");
	SubscriptionId id = INVALID_SUBSCRIPTION;
	for (uint32_t i = 0; i < subscriptions.size(); i++) {
		if (!subscriptions[i].valid) {
			id = i;
			break;
		}
	}
	if (id == INVALID_SUBSCRIPTION) {
		id = subscriptions.size();
		subscriptions.push_back(SubscriptionSlot());
	}
	SubscriptionSlot &slot = subscriptions[id];
	slot.desc = p_desc;
	slot.valid = true;
	slot.insertion = subscription_counter++;
	subscriptions_dirty = true;
	_update_interest();
	return id;
}

bool World::unsubscribe(SubscriptionId p_id) {
	if (p_id >= subscriptions.size() || !subscriptions[p_id].valid) {
		return false;
	}
	subscriptions[p_id].valid = false;
	subscriptions[p_id].desc = SubscriptionDesc();
	subscriptions_dirty = true;
	_update_interest();
	return true;
}

void World::set_subscription_enabled(SubscriptionId p_id, bool p_enabled) {
	ERR_FAIL_COND(p_id >= subscriptions.size() || !subscriptions[p_id].valid);
	subscriptions[p_id].desc.enabled = p_enabled;
	_update_interest();
}

const SubscriptionDesc *World::get_subscription(SubscriptionId p_id) const {
	if (p_id >= subscriptions.size() || !subscriptions[p_id].valid) {
		return nullptr;
	}
	return &subscriptions[p_id].desc;
}

Dictionary World::event_to_dictionary(const Event &p_event) const {
	Dictionary d;
	d["event"] = int64_t(p_event.id);
	d["name"] = get_event_name(p_event.id);
	d["entity"] = int64_t(p_event.entity.id);
	const ComponentType *t = get_component_type(p_event.component);
	d["component"] = t ? t->name : StringName();
	d["delta"] = p_event.delta;
	d["peer"] = int64_t(p_event.peer);
	d["net_id"] = int64_t(p_event.net_id);
	d["payload"] = p_event.payload;
	return d;
}

void World::_dispatch(const Event &p_event) {
	if (p_event.id >= MAX_EVENTS || !interest.test(p_event.id)) {
		return;
	}
	if (dispatch_depth == 0 && subscriptions_dirty) {
		_rebuild_subscription_order();
	}
	dispatch_depth++;
	Dictionary script_event;
	bool script_event_built = false;
	const bool has_entity = p_event.entity.is_valid();
	ComponentMask entity_mask;
	bool entity_mask_built = false;

	for (uint32_t i = 0; i < subscription_order.size(); i++) {
		SubscriptionId sid = subscription_order[i];
		if (sid >= subscriptions.size() || !subscriptions[sid].valid) {
			continue;
		}
		const SubscriptionDesc &sub = subscriptions[sid].desc;
		if (!sub.enabled || !sub.events.test(p_event.id)) {
			continue;
		}
		// Entity / component restrictions only apply to entity events; global
		// events (update phases, peers...) are delivered to every subscriber.
		if (has_entity && sub.entity.is_valid() && sub.entity != p_event.entity) {
			continue;
		}
		if (has_entity && !sub.filter.is_empty()) {
			if (!entity_mask_built) {
				entity_mask = get_entity_mask(p_event.entity);
				entity_mask_built = true;
			}
			if (!entity_mask.contains_all(sub.filter)) {
				continue;
			}
		}
		const uint64_t start = OS::get_singleton()->get_ticks_usec();
		if (sub.func) {
			sub.func(*this, p_event, sub.userdata);
		} else if (sub.callable.is_valid()) {
			if (!script_event_built) {
				script_event = event_to_dictionary(p_event);
				script_event_built = true;
			}
			Variant arg = script_event;
			const Variant *args[1] = { &arg };
			Variant ret;
			Callable::CallError ce;
			sub.callable.callp(args, 1, ret, ce);
			if (ce.error != Callable::CallError::CALL_OK) {
				ERR_PRINT(vformat("Error calling ECS event handler '%s' for event '%s': %s", sub.name, get_event_name(p_event.id), Variant::get_callable_error_text(sub.callable, args, 1, ce)));
			}
		}
		if (sid < subscriptions.size() && subscriptions[sid].valid) {
			subscriptions[sid].timing.record(OS::get_singleton()->get_ticks_usec() - start);
		}
	}
	events_dispatched++;
	dispatch_depth--;
}

void World::emit_event(const Event &p_event) {
	_dispatch(p_event);
}

void World::queue_event(const Event &p_event) {
	MutexLock lock(event_queue_mutex);
	event_queue.push_back(p_event);
}

void World::flush_events() {
	// Handlers may queue more events; loop until the queue is drained.
	while (true) {
		LocalVector<Event> pending;
		{
			MutexLock lock(event_queue_mutex);
			if (event_queue.is_empty()) {
				return;
			}
			pending = std::move(event_queue);
			event_queue.clear();
		}
		for (const Event &ev : pending) {
			// Queued entity events are dropped if the entity died meanwhile.
			if (ev.entity.is_valid() && !is_alive(ev.entity)) {
				continue;
			}
			_dispatch(ev);
		}
	}
}

void World::_system_event_handler(World &p_world, const Event &p_event, void *p_userdata) {
	p_world._run_system_event(SystemId(uintptr_t(p_userdata)), p_event);
}

void World::_run_system_event(SystemId p_id, const Event &p_event) {
	if (p_id >= systems.size() || !systems[p_id].valid || !systems[p_id].desc.enabled) {
		return;
	}
	const SystemSlot &slot = systems[p_id];
	SystemEventFunc func = slot.desc.event_func;
	Callable callable = slot.desc.event_callable;
	StringName name = slot.desc.name;
	SystemContext ctx;
	ctx.world = this;
	ctx.delta = p_event.delta;
	ctx.tick = tick;
	ctx.last_run_tick = slot.last_run_tick;
	ctx.userdata = slot.desc.userdata;
	ctx.commands = &command_buffer;
	if (func) {
		func(ctx, p_event);
	} else if (callable.is_valid()) {
		Variant arg = event_to_dictionary(p_event);
		const Variant *args[1] = { &arg };
		Variant ret;
		Callable::CallError ce;
		callable.callp(args, 1, ret, ce);
		if (ce.error != Callable::CallError::CALL_OK) {
			ERR_PRINT(vformat("Error calling ECS system '%s' event handler: %s", name, Variant::get_callable_error_text(callable, args, 1, ce)));
		}
	}
	if (p_id < systems.size() && systems[p_id].valid) {
		systems[p_id].last_run_tick = tick;
	}
}

/* Profiling */

World::MemoryStats World::get_memory_usage() const {
	MemoryStats m;
	for (const Archetype *a : archetypes) {
		m.archetype_bytes += a->get_memory_usage();
	}
	for (const SparseComponentStorage *s : sparse_storages) {
		if (s) {
			m.sparse_bytes += s->get_memory_usage();
		}
	}
	m.entity_bytes = uint64_t(records.size()) * sizeof(EntityRecord) + uint64_t(free_list.size()) * sizeof(uint32_t);
	return m;
}

static const char *stage_names[STAGE_MAX] = { "pre_update", "update", "post_update", "render", "pre_physics", "physics", "post_physics", "network" };

Dictionary World::get_profile() {
	if (schedule_dirty) {
		_rebuild_schedule();
	}
	if (subscriptions_dirty && dispatch_depth == 0) {
		_rebuild_subscription_order();
	}
	Dictionary d;
	d["serial"] = int64_t(serial);
	d["name"] = name == StringName() ? StringName(vformat("ECSWorld %d", serial)) : name;
	d["entities"] = int64_t(alive_count);
	d["archetypes"] = int64_t(archetypes.size());
	d["components"] = int64_t(component_types.size());
	d["tick"] = int64_t(tick);
	d["update_us"] = int64_t(last_update_us);
	d["physics_us"] = int64_t(last_physics_us);
	d["events_per_frame"] = int64_t(events_last_frame);
	MemoryStats m = get_memory_usage();
	Dictionary mem;
	mem["total"] = int64_t(m.total());
	mem["archetypes"] = int64_t(m.archetype_bytes);
	mem["sparse"] = int64_t(m.sparse_bytes);
	mem["entities"] = int64_t(m.entity_bytes);
	d["memory"] = mem;

	// Systems and subscriptions in execution order.
	Array list;
	int index = 0;
	uint32_t system_count = 0;
	uint32_t active_count = 0;
	auto timing_to = [](Dictionary &p_row, const Timing &p_t) {
		p_row["last_us"] = int64_t(p_t.last_us);
		p_row["avg_us"] = p_t.runs ? double(p_t.total_us) / double(p_t.runs) : 0.0;
		p_row["max_us"] = int64_t(p_t.max_us);
		p_row["runs"] = int64_t(p_t.runs);
	};
	auto events_to = [&](const EventMask &p_mask) {
		Array names;
		p_mask.for_each([&](uint32_t p_bit) { names.push_back(get_event_name(p_bit)); });
		return names;
	};
	auto subscription_row = [&](SubscriptionId p_sid, const String &p_stage) {
		const SubscriptionSlot &slot = subscriptions[p_sid];
		Dictionary row;
		row["index"] = index++;
		bool from_system = slot.desc.func == _system_event_handler;
		SystemId sys_id = from_system ? SystemId(uintptr_t(slot.desc.userdata)) : INVALID_SYSTEM;
		String n = slot.desc.name;
		if (n.is_empty()) {
			n = slot.desc.callable.is_valid() ? String(slot.desc.callable.get_method()) : vformat("subscription %d", p_sid);
		}
		row["name"] = n;
		row["stage"] = p_stage;
		row["batch"] = -1;
		row["parallel"] = false;
		row["type"] = from_system ? "event system" : "subscription";
		row["enabled"] = slot.desc.enabled && (!from_system || (sys_id < systems.size() && systems[sys_id].desc.enabled));
		row["order"] = int64_t(slot.desc.order);
		row["events"] = events_to(slot.desc.events);
		timing_to(row, slot.timing);
		list.push_back(row);
	};

	static const EventId stage_event_ids[STAGE_MAX] = {
		EVENT_PRE_UPDATE, EVENT_UPDATE, EVENT_LATE_UPDATE, EVENT_RENDER,
		EVENT_PRE_FIXED_UPDATE, EVENT_FIXED_UPDATE, EVENT_LATE_FIXED_UPDATE, INVALID_EVENT
	};
	HashSet<SubscriptionId> listed;
	for (uint32_t st = 0; st < STAGE_MAX; st++) {
		const LocalVector<Batch> &batches = schedule[st];
		for (uint32_t b = 0; b < batches.size(); b++) {
			for (SystemId sid : batches[b].systems) {
				const SystemSlot &slot = systems[sid];
				Dictionary row;
				row["index"] = index++;
				row["name"] = slot.desc.name;
				row["stage"] = stage_names[st];
				row["batch"] = int64_t(b);
				row["parallel"] = batches[b].parallel && batches[b].systems.size() > 1;
				row["type"] = "system";
				row["enabled"] = slot.desc.enabled;
				row["order"] = int64_t(slot.desc.order);
				row["events"] = Array();
				timing_to(row, slot.timing);
				list.push_back(row);
				system_count++;
				if (slot.desc.enabled) {
					active_count++;
				}
			}
		}
		if (stage_event_ids[st] != INVALID_EVENT) {
			for (SubscriptionId sid : subscription_order) {
				if (sid < subscriptions.size() && subscriptions[sid].valid && subscriptions[sid].desc.events.test(stage_event_ids[st])) {
					subscription_row(sid, vformat("%s (event)", stage_names[st]));
					listed.insert(sid);
				}
			}
		}
	}
	// Subscriptions to entity / network / custom events.
	for (SubscriptionId sid : subscription_order) {
		if (sid < subscriptions.size() && subscriptions[sid].valid && !listed.has(sid)) {
			subscription_row(sid, "events");
		}
	}
	d["systems"] = list;
	d["system_count"] = int64_t(system_count);
	d["active_systems"] = int64_t(active_count);
	d["subscription_count"] = int64_t(subscription_order.size());

	Array archs;
	for (const Archetype *a : archetypes) {
		Dictionary row;
		Array comps;
		a->get_mask().for_each([&](uint32_t p_bit) { comps.push_back(component_types[p_bit]->name); });
		row["components"] = comps;
		row["entities"] = int64_t(a->size());
		row["capacity"] = int64_t(a->get_capacity());
		row["memory"] = int64_t(a->get_memory_usage());
		archs.push_back(row);
	}
	d["archetype_list"] = archs;
	return d;
}

/* Command buffer */

void CommandBuffer::flush(World &p_world) {
	LocalVector<Command> cmds;
	LocalVector<uint8_t> blob;
	LocalVector<const ComponentType *> types;
	{
		MutexLock lock(mutex);
		cmds = std::move(commands);
		blob = std::move(data);
		types = std::move(data_types);
		commands.clear();
		data.clear();
		data_types.clear();
	}
	for (uint32_t i = 0; i < cmds.size(); i++) {
		const Command &c = cmds[i];
		switch (c.type) {
			case CMD_DESTROY_ENTITY:
				p_world.destroy_entity(c.entity);
				break;
			case CMD_ADD_COMPONENT: {
				const void *init = c.data_offset == NO_DATA ? nullptr : blob.ptr() + c.data_offset;
				if (p_world.is_alive(c.entity)) {
					p_world.add_component(c.entity, c.component, init);
				}
				if (init) {
					types[i]->destruct_at(blob.ptr() + c.data_offset);
				}
			} break;
			case CMD_REMOVE_COMPONENT:
				p_world.remove_component(c.entity, c.component);
				break;
			case CMD_CALLBACK:
				if (c.callback) {
					c.callback(p_world, c.userdata);
				}
				break;
		}
	}
}

CommandBuffer::~CommandBuffer() {
	for (uint32_t i = 0; i < commands.size(); i++) {
		if (commands[i].type == CMD_ADD_COMPONENT && commands[i].data_offset != NO_DATA) {
			data_types[i]->destruct_at(data.ptr() + commands[i].data_offset);
		}
	}
}

} // namespace ecs
