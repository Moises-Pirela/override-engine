/**************************************************************************/
/*  ecs_world_bind.cpp                                                    */
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

#include "ecs_world_bind.h"

#include "../components/ecs_builtin_components.h"
#include "ecs_profiler.h"

#include "core/variant/variant_utility.h"
#include "scene/resources/2d/shape_2d.h"
#include "scene/resources/3d/shape_3d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/texture.h"

using namespace ecs;

/* Helpers */

static _FORCE_INLINE_ Entity _e(int64_t p_id) {
	return Entity(uint64_t(p_id));
}

ComponentId ECSWorld::_resolve(const Variant &p_component) const {
	switch (p_component.get_type()) {
		case Variant::INT: {
			int64_t id = p_component;
			if (id < 0 || id >= int64_t(world->get_component_type_count())) {
				return INVALID_COMPONENT;
			}
			return ComponentId(id);
		}
		case Variant::STRING:
		case Variant::STRING_NAME:
			return world->find_component(p_component);
		default:
			return INVALID_COMPONENT;
	}
}

EventId ECSWorld::_resolve_event(const Variant &p_event) const {
	switch (p_event.get_type()) {
		case Variant::INT: {
			int64_t id = p_event;
			if (id < 0 || id >= int64_t(MAX_EVENTS)) {
				return INVALID_EVENT;
			}
			return EventId(id);
		}
		case Variant::STRING:
		case Variant::STRING_NAME:
			return world->find_event(p_event);
		default:
			return INVALID_EVENT;
	}
}

bool ECSWorld::_event_mask_from(const Variant &p_events, EventMask &r_mask) const {
	if (p_events.get_type() == Variant::ARRAY) {
		Array arr = p_events;
		for (int i = 0; i < arr.size(); i++) {
			EventId id = _resolve_event(arr[i]);
			ERR_FAIL_COND_V_MSG(id == INVALID_EVENT, false, vformat("Unknown event '%s'.", String(arr[i])));
			r_mask.set(id);
		}
		return true;
	}
	EventId id = _resolve_event(p_events);
	ERR_FAIL_COND_V_MSG(id == INVALID_EVENT, false, vformat("Unknown event '%s'.", String(p_events)));
	r_mask.set(id);
	return true;
}

Replicator *ECSWorld::_replicator() {
	return world->get_replicator();
}

Array ECSWorld::_mask_to_names(const World *p_world, const ComponentMask &p_mask) {
	Array r;
	p_mask.for_each([&](uint32_t p_bit) {
		r.push_back(p_world->get_component_type(p_bit)->name);
	});
	return r;
}

bool ECSWorld::_mask_from_array(const Array &p_components, ComponentMask &r_mask) const {
	for (int i = 0; i < p_components.size(); i++) {
		ComponentId id = _resolve(p_components[i]);
		if (id == INVALID_COMPONENT) {
			ERR_PRINT(vformat("Unknown component '%s'.", String(p_components[i])));
			return false;
		}
		r_mask.set(id);
	}
	return true;
}

#define RESOLVE_OR_FAIL(m_var, m_component, m_ret) \
	ComponentId m_var = _resolve(m_component);     \
	ERR_FAIL_COND_V_MSG(m_var == INVALID_COMPONENT, m_ret, vformat("Unknown component '%s'.", String(m_component)));

/* Components */

int64_t ECSWorld::register_component(const StringName &p_name, const Dictionary &p_fields, int p_flags) {
	String err;
	ComponentId id = world->register_script_component(p_name, p_fields, uint32_t(p_flags), &err);
	ERR_FAIL_COND_V_MSG(id == INVALID_COMPONENT, -1, vformat("Unable to register component '%s': %s", p_name, err));
	return id;
}

int64_t ECSWorld::get_component_id(const StringName &p_name) const {
	ComponentId id = world->find_component(p_name);
	return id == INVALID_COMPONENT ? -1 : int64_t(id);
}

StringName ECSWorld::get_component_name(int64_t p_id) const {
	const ComponentType *t = world->get_component_type(ComponentId(p_id));
	return t ? t->name : StringName();
}

PackedStringArray ECSWorld::get_component_names() const {
	PackedStringArray r;
	for (uint32_t i = 0; i < world->get_component_type_count(); i++) {
		r.push_back(world->get_component_type(i)->name);
	}
	return r;
}

Dictionary ECSWorld::get_component_info(const Variant &p_component) const {
	Dictionary d;
	ComponentId id = _resolve(p_component);
	if (id == INVALID_COMPONENT) {
		return d;
	}
	const ComponentType *t = world->get_component_type(id);
	d["id"] = int64_t(id);
	d["name"] = t->name;
	d["size"] = int64_t(t->size);
	d["flags"] = int64_t(t->flags);
	d["sparse"] = t->is_sparse();
	d["replicated"] = t->is_replicated();
	Dictionary fields;
	for (const ComponentField &f : t->fields) {
		fields[f.name] = f.kind == FIELD_KIND_ENTITY ? int64_t(FIELD_TYPE_ENTITY) : int64_t(f.type);
	}
	d["fields"] = fields;
	return d;
}

int64_t ECSWorld::get_component_count() const {
	return world->get_component_type_count();
}

/* Entities */

