/**************************************************************************/
/*  ecs_system_bind.cpp                                                   */
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

#include "ecs_system_bind.h"

#include "../core/ecs_world.h"
#include "ecs_world_bind.h"

void ECSSystem::_dispatch(ecs::World &p_world, const ecs::Event &p_event, void *p_userdata) {
	ECSSystem *self = static_cast<ECSSystem *>(p_userdata);
	if (!self->enabled) {
		return;
	}
	Dictionary ev = p_world.event_to_dictionary(p_event);
	self->GDVIRTUAL_CALL(_process_event, ev);
}

Ref<ECSWorld> ECSSystem::get_world() const {
	if (world_id.is_null()) {
		return Ref<ECSWorld>();
	}
	return Ref<ECSWorld>(Object::cast_to<ECSWorld>(ObjectDB::get_instance(world_id)));
}

void ECSSystem::set_order(int p_order) {
	order = p_order;
	ERR_FAIL_COND_MSG(is_registered(), "Order can't be changed while the system is registered; remove it first.");
}

void ECSSystem::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	Ref<ECSWorld> w = get_world();
	if (w.is_valid() && subscription >= 0) {
		w->get_native()->set_subscription_enabled(ecs::SubscriptionId(subscription), p_enabled);
	}
}

Array ECSSystem::get_events() const {
	Array r;
	GDVIRTUAL_CALL(_get_events, r);
	return r;
}

Array ECSSystem::get_component_filter() const {
	Array r;
	GDVIRTUAL_CALL(_get_component_filter, r);
	return r;
}

void ECSSystem::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_world"), &ECSSystem::get_world);
	ClassDB::bind_method(D_METHOD("is_registered"), &ECSSystem::is_registered);
	ClassDB::bind_method(D_METHOD("set_order", "order"), &ECSSystem::set_order);
	ClassDB::bind_method(D_METHOD("get_order"), &ECSSystem::get_order);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &ECSSystem::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &ECSSystem::is_enabled);
	ClassDB::bind_method(D_METHOD("get_events"), &ECSSystem::get_events);
	ClassDB::bind_method(D_METHOD("get_component_filter"), &ECSSystem::get_component_filter);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "order"), "set_order", "get_order");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");

	GDVIRTUAL_BIND(_get_events);
	GDVIRTUAL_BIND(_get_component_filter);
	GDVIRTUAL_BIND(_process_event, "event");
	GDVIRTUAL_BIND(_on_registered);
	GDVIRTUAL_BIND(_on_unregistered);
}
