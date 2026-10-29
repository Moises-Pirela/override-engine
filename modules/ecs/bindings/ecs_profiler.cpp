/**************************************************************************/
/*  ecs_profiler.cpp                                                      */
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

#include "ecs_profiler.h"

#include "../core/ecs_world.h"

#include "core/debugger/engine_debugger.h"
#include "core/os/os.h"

static Ref<ECSProfiler> ecs_profiler;

Array ECSProfiler::collect() {
	Array out;
	LocalVector<ecs::World *> worlds;
	ecs::World::get_all_worlds(worlds);
	for (ecs::World *w : worlds) {
		out.push_back(w->get_profile());
	}
	return out;
}

void ECSProfiler::toggle(bool p_enable, const Array &p_opts) {
	enabled = p_enable;
	last_send_msec = 0;
	if (p_opts.size() > 0 && p_opts[0].get_type() == Variant::INT) {
		interval_msec = MAX(int(p_opts[0]), 16);
	}
}

void ECSProfiler::tick(double p_frame_time, double p_process_time, double p_physics_time, double p_physics_frame_time) {
	if (!enabled || !EngineDebugger::is_active()) {
		return;
	}
	uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (now - last_send_msec < interval_msec) {
		return;
	}
	last_send_msec = now;
	EngineDebugger::get_singleton()->send_message("ecs:profile", collect());
}

void ECSProfiler::initialize() {
	if (ecs_profiler.is_valid()) {
		return;
	}
	ecs_profiler.instantiate();
	ecs_profiler->bind("ecs");
}

void ECSProfiler::deinitialize() {
	if (ecs_profiler.is_valid()) {
		ecs_profiler->unbind();
		ecs_profiler.unref();
	}
}
