/**************************************************************************/
/*  ecs_main_loop.cpp                                                     */
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

#include "ecs_main_loop.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "servers/rendering/rendering_server.h"

#ifndef PHYSICS_2D_DISABLED
#include "servers/physics_2d/physics_server_2d.h"
#endif
#ifndef PHYSICS_3D_DISABLED
#include "servers/physics_3d/physics_server_3d.h"
#endif

void ECSMainLoop::_window_event(DisplayServer::WindowEvent p_event) {
	switch (p_event) {
		case DisplayServer::WINDOW_EVENT_CLOSE_REQUEST:
		case DisplayServer::WINDOW_EVENT_GO_BACK_REQUEST:
		case DisplayServer::WINDOW_EVENT_FORCE_CLOSE:
			quit_requested = true;
			break;
		default:
			break;
	}
}

void ECSMainLoop::_window_input(const Ref<InputEvent> &p_event) {
	if (p_event.is_null()) {
		return;
	}
	GDVIRTUAL_CALL(_input, p_event);
}

void ECSMainLoop::_window_rect_changed(const Rect2i &p_rect) {
	_update_viewport_size();
}

void ECSMainLoop::_update_viewport_size() {
	if (headless || !viewport.is_valid()) {
		return;
	}
	Size2i size = DisplayServer::get_singleton()->window_get_size(DisplayServer::MAIN_WINDOW_ID);
	RenderingServer::get_singleton()->viewport_set_size(viewport, size.width, size.height);
	RenderingServer::get_singleton()->viewport_attach_to_screen(viewport, Rect2(Vector2(), size), DisplayServer::MAIN_WINDOW_ID);
	world->set_viewport_size(size);
}

void ECSMainLoop::set_world(const Ref<ECSWorld> &p_world) {
	ERR_FAIL_COND(p_world.is_null());
	world = p_world;
	world->set_viewport(viewport);
	world->set_canvas(canvas);
	world->set_scenario(scenario);
	world->set_space_2d(space_2d);
	world->set_space_3d(space_3d);
	_update_viewport_size();
}

void ECSMainLoop::initialize() {
	RenderingServer *rs = RenderingServer::get_singleton();
	DisplayServer *ds = DisplayServer::get_singleton();
	headless = rs == nullptr || ds == nullptr || ds->get_name() == "headless";

	if (rs) {
		viewport = rs->viewport_create();
		canvas = rs->canvas_create();
		scenario = rs->scenario_create();
		rs->viewport_set_active(viewport, true);
		rs->viewport_set_update_mode(viewport, RS::VIEWPORT_UPDATE_ALWAYS);
		rs->viewport_set_clear_mode(viewport, RS::VIEWPORT_CLEAR_ALWAYS);
		rs->viewport_attach_canvas(viewport, canvas);
		rs->viewport_set_scenario(viewport, scenario);
		world->set_viewport(viewport);
		world->set_canvas(canvas);
		world->set_scenario(scenario);
	}

#ifndef PHYSICS_2D_DISABLED
	if (PhysicsServer2D::get_singleton()) {
		space_2d = PhysicsServer2D::get_singleton()->space_create();
		PhysicsServer2D::get_singleton()->space_set_active(space_2d, true);
		world->set_space_2d(space_2d);
	}
#endif
#ifndef PHYSICS_3D_DISABLED
	if (PhysicsServer3D::get_singleton()) {
		space_3d = PhysicsServer3D::get_singleton()->space_create();
		PhysicsServer3D::get_singleton()->space_set_active(space_3d, true);
		world->set_space_3d(space_3d);
	}
#endif

	if (!headless) {
		_update_viewport_size();
		ds->window_set_window_event_callback(callable_mp(this, &ECSMainLoop::_window_event), DisplayServer::MAIN_WINDOW_ID);
		ds->window_set_input_event_callback(callable_mp(this, &ECSMainLoop::_window_input), DisplayServer::MAIN_WINDOW_ID);
		ds->window_set_rect_changed_callback(callable_mp(this, &ECSMainLoop::_window_rect_changed), DisplayServer::MAIN_WINDOW_ID);
	}

	MainLoop::initialize();
}

bool ECSMainLoop::physics_process(double p_time) {
	bool quit = MainLoop::physics_process(p_time);
	world->physics_update(p_time);
	return quit || quit_requested;
}

bool ECSMainLoop::process(double p_time) {
	bool quit = MainLoop::process(p_time);
	world->update(p_time);
	return quit || quit_requested;
}

void ECSMainLoop::finalize() {
	MainLoop::finalize();
	if (!headless && DisplayServer::get_singleton()) {
		DisplayServer *ds = DisplayServer::get_singleton();
		ds->window_set_window_event_callback(Callable(), DisplayServer::MAIN_WINDOW_ID);
		ds->window_set_input_event_callback(Callable(), DisplayServer::MAIN_WINDOW_ID);
		ds->window_set_rect_changed_callback(Callable(), DisplayServer::MAIN_WINDOW_ID);
	}
	// Destroy entities first so render RIDs owned by components are freed
	// before their canvas / scenario, then drop systems and subscriptions so
	// scripts that captured the world don't keep it alive.
	world->clear_entities();
	world->clear_systems();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		if (viewport.is_valid()) {
			rs->free_rid(viewport);
		}
		if (canvas.is_valid()) {
			rs->free_rid(canvas);
		}
		if (scenario.is_valid()) {
			rs->free_rid(scenario);
		}
	}
	viewport = RID();
	canvas = RID();
	scenario = RID();
	world->set_viewport(RID());
	world->set_canvas(RID());
	world->set_scenario(RID());
#ifndef PHYSICS_2D_DISABLED
	if (space_2d.is_valid() && PhysicsServer2D::get_singleton()) {
		PhysicsServer2D::get_singleton()->free_rid(space_2d);
	}
#endif
#ifndef PHYSICS_3D_DISABLED
	if (space_3d.is_valid() && PhysicsServer3D::get_singleton()) {
		PhysicsServer3D::get_singleton()->free_rid(space_3d);
	}
#endif
	space_2d = RID();
	space_3d = RID();
	world->set_space_2d(RID());
	world->set_space_3d(RID());
}

void ECSMainLoop::quit() {
	quit_requested = true;
}

ECSMainLoop::ECSMainLoop() {
	world.instantiate();
	world->set_name("Main");
}

ECSMainLoop::~ECSMainLoop() {
}

void ECSMainLoop::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_world"), &ECSMainLoop::get_world);
	ClassDB::bind_method(D_METHOD("set_world", "world"), &ECSMainLoop::set_world);
	ClassDB::bind_method(D_METHOD("get_viewport_rid"), &ECSMainLoop::get_viewport_rid);
	ClassDB::bind_method(D_METHOD("get_canvas_rid"), &ECSMainLoop::get_canvas_rid);
	ClassDB::bind_method(D_METHOD("get_scenario_rid"), &ECSMainLoop::get_scenario_rid);
	ClassDB::bind_method(D_METHOD("get_space_2d_rid"), &ECSMainLoop::get_space_2d_rid);
	ClassDB::bind_method(D_METHOD("get_space_3d_rid"), &ECSMainLoop::get_space_3d_rid);
	ClassDB::bind_method(D_METHOD("quit"), &ECSMainLoop::quit);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world", PROPERTY_HINT_RESOURCE_TYPE, "ECSWorld", PROPERTY_USAGE_NONE), "set_world", "get_world");

	GDVIRTUAL_BIND(_input, "event");
}
