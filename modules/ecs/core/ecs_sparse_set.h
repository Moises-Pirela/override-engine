/**************************************************************************/
/*  ecs_sparse_set.h                                                      */
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

#include "core/error/error_macros.h"
#include "core/os/memory.h"
#include "core/templates/local_vector.h"

#include <type_traits>
#include <utility>

// Paged sparse set: O(1) insert / erase / lookup by a dense-ish integer key
// (entity index, archetype id, network id, ...) while keeping the values
// packed contiguously so they can be iterated linearly.
//
// The sparse side is allocated in pages so that very large or sparsely used
// key ranges don't cost memory up front. The dense side is two parallel
// arrays (keys and values) which makes the value storage SoA-friendly.
//
// Used throughout the ECS for:
//  - entity index -> sparse component storage (components flagged SPARSE),
//  - component type -> set of archetypes storing it (query acceleration),
//  - per-query archetype caches,
//  - network id -> entity maps in the replicator.
template <typename T, uint32_t PAGE_SIZE = 1024>
class ECSSparseSet {
	static_assert((PAGE_SIZE & (PAGE_SIZE - 1)) == 0, "PAGE_SIZE must be a power of two.");

public:
	static constexpr uint32_t INVALID = 0xFFFFFFFF;

private:
	static constexpr uint32_t _log2(uint32_t p_v) { return p_v <= 1 ? 0 : 1 + _log2(p_v >> 1); }
	static constexpr uint32_t PAGE_SHIFT = _log2(PAGE_SIZE);
	static constexpr uint32_t PAGE_MASK = PAGE_SIZE - 1;

	LocalVector<uint32_t *> pages;
	LocalVector<uint32_t> dense_keys;
	LocalVector<T> dense_values;

	_FORCE_INLINE_ uint32_t *_get_page(uint32_t p_key, bool p_create) {
		uint32_t page = p_key >> PAGE_SHIFT;
		if (page >= pages.size()) {
			if (!p_create) {
				return nullptr;
			}
			uint32_t old = pages.size();
			pages.resize(page + 1);
			for (uint32_t i = old; i < pages.size(); i++) {
				pages[i] = nullptr;
			}
		}
		if (pages[page] == nullptr) {
			if (!p_create) {
				return nullptr;
			}
			uint32_t *p = (uint32_t *)memalloc(sizeof(uint32_t) * PAGE_SIZE);
			for (uint32_t i = 0; i < PAGE_SIZE; i++) {
				p[i] = INVALID;
			}
			pages[page] = p;
		}
		return pages[page];
	}

	_FORCE_INLINE_ const uint32_t *_get_page_const(uint32_t p_key) const {
		uint32_t page = p_key >> PAGE_SHIFT;
		if (page >= pages.size()) {
			return nullptr;
		}
		return pages[page];
	}

public:
	_FORCE_INLINE_ uint32_t size() const { return dense_keys.size(); }
	_FORCE_INLINE_ bool is_empty() const { return dense_keys.is_empty(); }

	_FORCE_INLINE_ bool has(uint32_t p_key) const {
		const uint32_t *page = _get_page_const(p_key);
		return page && page[p_key & PAGE_MASK] != INVALID;
	}

	// Dense index of a key, or INVALID.
	_FORCE_INLINE_ uint32_t index_of(uint32_t p_key) const {
		const uint32_t *page = _get_page_const(p_key);
		return page ? page[p_key & PAGE_MASK] : INVALID;
	}

	_FORCE_INLINE_ T *getptr(uint32_t p_key) {
		uint32_t idx = index_of(p_key);
		return idx == INVALID ? nullptr : &dense_values[idx];
	}
	_FORCE_INLINE_ const T *getptr(uint32_t p_key) const {
		uint32_t idx = index_of(p_key);
		return idx == INVALID ? nullptr : &dense_values[idx];
	}

	_FORCE_INLINE_ T &get(uint32_t p_key) {
		uint32_t idx = index_of(p_key);
		CRASH_COND(idx == INVALID);
		return dense_values[idx];
	}
	_FORCE_INLINE_ const T &get(uint32_t p_key) const {
		uint32_t idx = index_of(p_key);
		CRASH_COND(idx == INVALID);
		return dense_values[idx];
	}

	// Inserts or overwrites. Returns the dense index.
	uint32_t insert(uint32_t p_key, T p_value) {
		uint32_t *page = _get_page(p_key, true);
		uint32_t &slot = page[p_key & PAGE_MASK];
		if (slot != INVALID) {
			dense_values[slot] = std::move(p_value);
			return slot;
		}
		slot = dense_keys.size();
		dense_keys.push_back(p_key);
		dense_values.push_back(std::move(p_value));
		return slot;
	}