int64_t ECSWorld::create_entity(const Dictionary &p_components) {
	if (p_components.is_empty()) {
		return int64_t(world->create_entity().id);
	}
	ComponentMask table;
	LocalVector<ComponentId> sparse;
	for (const KeyValue<Variant, Variant> &kv : p_components) {
		ComponentId id = _resolve(kv.key);
		ERR_FAIL_COND_V_MSG(id == INVALID_COMPONENT, 0, vformat("Unknown component '%s'.", String(kv.key)));
		if (world->is_sparse_component(id)) {
			sparse.push_back(id);
		} else {
			table.set(id);
		}
	}
	Entity e = world->create_entity(table);
	for (ComponentId id : sparse) {
		world->add_component(e, id, nullptr);
	}
	for (const KeyValue<Variant, Variant> &kv : p_components) {
		if (kv.value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary values = kv.value;
		if (values.is_empty()) {
			continue;
		}
		ComponentId id = _resolve(kv.key);
		const ComponentType *t = world->get_component_type(id);
		void *ptr = world->get_component_mut(e, id);
		if (ptr) {
			t->from_dictionary(ptr, values);
		}
	}
	return int64_t(e.id);
}

bool ECSWorld::destroy_entity(int64_t p_entity) {
	return world->destroy_entity(_e(p_entity));
}

bool ECSWorld::is_alive(int64_t p_entity) const {
	return world->is_alive(_e(p_entity));
}

int64_t ECSWorld::get_entity_count() const {
	return world->get_entity_count();
}

void ECSWorld::clear_entities() {
	world->clear_entities();
}

Array ECSWorld::get_entity_components(int64_t p_entity) const {
	return _mask_to_names(world, world->get_entity_mask(_e(p_entity)));
}

bool ECSWorld::add_component(int64_t p_entity, const Variant &p_component, const Dictionary &p_values) {
	RESOLVE_OR_FAIL(id, p_component, false);
	Entity e = _e(p_entity);
	ERR_FAIL_COND_V_MSG(!world->is_alive(e), false, "Entity is not alive.");
	const ComponentType *t = world->get_component_type(id);
	void *ptr = world->add_component(e, id, nullptr);
	if (!ptr && !t->is_tag()) {
		return false;
	}
	if (ptr && !p_values.is_empty()) {
		t->from_dictionary(ptr, p_values);
	}
	return true;
}

bool ECSWorld::remove_component(int64_t p_entity, const Variant &p_component) {
	RESOLVE_OR_FAIL(id, p_component, false);
	return world->remove_component(_e(p_entity), id);
}

bool ECSWorld::has_component(int64_t p_entity, const Variant &p_component) const {
	ComponentId id = _resolve(p_component);
	if (id == INVALID_COMPONENT) {
		return false;
	}
	return world->has_component(_e(p_entity), id);
}

Dictionary ECSWorld::get_component(int64_t p_entity, const Variant &p_component) const {
	RESOLVE_OR_FAIL(id, p_component, Dictionary());
	const void *ptr = world->get_component(_e(p_entity), id);
	if (!ptr) {
		return Dictionary();
	}
	return world->get_component_type(id)->to_dictionary(ptr);
}

bool ECSWorld::set_component(int64_t p_entity, const Variant &p_component, const Dictionary &p_values) {
	RESOLVE_OR_FAIL(id, p_component, false);
	void *ptr = world->get_component_mut(_e(p_entity), id);
	if (!ptr) {
		return false;
	}
	world->get_component_type(id)->from_dictionary(ptr, p_values);
	return true;
}

Variant ECSWorld::get_field(int64_t p_entity, const Variant &p_component, const StringName &p_field) const {
	RESOLVE_OR_FAIL(id, p_component, Variant());
	const ComponentType *t = world->get_component_type(id);
	int32_t f = t->find_field(p_field);
	ERR_FAIL_COND_V_MSG(f < 0, Variant(), vformat("Component '%s' has no field '%s'.", t->name, p_field));
	const void *ptr = world->get_component(_e(p_entity), id);
	if (!ptr) {
		return Variant();
	}
	return t->get_field(ptr, uint32_t(f));
}

bool ECSWorld::set_field(int64_t p_entity, const Variant &p_component, const StringName &p_field, const Variant &p_value) {
	RESOLVE_OR_FAIL(id, p_component, false);
	const ComponentType *t = world->get_component_type(id);
	int32_t f = t->find_field(p_field);
	ERR_FAIL_COND_V_MSG(f < 0, false, vformat("Component '%s' has no field '%s'.", t->name, p_field));
	void *ptr = world->get_component_mut(_e(p_entity), id);
	if (!ptr) {
		return false;
	}
	return t->set_field(ptr, uint32_t(f), p_value);
}

int64_t ECSWorld::get_change_tick(int64_t p_entity, const Variant &p_component) const {
	ComponentId id = _resolve(p_component);
	if (id == INVALID_COMPONENT) {
		return 0;
	}
	return world->get_component_change_tick(_e(p_entity), id);
}

/* Deferred */

void ECSWorld::defer_destroy_entity(int64_t p_entity) {
	world->commands().destroy_entity(_e(p_entity));
}

void ECSWorld::defer_add_component(int64_t p_entity, const Variant &p_component, const Dictionary &p_values) {
	RESOLVE_OR_FAIL(id, p_component, void());
	const ComponentType *t = world->get_component_type(id);
	if (p_values.is_empty() || t->size == 0) {
		world->commands().add_component(_e(p_entity), *t, nullptr);
		return;
	}
	// Build a temporary value to copy into the command buffer.
	uint8_t *tmp = (uint8_t *)memalloc(t->size);
	t->construct_at(tmp);
	t->from_dictionary(tmp, p_values);
	world->commands().add_component(_e(p_entity), *t, tmp);
	t->destruct_at(tmp);
	memfree(tmp);
}

void ECSWorld::defer_remove_component(int64_t p_entity, const Variant &p_component) {
	RESOLVE_OR_FAIL(id, p_component, void());
	world->commands().remove_component(_e(p_entity), id);
}

void ECSWorld::flush_commands() {
	world->flush_commands();
}

/* Prefabs and schemas */

int64_t ECSWorld::register_schema(const Ref<ECSComponentSchema> &p_schema) {
	ERR_FAIL_COND_V(p_schema.is_null(), -1);
	StringName name = p_schema->get_component_name();
	ERR_FAIL_COND_V_MSG(name == StringName(), -1, "The schema has no component name.");
	ComponentId existing = world->find_component(name);
	if (existing != INVALID_COMPONENT) {
		return existing;
	}
	String err;
	ComponentId id = world->register_script_component(name, p_schema->get_fields(), uint32_t(p_schema->get_flags()), &err);
	ERR_FAIL_COND_V_MSG(id == INVALID_COMPONENT, -1, vformat("Unable to register schema '%s': %s", name, err));
	return id;
}

Entity ECSWorld::_instantiate_prefab(const Ref<ECSPrefab> &p_prefab, Entity p_parent, const String &p_root_path, const String &p_index_path) {
	Dictionary comps = p_prefab->get_components();
	ComponentMask table;
	LocalVector<ComponentId> sparse;
	for (const KeyValue<Variant, Variant> &kv : comps) {
		ComponentId id = world->find_component(kv.key);
		if (id == INVALID_COMPONENT) {
			WARN_PRINT(vformat("ECSPrefab '%s': component '%s' is not registered in this world, skipping.", p_prefab->get_path(), String(kv.key)));
			continue;
		}
		if (world->is_sparse_component(id)) {
			sparse.push_back(id);
		} else {
			table.set(id);
		}
	}
	if (p_parent.is_valid()) {
		table.set(world->component_id<Parent>());
	}
	if (!p_root_path.is_empty()) {
		table.set(world->component_id<PrefabRef>());
	}
	Entity e = world->create_entity(table);
	for (ComponentId id : sparse) {
		world->add_component(e, id, nullptr);
	}
	for (const KeyValue<Variant, Variant> &kv : comps) {
		ComponentId id = world->find_component(kv.key);
		if (id == INVALID_COMPONENT || kv.value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary values = kv.value;
		if (values.is_empty()) {
			continue;
		}
		void *ptr = world->get_component_mut(e, id);
		if (ptr) {
			world->get_component_type(id)->from_dictionary(ptr, values);
		}
	}
	if (p_parent.is_valid()) {
		world->get_mut<Parent>(e)->parent = p_parent;
	}
	if (!p_root_path.is_empty()) {
		world->get_mut<PrefabRef>(e)->path = ECSPrefab::make_sub_path(p_root_path, p_index_path);
	}
	for (int i = 0; i < p_prefab->get_child_count(); i++) {
		Ref<ECSPrefab> child = p_prefab->get_child(i);
		if (child.is_null()) {
			continue;
		}
		String child_index = p_index_path.is_empty() ? itos(i) : p_index_path + "/" + itos(i);
		_instantiate_prefab(child, e, p_root_path, child_index);
	}
	return e;
}

int64_t ECSWorld::instantiate_prefab(const Ref<ECSPrefab> &p_prefab, int64_t p_parent) {
	ERR_FAIL_COND_V(p_prefab.is_null(), 0);
	Entity parent = _e(p_parent);
	ERR_FAIL_COND_V_MSG(parent.is_valid() && !world->is_alive(parent), 0, "Parent entity is not alive.");
	String path = p_prefab->get_path();
	if (path.contains("::")) {
		path = String(); // Embedded sub-resource: not addressable on its own.
	}
	return int64_t(_instantiate_prefab(p_prefab, parent, path, String()).id);
}

bool ECSWorld::apply_prefab(int64_t p_entity, const Ref<ECSPrefab> &p_prefab, bool p_overwrite) {
	ERR_FAIL_COND_V(p_prefab.is_null(), false);
	Entity e = _e(p_entity);
	ERR_FAIL_COND_V_MSG(!world->is_alive(e), false, "Entity is not alive.");
	Dictionary comps = p_prefab->get_components();
	for (const KeyValue<Variant, Variant> &kv : comps) {
		ComponentId id = world->find_component(kv.key);
		if (id == INVALID_COMPONENT) {
			continue;
		}
		bool had = world->has_component(e, id);
		if (had && !p_overwrite) {
			continue;
		}
		void *ptr = world->add_component(e, id, nullptr);
		if (ptr && kv.value.get_type() == Variant::DICTIONARY) {
			world->get_component_type(id)->from_dictionary(ptr, Dictionary(kv.value));
		}
	}
	return true;
}

void ECSWorld::_apply_prefab_locals(Entity p_entity) {
	const PrefabRef *ref = world->get<PrefabRef>(p_entity);
	if (!ref || ref->path.is_empty()) {
		return;
	}
	Ref<ECSPrefab> prefab = ECSPrefab::load_from_path(ref->path);
	if (prefab.is_null()) {
		WARN_PRINT(vformat("ECS: replicated entity references prefab '%s' which could not be loaded.", ref->path));
		return;
	}
	apply_prefab(int64_t(p_entity.id), prefab, false);
}

PackedInt64Array ECSWorld::get_children(int64_t p_entity) const {
	PackedInt64Array r;
	Entity parent = _e(p_entity);
	Query q;
	q.init_with<Parent>(world);
	q.each<const Parent>([&](Entity p_child, const Parent &p_parent) {
		if (p_parent.parent == parent) {
			r.push_back(int64_t(p_child.id));
		}
	});
	return r;
}

Ref<ECSPrefab> ECSWorld::_create_prefab(Entity p_entity, bool p_recursive, Query &p_parents) {
	Ref<ECSPrefab> prefab;
	prefab.instantiate();
	ComponentMask mask = world->get_entity_mask(p_entity);
	const ComponentId skip[] = { world->component_id<NetworkIdentity>(), world->component_id<PrefabRef>(), world->component_id<Parent>(), world->component_id<GlobalTransform2D>(), world->component_id<GlobalTransform3D>() };
	mask.for_each([&](uint32_t p_bit) {
		for (ComponentId s : skip) {
			if (p_bit == s) {
				return;
			}
		}
		const ComponentType *t = world->get_component_type(p_bit);
		Dictionary values;
		const void *ptr = world->get_component(p_entity, p_bit);
		if (ptr) {
			for (uint32_t i = 0; i < t->fields.size(); i++) {
				const ComponentField &f = t->fields[i];
				if (f.kind == FIELD_KIND_TRIVIAL && f.type == Variant::RID) {
					continue; // Runtime handles.
				}
				values[f.name] = t->get_field(ptr, i);
			}
		}
		prefab->add_component(t->name, values);
	});
	if (p_recursive) {
		LocalVector<Entity> children;
		p_parents.each<const Parent>([&](Entity p_child, const Parent &p_parent) {
			if (p_parent.parent == p_entity) {
				children.push_back(p_child);
			}
		});
		for (Entity c : children) {
			prefab->add_child(_create_prefab(c, true, p_parents));
		}
	}
	return prefab;
}

Ref<ECSPrefab> ECSWorld::create_prefab(int64_t p_entity, bool p_recursive) {
	Entity e = _e(p_entity);
	ERR_FAIL_COND_V_MSG(!world->is_alive(e), Ref<ECSPrefab>(), "Entity is not alive.");
	Query parents;
	parents.init_with<Parent>(world);
	return _create_prefab(e, p_recursive, parents);
}

/* Queries */

Ref<ECSQuery> ECSWorld::query(const Array &p_include, const Array &p_exclude) {
	ComponentMask inc;
	ComponentMask exc;
	ERR_FAIL_COND_V(!_mask_from_array(p_include, inc), Ref<ECSQuery>());
	ERR_FAIL_COND_V(!_mask_from_array(p_exclude, exc), Ref<ECSQuery>());
	Ref<ECSQuery> q;
	q.instantiate();
	q->world = Ref<ECSWorld>(this);
	q->query.init(world, inc, exc);
	return q;
}

/* Events */

int64_t ECSWorld::register_event(const StringName &p_name) {
	EventId id = world->register_event(p_name);
	return id == INVALID_EVENT ? -1 : int64_t(id);
}

int64_t ECSWorld::get_event_id(const StringName &p_name) const {
	EventId id = world->find_event(p_name);
	return id == INVALID_EVENT ? -1 : int64_t(id);
}

StringName ECSWorld::get_event_name(int64_t p_event) const {
	return world->get_event_name(EventId(p_event));
}

PackedStringArray ECSWorld::get_event_names() const {
	PackedStringArray r;
	for (uint32_t i = 0; i < world->get_event_count(); i++) {
		StringName n = world->get_event_name(i);
		if (n != StringName()) {
			r.push_back(n);
		}
	}
	return r;
}

int64_t ECSWorld::subscribe(const Variant &p_events, const Callable &p_callable, const Array &p_filter, int64_t p_entity, int p_order) {
	ERR_FAIL_COND_V_MSG(!p_callable.is_valid(), -1, "Invalid callable.");
	SubscriptionDesc d;
	ERR_FAIL_COND_V(!_event_mask_from(p_events, d.events), -1);
	ERR_FAIL_COND_V(!_mask_from_array(p_filter, d.filter), -1);
	d.callable = p_callable;
	d.entity = _e(p_entity);
	d.order = p_order;
	d.name = p_callable.get_method();
	SubscriptionId id = world->subscribe(d);
	return id == INVALID_SUBSCRIPTION ? -1 : int64_t(id);
}

bool ECSWorld::unsubscribe(int64_t p_subscription) {
	if (p_subscription < 0) {
		return false;
	}
	return world->unsubscribe(SubscriptionId(p_subscription));
}

void ECSWorld::set_subscription_enabled(int64_t p_subscription, bool p_enabled) {
	ERR_FAIL_COND(p_subscription < 0);
	world->set_subscription_enabled(SubscriptionId(p_subscription), p_enabled);
}

void ECSWorld::emit_event(const Variant &p_event, const Variant &p_payload, int64_t p_entity) {
	EventId id = _resolve_event(p_event);
	ERR_FAIL_COND_MSG(id == INVALID_EVENT, vformat("Unknown event '%s'.", String(p_event)));
	ecs::Event ev(id);
	ev.entity = _e(p_entity);
	ev.payload = p_payload;
	world->emit_event(ev);
}

void ECSWorld::queue_event(const Variant &p_event, const Variant &p_payload, int64_t p_entity) {
	EventId id = _resolve_event(p_event);
	ERR_FAIL_COND_MSG(id == INVALID_EVENT, vformat("Unknown event '%s'.", String(p_event)));
	ecs::Event ev(id);
	ev.entity = _e(p_entity);
	ev.payload = p_payload;
	world->queue_event(ev);
}

void ECSWorld::flush_events() {
	world->flush_events();
}

int64_t ECSWorld::add_system_object(const Ref<ECSSystem> &p_system) {
	ERR_FAIL_COND_V(p_system.is_null(), -1);
	ERR_FAIL_COND_V_MSG(p_system->is_registered(), -1, "The system is already registered in a world.");
	SubscriptionDesc d;
	ERR_FAIL_COND_V_MSG(!_event_mask_from(p_system->get_events(), d.events), -1, "ECSSystem._get_events() returned an unknown event.");
	ERR_FAIL_COND_V_MSG(d.events.is_empty(), -1, "ECSSystem._get_events() must return at least one event.");
	ERR_FAIL_COND_V(!_mask_from_array(p_system->get_component_filter(), d.filter), -1);
	d.name = p_system->get_class();
	d.func = ECSSystem::_dispatch;
	d.userdata = p_system.ptr();
	d.order = p_system->get_order();
	d.enabled = p_system->is_enabled();
	SubscriptionId id = world->subscribe(d);
	ERR_FAIL_COND_V(id == INVALID_SUBSCRIPTION, -1);
	p_system->subscription = int64_t(id);
	p_system->world_id = get_instance_id();
	system_objects.push_back(p_system);
	p_system->GDVIRTUAL_CALL(_on_registered);
	return int64_t(id);
}

bool ECSWorld::remove_system_object(const Ref<ECSSystem> &p_system) {
	if (p_system.is_null() || !p_system->is_registered() || p_system->world_id != get_instance_id()) {
		return false;
	}
	world->unsubscribe(SubscriptionId(p_system->subscription));
	p_system->subscription = -1;
	p_system->world_id = ObjectID();
	system_objects.erase(p_system);
	p_system->GDVIRTUAL_CALL(_on_unregistered);
	return true;
}

void ECSWorld::clear_systems() {
	LocalVector<Ref<ECSSystem>> objects = system_objects;
	for (const Ref<ECSSystem> &sys : objects) {
		remove_system_object(sys);
	}
	world->clear_systems();
	// Built-in systems are part of the world's behavior: keep them.
	register_builtin_systems(*world);
}

Array ECSWorld::get_system_objects() const {
	Array r;
	for (const Ref<ECSSystem> &s : system_objects) {
		r.push_back(s);
	}
	return r;
}

/* Systems */

int64_t ECSWorld::add_system(const StringName &p_name, const Callable &p_callable, Stage p_stage, int p_order, const Array &p_reads, const Array &p_writes) {
	ERR_FAIL_COND_V_MSG(!p_callable.is_valid(), -1, "Invalid callable.");
	SystemDesc d;
	d.name = p_name;
	d.callable = p_callable;
	d.stage = ecs::Stage(p_stage);
	d.order = p_order;
	ERR_FAIL_COND_V(!_mask_from_array(p_reads, d.reads), -1);
	ERR_FAIL_COND_V(!_mask_from_array(p_writes, d.writes), -1);
	SystemId id = world->add_system(d);
	return id == INVALID_SYSTEM ? -1 : int64_t(id);
}

bool ECSWorld::remove_system(const StringName &p_name) {
	SystemId id = world->find_system(p_name);
	if (id == INVALID_SYSTEM) {
		return false;
	}
	return world->remove_system(id);
}

void ECSWorld::set_system_enabled(const StringName &p_name, bool p_enabled) {
	SystemId id = world->find_system(p_name);
	ERR_FAIL_COND_MSG(id == INVALID_SYSTEM, vformat("No system named '%s'.", p_name));
	world->set_system_enabled(id, p_enabled);
}

bool ECSWorld::is_system_enabled(const StringName &p_name) const {
	SystemId id = world->find_system(p_name);
	return id != INVALID_SYSTEM && world->is_system_enabled(id);
}

PackedStringArray ECSWorld::get_system_names() const {
	PackedStringArray r;
	for (uint32_t i = 0; i < world->get_system_count(); i++) {
		const SystemDesc *d = world->get_system(i);
		if (d) {
			r.push_back(d->name);
		}
	}
	return r;
}

void ECSWorld::update(double p_delta) {
	world->update(p_delta);
}

void ECSWorld::physics_update(double p_delta) {
	world->physics_update(p_delta);
}

void ECSWorld::run_stage(Stage p_stage, double p_delta) {
	world->run_stage(ecs::Stage(p_stage), p_delta);
}

int64_t ECSWorld::get_tick() const {
	return world->get_tick();
}

/* Networking */

void ECSWorld::set_multiplayer_peer(const Ref<MultiplayerPeer> &p_peer) {
	multiplayer_peer = p_peer;
	_replicator()->set_peer(p_peer);
}

Ref<MultiplayerPeer> ECSWorld::get_multiplayer_peer() const {
	return multiplayer_peer;
}

bool ECSWorld::is_server() const {
	return multiplayer_peer.is_null() || multiplayer_peer->is_server();
}

bool ECSWorld::is_network_connected() const {
	return multiplayer_peer.is_valid() && multiplayer_peer->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED;
}

int ECSWorld::get_unique_id() const {
	return multiplayer_peer.is_valid() ? multiplayer_peer->get_unique_id() : 1;
}

PackedInt32Array ECSWorld::get_peers() const {
	PackedInt32Array r;
	if (world->get_replicator()) {
		LocalVector<int> ids;
		world->get_replicator()->get_peer_ids(ids);
		for (int id : ids) {
			r.push_back(id);
		}
	}
	return r;
}

void ECSWorld::replicate(int64_t p_entity, int p_authority, bool p_recursive) {
	ERR_FAIL_COND_MSG(!world->is_alive(_e(p_entity)), "Entity is not alive.");
	_replicator()->replicate(_e(p_entity), p_authority);
	if (p_recursive) {
		PackedInt64Array children = get_children(p_entity);
		for (int64_t c : children) {
			replicate(c, p_authority, true);
		}
	}
}

int64_t ECSWorld::get_network_id(int64_t p_entity) const {
	return world->get_replicator()->get_net_id(_e(p_entity));
}

int64_t ECSWorld::get_entity_by_network_id(int64_t p_net_id) const {
	return int64_t(world->get_replicator()->get_entity(uint32_t(p_net_id)).id);
}

int ECSWorld::get_authority(int64_t p_entity) const {
	return world->get_replicator()->get_authority(_e(p_entity));
}

void ECSWorld::set_authority(int64_t p_entity, int p_peer) {
	_replicator()->set_authority(_e(p_entity), p_peer);
}

bool ECSWorld::has_authority(int64_t p_entity) const {
	return world->get_replicator()->has_authority(_e(p_entity));
}

void ECSWorld::register_command(const StringName &p_name, const Callable &p_callable) {
	_replicator()->register_command(p_name, p_callable);
}

void ECSWorld::unregister_command(const StringName &p_name) {
	_replicator()->unregister_command(p_name);
}

void ECSWorld::send_command(int p_peer, const StringName &p_name, const Array &p_args, bool p_reliable) {
	_replicator()->send_command(p_peer, p_name, p_args, p_reliable);
}

void ECSWorld::set_sync_interval(int p_ticks) {
	_replicator()->set_sync_interval(uint32_t(MAX(p_ticks, 1)));
}

int ECSWorld::get_sync_interval() const {
	return int(world->get_replicator()->get_sync_interval());
}

Dictionary ECSWorld::get_network_stats() const {
	Dictionary d;
	const Replicator::Stats &s = world->get_replicator()->get_stats();
	d["packets_sent"] = int64_t(s.packets_sent);
	d["packets_received"] = int64_t(s.packets_received);
	d["bytes_sent"] = int64_t(s.bytes_sent);
	d["bytes_received"] = int64_t(s.bytes_received);
	d["last_sync_updates"] = int64_t(s.last_sync_updates);
	return d;
}

void ECSWorld::on_peer_connected(int p_peer) {
	emit_signal(SNAME("peer_connected"), p_peer);
}

void ECSWorld::on_peer_disconnected(int p_peer) {
	emit_signal(SNAME("peer_disconnected"), p_peer);
}

void ECSWorld::on_entity_spawned(Entity p_entity, uint32_t p_net_id) {
	if (!is_server()) {
		// Clients re-create the prefab's local-only components (sprites, meshes...).
		_apply_prefab_locals(p_entity);
	}
	emit_signal(SNAME("entity_spawned"), int64_t(p_entity.id), int64_t(p_net_id));
}

void ECSWorld::on_entity_despawned(Entity p_entity, uint32_t p_net_id) {
	emit_signal(SNAME("entity_despawned"), int64_t(p_entity.id), int64_t(p_net_id));
}

void ECSWorld::on_authority_changed(Entity p_entity, int p_peer) {
	emit_signal(SNAME("authority_changed"), int64_t(p_entity.id), p_peer);
}

bool ECSWorld::on_command(int p_sender, const StringName &p_name, const Array &p_args) {
	emit_signal(SNAME("command_received"), p_sender, p_name, p_args);
	return true;
}

/* Rendering context */

void ECSWorld::set_canvas(RID p_canvas) {
	world->set_canvas(p_canvas);
}
RID ECSWorld::get_canvas() const {
	return world->get_canvas();
}
void ECSWorld::set_scenario(RID p_scenario) {
	world->set_scenario(p_scenario);
}
RID ECSWorld::get_scenario() const {
	return world->get_scenario();
}
void ECSWorld::set_viewport(RID p_viewport) {
	world->set_viewport(p_viewport);
}
RID ECSWorld::get_viewport() const {
	return world->get_viewport();
}
void ECSWorld::set_viewport_size(const Vector2 &p_size) {
	world->set_viewport_size(p_size);
}
Vector2 ECSWorld::get_viewport_size() const {
	return world->get_viewport_size();
}

void ECSWorld::_retain(const Ref<Resource> &p_resource) {
	if (p_resource.is_valid() && p_resource->get_rid().is_valid()) {
		retained_resources.insert(p_resource->get_rid(), p_resource);
	}
}

void ECSWorld::release_resource(const Ref<Resource> &p_resource) {
	if (p_resource.is_valid()) {
		retained_resources.erase(p_resource->get_rid());
	}
}

bool ECSWorld::set_sprite_texture(int64_t p_entity, const Ref<Texture2D> &p_texture) {
	Entity e = _e(p_entity);
	ERR_FAIL_COND_V(!world->is_alive(e), false);
	Sprite2D *s = world->get_mut<Sprite2D>(e);
	if (!s) {
		s = world->add<Sprite2D>(e);
	}
	ERR_FAIL_NULL_V(s, false);
	if (p_texture.is_valid()) {
		_retain(p_texture);
		s->texture = p_texture->get_rid();
		s->size = p_texture->get_size();
	} else {
		s->texture = RID();
	}
	return true;
}

bool ECSWorld::set_mesh(int64_t p_entity, const Ref<Mesh> &p_mesh) {
	Entity e = _e(p_entity);
	ERR_FAIL_COND_V(!world->is_alive(e), false);
	MeshInstance3D *m = world->get_mut<MeshInstance3D>(e);
	if (!m) {
		m = world->add<MeshInstance3D>(e);
	}
	ERR_FAIL_NULL_V(m, false);
	_retain(p_mesh);
	m->mesh = p_mesh.is_valid() ? p_mesh->get_rid() : RID();
	return true;
}

/* Physics context */

void ECSWorld::set_space_2d(RID p_space) {
	world->set_space_2d(p_space);
}
RID ECSWorld::get_space_2d() const {
	return world->get_space_2d();
}
void ECSWorld::set_space_3d(RID p_space) {
	world->set_space_3d(p_space);
}
RID ECSWorld::get_space_3d() const {
	return world->get_space_3d();
}

bool ECSWorld::set_shape_2d(int64_t p_entity, const Ref<Shape2D> &p_shape) {
#ifdef PHYSICS_2D_DISABLED
	return false;
#else
	Entity e = _e(p_entity);
	ERR_FAIL_COND_V(!world->is_alive(e), false);
	PhysicsBody2D *b = world->get_mut<PhysicsBody2D>(e);
	if (!b) {
		b = world->add<PhysicsBody2D>(e);
	}
	ERR_FAIL_NULL_V(b, false);
	_retain(p_shape);
	b->shape = p_shape.is_valid() ? p_shape->get_rid() : RID();
	return true;
#endif
}

bool ECSWorld::set_shape_3d(int64_t p_entity, const Ref<Shape3D> &p_shape) {
#ifdef PHYSICS_3D_DISABLED
	return false;
#else
	Entity e = _e(p_entity);
	ERR_FAIL_COND_V(!world->is_alive(e), false);
	PhysicsBody3D *b = world->get_mut<PhysicsBody3D>(e);
	if (!b) {
		b = world->add<PhysicsBody3D>(e);
	}
	ERR_FAIL_NULL_V(b, false);
	_retain(p_shape);
	b->shape = p_shape.is_valid() ? p_shape->get_rid() : RID();
	return true;
#endif
}

/* Introspection / profiling */

void ECSWorld::set_name(const StringName &p_name) {
	world->set_name(p_name);
}

StringName ECSWorld::get_name() const {
	return world->get_name();
}

int64_t ECSWorld::get_memory_usage() const {
	return int64_t(world->get_memory_usage().total());
}

Dictionary ECSWorld::get_profile() const {
	return world->get_profile();
}

Array ECSWorld::get_all_profiles() {
	return ECSProfiler::collect();
}

int64_t ECSWorld::get_archetype_count() const {
	return world->get_archetype_count();
}

Array ECSWorld::get_archetype_info() const {
	Array r;
	for (Archetype *a : world->get_archetypes()) {
		Dictionary d;
		d["id"] = int64_t(a->get_id());
		d["components"] = _mask_to_names(world, a->get_mask());
		d["entities"] = int64_t(a->size());
		d["capacity"] = int64_t(a->get_capacity());
		r.push_back(d);
	}
	return r;
}

/* Lifecycle */

ECSWorld::ECSWorld() {
	world = memnew(World);
	world->set_user_data(this);
	ECSComponentLibrary::register_schemas(*world);
	ECSEventLibrary::register_events(*world);
	register_builtin_systems(*world);
	world->get_replicator()->add_listener(this);
}

ECSWorld::~ECSWorld() {
	for (Ref<ECSSystem> &sys : system_objects) {
		sys->subscription = -1;
		sys->world_id = ObjectID();
	}
	system_objects.clear();
	world->get_replicator()->remove_listener(this);
	memdelete(world);
	retained_resources.clear();
}

void ECSWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_component", "name", "fields", "flags"), &ECSWorld::register_component, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_component_id", "name"), &ECSWorld::get_component_id);
	ClassDB::bind_method(D_METHOD("get_component_name", "id"), &ECSWorld::get_component_name);
	ClassDB::bind_method(D_METHOD("get_component_names"), &ECSWorld::get_component_names);
	ClassDB::bind_method(D_METHOD("get_component_info", "component"), &ECSWorld::get_component_info);
	ClassDB::bind_method(D_METHOD("get_component_count"), &ECSWorld::get_component_count);

	ClassDB::bind_method(D_METHOD("create_entity", "components"), &ECSWorld::create_entity, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("destroy_entity", "entity"), &ECSWorld::destroy_entity);
	ClassDB::bind_method(D_METHOD("is_alive", "entity"), &ECSWorld::is_alive);
	ClassDB::bind_method(D_METHOD("get_entity_count"), &ECSWorld::get_entity_count);
	ClassDB::bind_method(D_METHOD("clear_entities"), &ECSWorld::clear_entities);
	ClassDB::bind_method(D_METHOD("get_entity_components", "entity"), &ECSWorld::get_entity_components);

	ClassDB::bind_method(D_METHOD("add_component", "entity", "component", "values"), &ECSWorld::add_component, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("remove_component", "entity", "component"), &ECSWorld::remove_component);
	ClassDB::bind_method(D_METHOD("has_component", "entity", "component"), &ECSWorld::has_component);
	ClassDB::bind_method(D_METHOD("get_component", "entity", "component"), &ECSWorld::get_component);
	ClassDB::bind_method(D_METHOD("set_component", "entity", "component", "values"), &ECSWorld::set_component);
	ClassDB::bind_method(D_METHOD("get_field", "entity", "component", "field"), &ECSWorld::get_field);
	ClassDB::bind_method(D_METHOD("set_field", "entity", "component", "field", "value"), &ECSWorld::set_field);
	ClassDB::bind_method(D_METHOD("get_change_tick", "entity", "component"), &ECSWorld::get_change_tick);

	ClassDB::bind_method(D_METHOD("defer_destroy_entity", "entity"), &ECSWorld::defer_destroy_entity);
	ClassDB::bind_method(D_METHOD("defer_add_component", "entity", "component", "values"), &ECSWorld::defer_add_component, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("defer_remove_component", "entity", "component"), &ECSWorld::defer_remove_component);
	ClassDB::bind_method(D_METHOD("flush_commands"), &ECSWorld::flush_commands);

	ClassDB::bind_method(D_METHOD("register_schema", "schema"), &ECSWorld::register_schema);
	ClassDB::bind_method(D_METHOD("instantiate_prefab", "prefab", "parent"), &ECSWorld::instantiate_prefab, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("apply_prefab", "entity", "prefab", "overwrite"), &ECSWorld::apply_prefab, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("create_prefab", "entity", "recursive"), &ECSWorld::create_prefab, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_children", "entity"), &ECSWorld::get_children);

	ClassDB::bind_method(D_METHOD("query", "include", "exclude"), &ECSWorld::query, DEFVAL(Array()));

	ClassDB::bind_method(D_METHOD("register_event", "name"), &ECSWorld::register_event);
	ClassDB::bind_method(D_METHOD("get_event_id", "name"), &ECSWorld::get_event_id);
	ClassDB::bind_method(D_METHOD("get_event_name", "event"), &ECSWorld::get_event_name);
	ClassDB::bind_method(D_METHOD("get_event_names"), &ECSWorld::get_event_names);
	ClassDB::bind_method(D_METHOD("subscribe", "events", "callable", "filter", "entity", "order"), &ECSWorld::subscribe, DEFVAL(Array()), DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("unsubscribe", "subscription"), &ECSWorld::unsubscribe);
	ClassDB::bind_method(D_METHOD("set_subscription_enabled", "subscription", "enabled"), &ECSWorld::set_subscription_enabled);
	ClassDB::bind_method(D_METHOD("emit_event", "event", "payload", "entity"), &ECSWorld::emit_event, DEFVAL(Variant()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("queue_event", "event", "payload", "entity"), &ECSWorld::queue_event, DEFVAL(Variant()), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("flush_events"), &ECSWorld::flush_events);
	ClassDB::bind_method(D_METHOD("add_system_object", "system"), &ECSWorld::add_system_object);
	ClassDB::bind_method(D_METHOD("remove_system_object", "system"), &ECSWorld::remove_system_object);
	ClassDB::bind_method(D_METHOD("get_system_objects"), &ECSWorld::get_system_objects);
	ClassDB::bind_method(D_METHOD("clear_systems"), &ECSWorld::clear_systems);

	ClassDB::bind_method(D_METHOD("add_system", "name", "callable", "stage", "order", "reads", "writes"), &ECSWorld::add_system, DEFVAL(STAGE_UPDATE), DEFVAL(0), DEFVAL(Array()), DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("remove_system", "name"), &ECSWorld::remove_system);
	ClassDB::bind_method(D_METHOD("set_system_enabled", "name", "enabled"), &ECSWorld::set_system_enabled);
	ClassDB::bind_method(D_METHOD("is_system_enabled", "name"), &ECSWorld::is_system_enabled);
	ClassDB::bind_method(D_METHOD("get_system_names"), &ECSWorld::get_system_names);
	ClassDB::bind_method(D_METHOD("update", "delta"), &ECSWorld::update);
	ClassDB::bind_method(D_METHOD("physics_update", "delta"), &ECSWorld::physics_update);
	ClassDB::bind_method(D_METHOD("run_stage", "stage", "delta"), &ECSWorld::run_stage);
	ClassDB::bind_method(D_METHOD("get_tick"), &ECSWorld::get_tick);

	ClassDB::bind_method(D_METHOD("set_multiplayer_peer", "peer"), &ECSWorld::set_multiplayer_peer);
	ClassDB::bind_method(D_METHOD("get_multiplayer_peer"), &ECSWorld::get_multiplayer_peer);
	ClassDB::bind_method(D_METHOD("is_server"), &ECSWorld::is_server);
	ClassDB::bind_method(D_METHOD("is_network_connected"), &ECSWorld::is_network_connected);
	ClassDB::bind_method(D_METHOD("get_unique_id"), &ECSWorld::get_unique_id);
	ClassDB::bind_method(D_METHOD("get_peers"), &ECSWorld::get_peers);
	ClassDB::bind_method(D_METHOD("replicate", "entity", "authority", "recursive"), &ECSWorld::replicate, DEFVAL(1), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_network_id", "entity"), &ECSWorld::get_network_id);
	ClassDB::bind_method(D_METHOD("get_entity_by_network_id", "net_id"), &ECSWorld::get_entity_by_network_id);
	ClassDB::bind_method(D_METHOD("get_authority", "entity"), &ECSWorld::get_authority);
	ClassDB::bind_method(D_METHOD("set_authority", "entity", "peer"), &ECSWorld::set_authority);
	ClassDB::bind_method(D_METHOD("has_authority", "entity"), &ECSWorld::has_authority);
	ClassDB::bind_method(D_METHOD("register_command", "name", "callable"), &ECSWorld::register_command);
	ClassDB::bind_method(D_METHOD("unregister_command", "name"), &ECSWorld::unregister_command);
	ClassDB::bind_method(D_METHOD("send_command", "peer", "name", "args", "reliable"), &ECSWorld::send_command, DEFVAL(Array()), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("set_sync_interval", "ticks"), &ECSWorld::set_sync_interval);
	ClassDB::bind_method(D_METHOD("get_sync_interval"), &ECSWorld::get_sync_interval);
	ClassDB::bind_method(D_METHOD("get_network_stats"), &ECSWorld::get_network_stats);

	ClassDB::bind_method(D_METHOD("set_canvas", "canvas"), &ECSWorld::set_canvas);
	ClassDB::bind_method(D_METHOD("get_canvas"), &ECSWorld::get_canvas);
	ClassDB::bind_method(D_METHOD("set_scenario", "scenario"), &ECSWorld::set_scenario);
	ClassDB::bind_method(D_METHOD("get_scenario"), &ECSWorld::get_scenario);
	ClassDB::bind_method(D_METHOD("set_viewport", "viewport"), &ECSWorld::set_viewport);
	ClassDB::bind_method(D_METHOD("get_viewport"), &ECSWorld::get_viewport);
	ClassDB::bind_method(D_METHOD("set_viewport_size", "size"), &ECSWorld::set_viewport_size);
	ClassDB::bind_method(D_METHOD("get_viewport_size"), &ECSWorld::get_viewport_size);
	ClassDB::bind_method(D_METHOD("set_sprite_texture", "entity", "texture"), &ECSWorld::set_sprite_texture);
	ClassDB::bind_method(D_METHOD("set_mesh", "entity", "mesh"), &ECSWorld::set_mesh);
	ClassDB::bind_method(D_METHOD("set_space_2d", "space"), &ECSWorld::set_space_2d);
	ClassDB::bind_method(D_METHOD("get_space_2d"), &ECSWorld::get_space_2d);
	ClassDB::bind_method(D_METHOD("set_space_3d", "space"), &ECSWorld::set_space_3d);
	ClassDB::bind_method(D_METHOD("get_space_3d"), &ECSWorld::get_space_3d);
	ClassDB::bind_method(D_METHOD("set_shape_2d", "entity", "shape"), &ECSWorld::set_shape_2d);
	ClassDB::bind_method(D_METHOD("set_shape_3d", "entity", "shape"), &ECSWorld::set_shape_3d);
	ClassDB::bind_method(D_METHOD("release_resource", "resource"), &ECSWorld::release_resource);

	ClassDB::bind_method(D_METHOD("set_name", "name"), &ECSWorld::set_name);
	ClassDB::bind_method(D_METHOD("get_name"), &ECSWorld::get_name);
	ClassDB::bind_method(D_METHOD("get_memory_usage"), &ECSWorld::get_memory_usage);
	ClassDB::bind_method(D_METHOD("get_profile"), &ECSWorld::get_profile);
	ClassDB::bind_static_method("ECSWorld", D_METHOD("get_all_profiles"), &ECSWorld::get_all_profiles);
	ClassDB::bind_method(D_METHOD("get_archetype_count"), &ECSWorld::get_archetype_count);
	ClassDB::bind_method(D_METHOD("get_archetype_info"), &ECSWorld::get_archetype_info);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multiplayer_peer", PROPERTY_HINT_RESOURCE_TYPE, "MultiplayerPeer", PROPERTY_USAGE_NONE), "set_multiplayer_peer", "get_multiplayer_peer");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "name"), "set_name", "get_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sync_interval"), "set_sync_interval", "get_sync_interval");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "viewport_size"), "set_viewport_size", "get_viewport_size");

	ADD_SIGNAL(MethodInfo("peer_connected", PropertyInfo(Variant::INT, "id")));
	ADD_SIGNAL(MethodInfo("peer_disconnected", PropertyInfo(Variant::INT, "id")));
	ADD_SIGNAL(MethodInfo("entity_spawned", PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::INT, "net_id")));
	ADD_SIGNAL(MethodInfo("entity_despawned", PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::INT, "net_id")));
	ADD_SIGNAL(MethodInfo("authority_changed", PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::INT, "peer")));
	ADD_SIGNAL(MethodInfo("command_received", PropertyInfo(Variant::INT, "sender"), PropertyInfo(Variant::STRING_NAME, "name"), PropertyInfo(Variant::ARRAY, "args")));

	BIND_ENUM_CONSTANT(STAGE_PRE_UPDATE);
	BIND_ENUM_CONSTANT(STAGE_UPDATE);
	BIND_ENUM_CONSTANT(STAGE_POST_UPDATE);
	BIND_ENUM_CONSTANT(STAGE_RENDER);
	BIND_ENUM_CONSTANT(STAGE_PRE_PHYSICS);
	BIND_ENUM_CONSTANT(STAGE_PHYSICS);
	BIND_ENUM_CONSTANT(STAGE_POST_PHYSICS);
	BIND_ENUM_CONSTANT(STAGE_NETWORK);

	BIND_ENUM_CONSTANT(EVENT_ENTITY_CREATE);
	BIND_ENUM_CONSTANT(EVENT_ENTITY_DESTROY);
	BIND_ENUM_CONSTANT(EVENT_COMPONENT_ADDED);
	BIND_ENUM_CONSTANT(EVENT_COMPONENT_REMOVED);
	BIND_ENUM_CONSTANT(EVENT_PRE_UPDATE);
	BIND_ENUM_CONSTANT(EVENT_UPDATE);
	BIND_ENUM_CONSTANT(EVENT_LATE_UPDATE);
	BIND_ENUM_CONSTANT(EVENT_RENDER);
	BIND_ENUM_CONSTANT(EVENT_PRE_FIXED_UPDATE);
	BIND_ENUM_CONSTANT(EVENT_FIXED_UPDATE);
	BIND_ENUM_CONSTANT(EVENT_LATE_FIXED_UPDATE);
	BIND_ENUM_CONSTANT(EVENT_PEER_CONNECTED);
	BIND_ENUM_CONSTANT(EVENT_PEER_DISCONNECTED);
	BIND_ENUM_CONSTANT(EVENT_ENTITY_SPAWNED);
	BIND_ENUM_CONSTANT(EVENT_ENTITY_DESPAWNED);
	BIND_ENUM_CONSTANT(EVENT_AUTHORITY_CHANGED);

	BIND_BITFIELD_FLAG(COMPONENT_FLAG_NONE);
	BIND_BITFIELD_FLAG(COMPONENT_FLAG_SPARSE);
	BIND_BITFIELD_FLAG(COMPONENT_FLAG_REPLICATED);
	BIND_BITFIELD_FLAG(COMPONENT_FLAG_OWNER_AUTHORITY);
	BIND_BITFIELD_FLAG(COMPONENT_FLAG_UNRELIABLE);

	BIND_CONSTANT(FIELD_TYPE_ENTITY);
	BIND_CONSTANT(NULL_ENTITY);
}

