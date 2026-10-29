/**************************************************************************/
/*  ecs_bitset.h                                                          */
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

#include "core/templates/hashfuncs.h"
#include "core/typedefs.h"

#include <cstring>

// Fixed-width bitset used to describe sets of component types.
//
// An archetype is uniquely identified by the set of component types it stores,
// and a query is a pair (include, exclude) of such sets. Using a fixed-width
// bitset makes archetype matching a handful of AND/compare instructions on
// a few 64-bit words, which is what keeps query (re)matching and archetype
// graph lookups cheap even with thousands of archetypes.
template <uint32_t BITS>
class ECSBitSet {
	static_assert(BITS > 0 && BITS % 64 == 0, "ECSBitSet size must be a positive multiple of 64.");

public:
	static constexpr uint32_t BIT_COUNT = BITS;
	static constexpr uint32_t WORD_COUNT = BITS / 64;

private:
	uint64_t words[WORD_COUNT] = {};

public:
	_FORCE_INLINE_ void set(uint32_t p_bit) {
		DEV_ASSERT(p_bit < BITS);
		words[p_bit >> 6] |= (uint64_t(1) << (p_bit & 63));
	}

	_FORCE_INLINE_ void clear(uint32_t p_bit) {
		DEV_ASSERT(p_bit < BITS);
		words[p_bit >> 6] &= ~(uint64_t(1) << (p_bit & 63));
	}

	_FORCE_INLINE_ void set_value(uint32_t p_bit, bool p_value) {
		if (p_value) {
			set(p_bit);
		} else {
			clear(p_bit);
		}
	}

	_FORCE_INLINE_ bool test(uint32_t p_bit) const {
		DEV_ASSERT(p_bit < BITS);
		return (words[p_bit >> 6] >> (p_bit & 63)) & 1;
	}

	_FORCE_INLINE_ void clear_all() {
		memset(words, 0, sizeof(words));
	}

	_FORCE_INLINE_ bool is_empty() const {
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			if (words[i]) {
				return false;
			}
		}
		return true;
	}

	// True when every bit of p_other is also set in this set.
	_FORCE_INLINE_ bool contains_all(const ECSBitSet &p_other) const {
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			if ((words[i] & p_other.words[i]) != p_other.words[i]) {
				return false;
			}
		}
		return true;
	}

	// True when at least one bit is shared with p_other.
	_FORCE_INLINE_ bool intersects(const ECSBitSet &p_other) const {
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			if (words[i] & p_other.words[i]) {
				return true;
			}
		}
		return false;
	}

	_FORCE_INLINE_ uint32_t count() const {
		uint32_t c = 0;
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
#if defined(__GNUC__) || defined(__clang__)
			c += __builtin_popcountll(words[i]);
#else
			uint64_t v = words[i];
			while (v) {
				v &= v - 1;
				c++;
			}
#endif
		}
		return c;
	}

	// Returns the index of the first set bit at or after p_from, or -1.
	_FORCE_INLINE_ int32_t find_next(uint32_t p_from) const {
		if (p_from >= BITS) {
			return -1;
		}
		uint32_t w = p_from >> 6;
		uint64_t v = words[w] & (~uint64_t(0) << (p_from & 63));
		while (true) {
			if (v) {
#if defined(__GNUC__) || defined(__clang__)
				return int32_t((w << 6) + __builtin_ctzll(v));
#else
				uint32_t b = 0;
				while (!((v >> b) & 1)) {
					b++;
				}
				return int32_t((w << 6) + b);
#endif
			}
			w++;
			if (w >= WORD_COUNT) {
				return -1;
			}
			v = words[w];
		}
	}

	_FORCE_INLINE_ int32_t find_first() const { return find_next(0); }

	// Calls p_fn(uint32_t bit) for every set bit, in ascending order.
	template <typename F>
	_FORCE_INLINE_ void for_each(F &&p_fn) const {
		for (int32_t b = find_first(); b >= 0; b = find_next(uint32_t(b) + 1)) {
			p_fn(uint32_t(b));
		}
	}

	_FORCE_INLINE_ ECSBitSet operator&(const ECSBitSet &p_o) const {
		ECSBitSet r;
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			r.words[i] = words[i] & p_o.words[i];
		}
		return r;
	}
	_FORCE_INLINE_ ECSBitSet operator|(const ECSBitSet &p_o) const {
		ECSBitSet r;
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			r.words[i] = words[i] | p_o.words[i];
		}
		return r;
	}
	_FORCE_INLINE_ ECSBitSet operator^(const ECSBitSet &p_o) const {
		ECSBitSet r;
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			r.words[i] = words[i] ^ p_o.words[i];
		}
		return r;
	}
	// Set difference: bits in this set that are not in p_o.
	_FORCE_INLINE_ ECSBitSet operator-(const ECSBitSet &p_o) const {
		ECSBitSet r;
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			r.words[i] = words[i] & ~p_o.words[i];
		}
		return r;
	}
	_FORCE_INLINE_ ECSBitSet operator~() const {
		ECSBitSet r;
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			r.words[i] = ~words[i];
		}
		return r;
	}
	_FORCE_INLINE_ ECSBitSet &operator&=(const ECSBitSet &p_o) {
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			words[i] &= p_o.words[i];
		}
		return *this;
	}
	_FORCE_INLINE_ ECSBitSet &operator|=(const ECSBitSet &p_o) {
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			words[i] |= p_o.words[i];
		}
		return *this;
	}
	_FORCE_INLINE_ ECSBitSet &operator-=(const ECSBitSet &p_o) {
		for (uint32_t i = 0; i < WORD_COUNT; i++) {
			words[i] &= ~p_o.words[i];
		}
		return *this;
	}

	_FORCE_INLINE_ bool operator==(const ECSBitSet &p_o) const {
		return memcmp(words, p_o.words, sizeof(words)) == 0;
	}
	_FORCE_INLINE_ bool operator!=(const ECSBitSet &p_o) const {
		return !(*this == p_o);
	}

	_FORCE_INLINE_ uint32_t hash() const {
		uint32_t h = hash_murmur3_one_64(words[0]);
		for (uint32_t i = 1; i < WORD_COUNT; i++) {
			h = hash_murmur3_one_64(words[i], h);
		}
		return hash_fmix32(h);
	}

	_FORCE_INLINE_ const uint64_t *get_words() const { return words; }
	_FORCE_INLINE_ uint64_t *get_words() { return words; }
};

template <uint32_t BITS>
struct ECSBitSetHasher {
	static _FORCE_INLINE_ uint32_t hash(const ECSBitSet<BITS> &p_set) {
		return p_set.hash();
	}
};
