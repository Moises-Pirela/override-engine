/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "bindings/ecs_main_loop.h"
#include "bindings/ecs_profiler.h"
#include "bindings/ecs_system_bind.h"
#include "bindings/ecs_world_bind.h"
#include "resources/ecs_event_library.h"
#include "resources/ecs_prefab.h"

#include "core/config/project_settings.h"

#ifdef TOOLS_ENABLED
#include "editor/ecs_editor_plugin.h"
#include "editor/ecs_events_panel.h"
#include "editor/ecs_profiler_panel.h"
#include "editor/plugins/editor_plugin.h"
#endif

void initialize_ecs_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(ECSWorld);
		GDREGISTER_CLASS(ECSQuery);
		GDREGISTER_CLASS(ECSSystem);
		GDREGISTER_CLASS(ECSMainLoop);
		GDREGISTER_CLASS(ECSComponentSchema);
		GDREGISTER_CLASS(ECSPrefab);

		// Component schemas registered in every ECSWorld and offered by the prefab editor.
		GLOBAL_DEF(PropertyInfo(Variant::PACKED_STRING_ARRAY, ECSComponentLibrary::SETTING_SCHEMAS, PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:*.tres,*.res", Variant::STRING, PROPERTY_HINT_FILE)), PackedStringArray());
		ECSComponentLibrary::reload_project_schemas();

		// Custom event types ("OnPlayerHeal", ...) registered in every ECSWorld; edited from the ECS Events panel.
		GLOBAL_DEF(PropertyInfo(Variant::PACKED_STRING_ARRAY, ECSEventLibrary::SETTING_EVENTS), PackedStringArray());
		ECSEventLibrary::reload_project_events();

		ECSProfiler::initialize();
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(ECSEventsPanel);
		GDREGISTER_INTERNAL_CLASS(ECSProfilerPanel);
		GDREGISTER_INTERNAL_CLASS(ECSEditorDebugger);
		EditorPlugins::add_by_type<ECSEditorPlugin>();
	}
#endif
}

void uninitialize_ecs_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		ECSProfiler::deinitialize();
		ECSComponentLibrary::cleanup();
		ECSEventLibrary::cleanup();
	}
}