/* ECSQuery */

int64_t ECSQuery::count() {
	return query.count();
}

PackedInt64Array ECSQuery::get_entities() {
	PackedInt64Array r;
	r.resize(query.count());
	int64_t *w = r.ptrw();
	uint32_t i = 0;
	query.for_each([&](Entity p_entity, Archetype &, uint32_t) {
		w[i++] = int64_t(p_entity.id);
	});
	return r;
}

bool ECSQuery::matches(int64_t p_entity) const {
	return query.matches(Entity(uint64_t(p_entity)));
}

void ECSQuery::for_each(const Callable &p_callable) {
	ERR_FAIL_COND(!p_callable.is_valid());
	// Collect first: the callable may mutate the world (structural changes
	// would invalidate the iteration).
	LocalVector<Entity> entities;
	query.get_entities(entities);
	for (const Entity &e : entities) {
		Variant arg = int64_t(e.id);
		const Variant *args[1] = { &arg };
		Variant ret;
		Callable::CallError ce;
		p_callable.callp(args, 1, ret, ce);
		if (ce.error != Callable::CallError::CALL_OK) {
			ERR_PRINT(vformat("ECSQuery.for_each callable failed: %s", Variant::get_callable_error_text(p_callable, args, 1, ce)));
			return;
		}
	}
}

