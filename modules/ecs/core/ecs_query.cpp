/**************************************************************************/
/*  ecs_query.cpp                                                         */
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

#include "ecs_query.h"

namespace ecs {

void Query::init(World *p_world, const ComponentMask &p_include, const ComponentMask &p_exclude) {
	world = p_world;
	matched.clear();
	checked_archetypes = 0;
	include_sparse.clear();
	exclude_sparse.clear();

	const ComponentMask &sparse = p_world->get_sparse_mask();
	include_table = p_include - sparse;
	exclude_table = p_exclude - sparse;
	(p_include & sparse).for_each([&](uint32_t p_bit) { include_sparse.push_back(p_bit); });
	(p_exclude & sparse).for_each([&](uint32_t p_bit) { exclude_sparse.push_back(p_bit); });
}

void Query::refresh() {
	DEV_ASSERT(world);
	const LocalVector<Archetype *> &all = world->get_archetypes();
	while (checked_archetypes < all.size()) {
		Archetype *a = all[checked_archetypes++];
		if (_matches(a)) {
			matched.push_back(a);
		}
	}
}

bool Query::matches_sparse(Entity p_entity) const {
	uint32_t idx = p_entity.index();
	for (ComponentId c : include_sparse) {
		if (!world->get_sparse_storage(c)->has(idx)) {
			return false;
		}
	}
	for (ComponentId c : exclude_sparse) {
		if (world->get_sparse_storage(c)->has(idx)) {
			return false;
		}
	}
	return true;
}

bool Query::matches(Entity p_entity) const {
	Archetype *a = world->get_entity_archetype(p_entity);
	if (!a || !_matches(a)) {
		return false;
	}
	return matches_sparse(p_entity);
}

uint32_t Query::count() {
	refresh();
	uint32_t n = 0;
	if (has_sparse_terms()) {
		for (Archetype *a : matched) {
			uint32_t rows = a->size();
			const Entity *ents = a->entities_ptr();
			for (uint32_t row = 0; row < rows; row++) {
				if (matches_sparse(ents[row])) {
					n++;
				}
			}
		}
	} else {
		for (Archetype *a : matched) {
			n += a->size();
		}
	}
	return n;
}

void Query::get_entities(LocalVector<Entity> &r_entities) {
	for_each([&](Entity p_entity, Archetype &, uint32_t) {
		r_entities.push_back(p_entity);
	});
}

struct ParallelRange {
	Archetype *archetype;
	uint32_t begin;
	uint32_t end;
};

struct ParallelTask {
	Query::ParallelFunc fn;
	void *userdata;
	LocalVector<ParallelRange> ranges;
};

static void _parallel_task(void *p_userdata, uint32_t p_index) {
	ParallelTask *task = (ParallelTask *)p_userdata;
	const ParallelRange &r = task->ranges[p_index];
	task->fn(*r.archetype, r.begin, r.end, task->userdata);
}

void Query::for_each_parallel(ParallelFunc p_fn, void *p_userdata, uint32_t p_min_batch_size) {
	refresh();
	ERR_FAIL_COND_MSG(has_sparse_terms(), "Parallel iteration is not supported for queries with sparse component terms.");
	p_min_batch_size = MAX(p_min_batch_size, 1u);

	ParallelTask task;
	task.fn = p_fn;
	task.userdata = p_userdata;
	for (Archetype *a : matched) {
		uint32_t count = a->size();
		for (uint32_t begin = 0; begin < count; begin += p_min_batch_size) {
			ParallelRange r;
			r.archetype = a;
			r.begin = begin;
			r.end = MIN(count, begin + p_min_batch_size);
			task.ranges.push_back(r);
		}
	}
	if (task.ranges.is_empty()) {
		return;
	}
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	if (task.ranges.size() == 1 || pool == nullptr || pool->get_thread_count() <= 1) {
		for (uint32_t i = 0; i < task.ranges.size(); i++) {
			_parallel_task(&task, i);
		}
		return;
	}
	WorkerThreadPool::GroupID group = pool->add_native_group_task(&_parallel_task, &task, int(task.ranges.size()), -1, true, "ECSQuery");
	pool->wait_for_group_task_completion(group);
}

} // namespace ecs
