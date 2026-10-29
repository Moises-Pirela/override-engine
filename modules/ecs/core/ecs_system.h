/**************************************************************************/
/*  ecs_system.h                                                          */
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

#include "ecs_entity.h"
#include "ecs_event.h"

#include "core/string/string_name.h"
#include "core/variant/callable.h"

namespace ecs {

class World;
class CommandBuffer;

// Execution stages. Frame stages run from World::update(), fixed stages from
// World::physics_update(). Inside a stage, systems are ordered by `order`
// and systems sharing the same order value that do not conflict (according
// to their declared read/write component sets) run in parallel on the
// WorkerThreadPool.
enum Stage : uint8_t {
	STAGE_PRE_UPDATE,
	STAGE_UPDATE,
	STAGE_POST_UPDATE,
	STAGE_RENDER, // Push state to the rendering server.
	STAGE_PRE_PHYSICS,
	STAGE_PHYSICS,
	STAGE_POST_PHYSICS,
	STAGE_NETWORK, // Replication runs after this stage in physics_update().
	STAGE_MAX
};

static _FORCE_INLINE_ bool is_fixed_stage(Stage p_stage) {
	return p_stage >= STAGE_PRE_PHYSICS;
}

struct SystemContext {
	World *world = nullptr;
	double delta = 0.0;
	uint32_t tick = 0; // Current world tick.
	uint32_t last_run_tick = 0; // Tick at which this system ran last (0 on first run).
	void *userdata = nullptr;
	CommandBuffer *commands = nullptr; // Deferred structural changes (thread safe).
};

typedef void (*SystemFunc)(SystemContext &p_ctx);
typedef void (*SystemEventFunc)(SystemContext &p_ctx, const Event &p_event);
typedef uint32_t SystemId;
constexpr SystemId INVALID_SYSTEM = 0xFFFFFFFF;

struct SystemDesc {
	StringName name;
	Stage stage = STAGE_UPDATE;
	int32_t order = 0;
	ComponentMask reads;
	ComponentMask writes;
	// Native entry point. Takes precedence over `callable`.
	SystemFunc func = nullptr;
	void *userdata = nullptr;
	// Script entry point, called as callable(delta). Always runs on the main thread.
	Callable callable;
	// When true the system may be executed on a worker thread together with
	// other non conflicting systems of the same stage and order.
	bool thread_safe = false;
	bool enabled = true;

	// Event subscription (CryEngine style "event mask + ProcessEvent"): the
	// system receives every event in `events` through `event_func` (or
	// `event_callable`, called with a Dictionary). Entity events can be
	// restricted to entities that have all components in `event_filter`.
	// A system may use only events, only a stage function, or both.
	EventMask events;
	SystemEventFunc event_func = nullptr;
	Callable event_callable;
	ComponentMask event_filter;
};

} // namespace ecs