Variant ECSQuery::get_field_array(const Variant &p_component, const StringName &p_field) {
	World *w = world->get_native();
	ComponentId id = world->_resolve(p_component);
	ERR_FAIL_COND_V_MSG(id == INVALID_COMPONENT, Variant(), vformat("Unknown component '%s'.", String(p_component)));
	const ComponentType *t = w->get_component_type(id);
	int32_t fi = t->find_field(p_field);
	ERR_FAIL_COND_V_MSG(fi < 0, Variant(), vformat("Component '%s' has no field '%s'.", t->name, p_field));
	const ComponentField &f = t->fields[fi];
	ERR_FAIL_COND_V_MSG(!query.get_include_mask().test(id) && !w->is_sparse_component(id), Variant(), "Component is not part of the query's include set.");

	uint32_t n = query.count();
	const bool sparse = w->is_sparse_component(id);

	// Gather pointers first.
	LocalVector<const uint8_t *> ptrs;
	ptrs.reserve(n);
	if (sparse) {
		SparseComponentStorage *s = w->get_sparse_storage(id);
		query.for_each([&](Entity p_entity, Archetype &, uint32_t) {
			const uint8_t *c = (const uint8_t *)s->get(p_entity.index());
			ptrs.push_back(c ? c + f.offset : nullptr);
		});
	} else {
		query.for_each_archetype([&](Archetype &a) {
			int32_t col = a.get_column_index(id);
			const Archetype::Column &c = a.get_column(uint32_t(col));
			uint32_t rows = a.size();
			const Entity *ents = a.entities_ptr();
			for (uint32_t r = 0; r < rows; r++) {
				if (query.has_sparse_terms() && !query.matches_sparse(ents[r])) {
					continue;
				}
				ptrs.push_back(c.data + size_t(r) * c.stride + f.offset);
			}
		});
	}
	n = ptrs.size();

#define PACK(m_array, m_type, m_conv)            \
	{                                            \
		m_array arr;                             \
		arr.resize(n);                           \
		auto *wp = arr.ptrw();                   \
		for (uint32_t i = 0; i < n; i++) {       \
			wp[i] = ptrs[i] ? m_conv : m_type(); \
		}                                        \
		return arr;                              \
	}

	if (f.kind == FIELD_KIND_TRIVIAL) {
		switch (f.type) {
			case Variant::FLOAT:
				if (f.size == 4) {
					PACK(PackedFloat64Array, double, double(*(const float *)ptrs[i]));
				}
				PACK(PackedFloat64Array, double, *(const double *)ptrs[i]);
			case Variant::INT:
				switch (f.size) {
					case 1:
						PACK(PackedInt64Array, int64_t, int64_t(*(const int8_t *)ptrs[i]));
					case 2:
						PACK(PackedInt64Array, int64_t, int64_t(*(const int16_t *)ptrs[i]));
					case 4:
						PACK(PackedInt64Array, int64_t, int64_t(*(const int32_t *)ptrs[i]));
					default:
						PACK(PackedInt64Array, int64_t, *(const int64_t *)ptrs[i]);
				}
			case Variant::BOOL:
				PACK(PackedByteArray, uint8_t, uint8_t(*(const bool *)ptrs[i] ? 1 : 0));
			case Variant::VECTOR2:
				PACK(PackedVector2Array, Vector2, *(const Vector2 *)ptrs[i]);
			case Variant::VECTOR3:
				PACK(PackedVector3Array, Vector3, *(const Vector3 *)ptrs[i]);
			case Variant::VECTOR4:
				PACK(PackedVector4Array, Vector4, *(const Vector4 *)ptrs[i]);
			case Variant::COLOR:
				PACK(PackedColorArray, Color, *(const Color *)ptrs[i]);
			default:
				break;
		}
	} else if (f.kind == FIELD_KIND_ENTITY) {
		PACK(PackedInt64Array, int64_t, int64_t(((const Entity *)ptrs[i])->id));
	}
#undef PACK

	Array arr;
	arr.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		arr[i] = ptrs[i] ? t->get_field(ptrs[i] - f.offset, uint32_t(fi)) : Variant();
	}
	return arr;
}

