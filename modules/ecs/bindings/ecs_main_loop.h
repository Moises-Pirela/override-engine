/**************************************************************************/
/*  ecs_main_loop.h                                                       */
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

#include "ecs_world_bind.h"

#include "core/input/input_event.h"
#include "core/os/main_loop.h"
#include "servers/display/display_server.h"

// A MainLoop that runs an ECSWorld without a SceneTree. It creates a
// rendering viewport attached to the main OS window, a 2D canvas and a 3D
// scenario, and drives the world's frame / physics stages. Input events from
// the window are forwarded to `_input`.
//
// Set `application/run/main_loop_type` to "ECSMainLoop" (or a script
// extending it) to use it for a project.
class ECSMainLoop : public MainLoop {
	GDCLASS(ECSMainLoop, MainLoop);

	Ref<ECSWorld> world;
	RID viewport;
	RID canvas;
	RID scenario;
	RID space_2d;
	RID space_3d;
	bool quit_requested = false;
	bool headless = false;

	void _window_event(DisplayServer::WindowEvent p_event);
	void _window_input(const Ref<InputEvent> &p_event);
	void _window_rect_changed(const Rect2i &p_rect);
	void _update_viewport_size();

protected:
	static void _bind_methods();

	GDVIRTUAL1(_input, Ref<InputEvent>)

public:
	virtual void initialize() override;
	virtual bool physics_process(double p_time) override;
	virtual bool process(double p_time) override;
	virtual void finalize() override;

	Ref<ECSWorld> get_world() const { return world; }
	void set_world(const Ref<ECSWorld> &p_world);
	RID get_viewport_rid() const { return viewport; }
	RID get_canvas_rid() const { return canvas; }
	RID get_scenario_rid() const { return scenario; }
	RID get_space_2d_rid() const { return space_2d; }
	RID get_space_3d_rid() const { return space_3d; }
	void quit();

	ECSMainLoop();
	~ECSMainLoop();
};
