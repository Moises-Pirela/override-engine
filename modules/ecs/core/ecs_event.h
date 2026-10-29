/**************************************************************************/
/*  ecs_event.h                                                           */
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

#include "ecs_bitset.h"
#include "ecs_entity.h"

#include "core/string/string_name.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"

namespace ecs {

class World;

// Events are how systems hook into the engine lifecycle (in the spirit of
// CryEngine's entity events): a system declares the set of events it wants
// (an EventMask) and receives them through a single handler. Built-in events
// cover entity/component lifecycle, the frame and fixed update phases and
// networking; user events are registered by name on top of them.
typedef uint32_t EventId;
constexpr uint32_t MAX_EVENTS = 256;
constexpr EventId INVALID_EVENT = 0xFFFFFFFF;
typedef ECSBitSet<MAX_EVENTS> EventMask;

enum BuiltinEvent : EventId {
	// Entity lifecycle. Creation / component-added events are queued and
	// delivered once the entity has been fully initialized (next flush);
	// destruction / component-removed events are delivered immediately,
	// while the data is still available.
	EVENT_ENTITY_CREATE,
	EVENT_ENTITY_DESTROY,
	EVENT_COMPONENT_ADDED,
	EVENT_COMPONENT_REMOVED,
	// Frame (World::update). Delivered after the systems of the matching stage.
	EVENT_PRE_UPDATE,
	EVENT_UPDATE,
	EVENT_LATE_UPDATE,
	EVENT_RENDER,
	// Fixed tick (World::physics_update).
	EVENT_PRE_FIXED_UPDATE,
	EVENT_FIXED_UPDATE,
	EVENT_LATE_FIXED_UPDATE,
	// Networking.
	EVENT_PEER_CONNECTED,
	EVENT_PEER_DISCONNECTED,
	EVENT_ENTITY_SPAWNED,
	EVENT_ENTITY_DESPAWNED,
	EVENT_AUTHORITY_CHANGED,
	EVENT_BUILTIN_MAX,
	// User events are registered from here on.
	EVENT_CUSTOM_BASE = 32,
};

struct Event {
	EventId id = INVALID_EVENT;
	Entity entity; // Subject entity, if any.
	ComponentId component = INVALID_COMPONENT; // For component events.
	double delta = 0.0; // For update events.
	int32_t peer = 0; // For network events.
	uint32_t net_id = 0; // For network entity events.
	Variant payload; // User data for custom events.
	void *data = nullptr; // Native user data for custom events (not owned).

	Event() {}
	explicit Event(EventId p_id) :
			id(p_id) {}
	static Event for_entity(EventId p_id, Entity p_entity) {
		Event e(p_id);
		e.entity = p_entity;
		return e;
	}
};

typedef void (*EventHandler)(World &p_world, const Event &p_event, void *p_userdata);
typedef uint32_t SubscriptionId;
constexpr SubscriptionId INVALID_SUBSCRIPTION = 0xFFFFFFFF;

// A subscription routes the events in `events` to a native handler or a
// script Callable (called with a Dictionary describing the event). Entity
// events can be restricted to one entity and/or to entities that have all
// the components in `filter`.
struct SubscriptionDesc {
	StringName name;
	EventMask events;
	EventHandler func = nullptr;
	void *userdata = nullptr;
	Callable callable;
	ComponentMask filter;
	Entity entity;
	int32_t order = 0;
	bool enabled = true;
};

} // namespace ecs