int64_t ECSQuery::set_field_array(const Variant &p_component, const StringName &p_field, const Variant &p_values) {
	World *w = world->get_native();
	ComponentId id = world->_resolve(p_component);
	ERR_FAIL_COND_V_MSG(id == INVALID_COMPONENT, 0, vformat("Unknown component '%s'.", String(p_component)));
	const ComponentType *t = w->get_component_type(id);
	int32_t fi = t->find_field(p_field);
	ERR_FAIL_COND_V_MSG(fi < 0, 0, vformat("Component '%s' has no field '%s'.", t->name, p_field));
	ERR_FAIL_COND_V_MSG(!query.get_include_mask().test(id) && !w->is_sparse_component(id), 0, "Component is not part of the query's include set.");

	// Any Array or packed array works: values are read with Variant::get(i).
	Variant values = p_values;
	int64_t count = 0;
	{
		ERR_FAIL_COND_V_MSG(!values.is_array(), 0, "Values must be an Array or a packed array.");
		Callable::CallError ce;
		Variant r;
		values.callp(SNAME("size"), nullptr, 0, r, ce);
		ERR_FAIL_COND_V_MSG(ce.error != Callable::CallError::CALL_OK, 0, "Values must be an Array or a packed array.");
		count = r;
	}

	const uint32_t tick = w->get_tick();
	int64_t i = 0;
	const bool sparse = w->is_sparse_component(id);
	if (sparse) {
		SparseComponentStorage *s = w->get_sparse_storage(id);
		query.for_each([&](Entity p_entity, Archetype &, uint32_t) {
			if (i >= count) {
				return;
			}
			uint8_t *c = (uint8_t *)s->get(p_entity.index());
			if (c) {
				t->set_field(c, uint32_t(fi), values.get(i));
				s->mark_changed(p_entity.index(), tick);
			}
			i++;
		});
	} else {
		query.for_each_archetype([&](Archetype &a) {
			int32_t col = a.get_column_index(id);
			Archetype::Column &c = a.get_column(uint32_t(col));
			uint32_t rows = a.size();
			const Entity *ents = a.entities_ptr();
			for (uint32_t r = 0; r < rows && i < count; r++) {
				if (query.has_sparse_terms() && !query.matches_sparse(ents[r])) {
					continue;
				}
				t->set_field(c.data + size_t(r) * c.stride, uint32_t(fi), values.get(i));
				c.change_ticks[r] = tick;
				i++;
			}
		});
	}
	return i;
}

Array ECSQuery::get_include() const {
	return ECSWorld::_mask_to_names(world->get_native(), query.get_include_mask());
}

Array ECSQuery::get_exclude() const {
	return ECSWorld::_mask_to_names(world->get_native(), query.get_exclude_mask());
}

void ECSQuery::_bind_methods() {
	ClassDB::bind_method(D_METHOD("count"), &ECSQuery::count);
	ClassDB::bind_method(D_METHOD("get_entities"), &ECSQuery::get_entities);
	ClassDB::bind_method(D_METHOD("matches", "entity"), &ECSQuery::matches);
	ClassDB::bind_method(D_METHOD("for_each", "callable"), &ECSQuery::for_each);
	ClassDB::bind_method(D_METHOD("get_field_array", "component", "field"), &ECSQuery::get_field_array);
	ClassDB::bind_method(D_METHOD("set_field_array", "component", "field", "values"), &ECSQuery::set_field_array);
	ClassDB::bind_method(D_METHOD("get_include"), &ECSQuery::get_include);
	ClassDB::bind_method(D_METHOD("get_exclude"), &ECSQuery::get_exclude);
	ClassDB::bind_method(D_METHOD("get_world"), &ECSQuery::get_world);
}
