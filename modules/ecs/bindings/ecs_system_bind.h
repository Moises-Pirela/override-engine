/**************************************************************************/
/*  ecs_system_bind.h                                                     */
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

#include "../core/ecs_event.h"

#include "core/object/gdvirtual.gen.inc"
#include "core/object/ref_counted.h"

class ECSWorld;

// Script-side system driven by events, in the spirit of CryEngine's entity
// components (GetEventMask / ProcessEvent): override _get_events() to declare
// the events you want (entity lifecycle, Update / FixedUpdate / LateUpdate,
// networking, custom events) and _process_event() to handle them. Register
// the system with ECSWorld.add_system_object().
class ECSSystem : public RefCounted {
	GDCLASS(ECSSystem, RefCounted);

	friend class ECSWorld;

	ObjectID world_id;
	int64_t subscription = -1;
	int order = 0;
	bool enabled = true;

	static void _dispatch(ecs::World &p_world, const ecs::Event &p_event, void *p_userdata);

protected:
	static void _bind_methods();

	GDVIRTUAL0RC(Array, _get_events)
	GDVIRTUAL0RC(Array, _get_component_filter)
	GDVIRTUAL1(_process_event, Dictionary)
	GDVIRTUAL0(_on_registered)
	GDVIRTUAL0(_on_unregistered)

public:
	Ref<ECSWorld> get_world() const;
	bool is_registered() const { return subscription >= 0; }

	void set_order(int p_order);
	int get_order() const { return order; }
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }

	// Resolved from _get_events() / _get_component_filter(); exposed for introspection.
	Array get_events() const;
	Array get_component_filter() const;
};