	// Inserts a default-constructed value if the key is missing and returns it.
	T &get_or_insert(uint32_t p_key) {
		uint32_t *page = _get_page(p_key, true);
		uint32_t &slot = page[p_key & PAGE_MASK];
		if (slot == INVALID) {
			slot = dense_keys.size();
			dense_keys.push_back(p_key);
			dense_values.push_back(T());
		}
		return dense_values[slot];
	}

	// Swap-remove. Returns true if the key was present.
	bool erase(uint32_t p_key) {
		uint32_t *page = _get_page(p_key, false);
		if (!page) {
			return false;
		}
		uint32_t &slot = page[p_key & PAGE_MASK];
		if (slot == INVALID) {
			return false;
		}
		uint32_t idx = slot;
		uint32_t last = dense_keys.size() - 1;
		if (idx != last) {
			uint32_t moved_key = dense_keys[last];
			dense_keys[idx] = moved_key;
			dense_values[idx] = std::move(dense_values[last]);
			_get_page(moved_key, false)[moved_key & PAGE_MASK] = idx;
		}
		dense_keys.resize(last);
		dense_values.resize(last);
		slot = INVALID;
		return true;
	}

	void clear() {
		for (uint32_t i = 0; i < pages.size(); i++) {
			if (pages[i]) {
				memfree(pages[i]);
				pages[i] = nullptr;
			}
		}
		pages.clear();
		dense_keys.clear();
		dense_values.clear();
	}

	void reserve(uint32_t p_size) {
		dense_keys.reserve(p_size);
		dense_values.reserve(p_size);
	}

	uint64_t get_memory_usage() const {
		uint64_t bytes = uint64_t(pages.size()) * sizeof(uint32_t *);
		for (uint32_t i = 0; i < pages.size(); i++) {
			if (pages[i]) {
				bytes += uint64_t(PAGE_SIZE) * sizeof(uint32_t);
			}
		}
		bytes += uint64_t(dense_keys.size()) * sizeof(uint32_t) + uint64_t(dense_values.size()) * sizeof(T);
		return bytes;
	}

	// Dense access (iteration order is unspecified but stable between mutations).
	_FORCE_INLINE_ const uint32_t *keys() const { return dense_keys.ptr(); }
	_FORCE_INLINE_ T *values() { return dense_values.ptr(); }
	_FORCE_INLINE_ const T *values() const { return dense_values.ptr(); }
	_FORCE_INLINE_ uint32_t key_at(uint32_t p_dense) const { return dense_keys[p_dense]; }
	_FORCE_INLINE_ T &value_at(uint32_t p_dense) { return dense_values[p_dense]; }
	_FORCE_INLINE_ const T &value_at(uint32_t p_dense) const { return dense_values[p_dense]; }

	void _copy_from(const ECSSparseSet &p_other) {
		dense_keys = p_other.dense_keys;
		dense_values = p_other.dense_values;
		pages.resize(p_other.pages.size());
		for (uint32_t i = 0; i < pages.size(); i++) {
			if (p_other.pages[i]) {
				pages[i] = (uint32_t *)memalloc(sizeof(uint32_t) * PAGE_SIZE);
				memcpy(pages[i], p_other.pages[i], sizeof(uint32_t) * PAGE_SIZE);
			} else {
				pages[i] = nullptr;
			}
		}
	}

	ECSSparseSet() {}
	ECSSparseSet(const ECSSparseSet &p_other) {
		_copy_from(p_other);
	}
	ECSSparseSet &operator=(const ECSSparseSet &p_other) {
		if (this != &p_other) {
			clear();
			_copy_from(p_other);
		}
		return *this;
	}
	ECSSparseSet(ECSSparseSet &&p_other) {
		pages = std::move(p_other.pages);
		dense_keys = std::move(p_other.dense_keys);
		dense_values = std::move(p_other.dense_values);
		p_other.pages.clear();
	}
	ECSSparseSet &operator=(ECSSparseSet &&p_other) {
		if (this != &p_other) {
			clear();
			pages = std::move(p_other.pages);
			dense_keys = std::move(p_other.dense_keys);
			dense_values = std::move(p_other.dense_values);
			p_other.pages.clear();
		}
		return *this;
	}
	~ECSSparseSet() {
		clear();
	}
};
