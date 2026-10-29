/**************************************************************************/
/*  ecs_profiler.h                                                        */
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

#include "core/debugger/engine_profiler.h"

// Game side of the ECS profiler: when the editor enables the "ecs" profiler
// of a debug session, snapshots of every live world (entities, memory,
// systems in execution order with timings, archetypes) are sent to the
// editor ten times per second as "ecs:profile" messages.
class ECSProfiler : public EngineProfiler {
	GDSOFTCLASS(ECSProfiler, EngineProfiler);

	bool enabled = false;
	uint64_t last_send_msec = 0;
	uint32_t interval_msec = 100;

public:
	// Profiles of every live ecs::World, in creation order.
	static Array collect();

	virtual void toggle(bool p_enable, const Array &p_opts) override;
	virtual void add(const Array &p_data) override {}
	virtual void tick(double p_frame_time, double p_process_time, double p_physics_time, double p_physics_frame_time) override;

	static void initialize();
	static void deinitialize();
};
