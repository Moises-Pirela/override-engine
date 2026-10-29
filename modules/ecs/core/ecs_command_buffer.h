/**************************************************************************/
/*  ecs_command_buffer.h                                                  */
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

#include "ecs_component.h"
#include "ecs_entity.h"

#include "core/os/mutex.h"
#include "core/templates/local_vector.h"

namespace ecs {

class World;

// Records structural changes (create/destroy entities, add/remove components)
// so they can be applied later from the main thread, typically at the end of
// a system batch. Systems running in parallel must use this instead of
// mutating the World directly.
class CommandBuffer {
	enum Type : uint8_t {
		CMD_DESTROY_ENTITY,
		CMD_ADD_COMPONENT,
		CMD_REMOVE_COMPONENT,
		CMD_CALLBACK,
	};

	struct Command {
		Type type;
		Entity entity;
		ComponentId component = INVALID_COMPONENT;
		uint32_t data_offset = 0; // Offset into `data` for CMD_ADD_COMPONENT payloads (or 0xFFFFFFFF).
		void (*callback)(World &p_world, void *p_userdata) = nullptr;
		void *userdata = nullptr;
	};

	Mutex mutex;
	LocalVector<Command> commands;
	LocalVector<uint8_t> data;
	LocalVector<const ComponentType *> data_types; // Parallel to commands for payload destruction.

	static constexpr uint32_t NO_DATA = 0xFFFFFFFF;

public:
	void destroy_entity(Entity p_entity) {
		MutexLock lock(mutex);
		Command c;
		c.type = CMD_DESTROY_ENTITY;
		c.entity = p_entity;
		c.data_offset = NO_DATA;
		commands.push_back(c);
		data_types.push_back(nullptr);
	}

	// p_init may be null, in which case the component is default constructed.
	void add_component(Entity p_entity, const ComponentType &p_type, const void *p_init = nullptr) {
		MutexLock lock(mutex);
		Command c;
		c.type = CMD_ADD_COMPONENT;
		c.entity = p_entity;
		c.component = p_type.id;
		c.data_offset = NO_DATA;
		if (p_init && p_type.size) {
			uint32_t align = MAX(p_type.align, 16u);
			uint32_t offset = (data.size() + align - 1) & ~(align - 1);
			data.resize(offset + p_type.size);
			p_type.copy_construct(data.ptr() + offset, p_init);
			c.data_offset = offset;
		}
		commands.push_back(c);
		data_types.push_back(&p_type);
	}

	void remove_component(Entity p_entity, ComponentId p_component) {
		MutexLock lock(mutex);
		Command c;
		c.type = CMD_REMOVE_COMPONENT;
		c.entity = p_entity;
		c.component = p_component;
		c.data_offset = NO_DATA;
		commands.push_back(c);
		data_types.push_back(nullptr);
	}

	// Arbitrary deferred work executed on the main thread.
	void callback(void (*p_callback)(World &p_world, void *p_userdata), void *p_userdata) {
		MutexLock lock(mutex);
		Command c;
		c.type = CMD_CALLBACK;
		c.data_offset = NO_DATA;
		c.callback = p_callback;
		c.userdata = p_userdata;
		commands.push_back(c);
		data_types.push_back(nullptr);
	}

	_FORCE_INLINE_ bool is_empty() const { return commands.is_empty(); }
	_FORCE_INLINE_ uint32_t size() const { return commands.size(); }

	// Applies and clears every recorded command. Must be called from the
	// thread that owns the world.
	void flush(World &p_world);

	~CommandBuffer();
};

} // namespace ecs
