/**************************************************************************/
/*  ecs_replicator.cpp                                                    */
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

#include "ecs_replicator.h"

#include "../components/ecs_builtin_components.h"
#include "../core/ecs_archetype.h"
#include "../core/ecs_query.h"
#include "../core/ecs_world.h"

#include "core/io/marshalls.h"

namespace ecs {

/* Signal glue: a tiny CallableCustom that forwards peer_connected /
 * peer_disconnected to the replicator (which is not an Object). */

class ReplicatorSignalCallable : public CallableCustom {
	Replicator *replicator;
	void (*func)(Replicator *, int);

	static bool _equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		const ReplicatorSignalCallable *a = static_cast<const ReplicatorSignalCallable *>(p_a);
		const ReplicatorSignalCallable *b = static_cast<const ReplicatorSignalCallable *>(p_b);
		return a->replicator == b->replicator && a->func == b->func;
	}
	static bool _less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const ReplicatorSignalCallable *a = static_cast<const ReplicatorSignalCallable *>(p_a);
		const ReplicatorSignalCallable *b = static_cast<const ReplicatorSignalCallable *>(p_b);
		if (a->replicator != b->replicator) {
			return a->replicator < b->replicator;
		}
		return (void *)a->func < (void *)b->func;
	}

public:
	virtual uint32_t hash() const override {
		return hash_murmur3_one_64((uint64_t)replicator, hash_murmur3_one_64((uint64_t)func));
	}
	virtual String get_as_text() const override { return "ECSReplicator::signal"; }
	virtual bool is_valid() const override { return true; }
	virtual CompareEqualFunc get_compare_equal_func() const override { return _equal; }
	virtual CompareLessFunc get_compare_less_func() const override { return _less; }
	virtual ObjectID get_object() const override { return ObjectID(); }
	virtual void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override {
		r_call_error.error = Callable::CallError::CALL_OK;
		if (p_argcount < 1) {
			r_call_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
			r_call_error.expected = 1;
			return;
		}
		func(replicator, int(*p_arguments[0]));
	}

	ReplicatorSignalCallable(Replicator *p_replicator, void (*p_func)(Replicator *, int)) :
			replicator(p_replicator), func(p_func) {}
};

static void _peer_connected_trampoline(Replicator *p_r, int p_peer);
static void _peer_disconnected_trampoline(Replicator *p_r, int p_peer);

/* Helpers */

static _FORCE_INLINE_ void _put_u8(LocalVector<uint8_t> &r_buf, uint8_t p_v) {
	r_buf.push_back(p_v);
}
static _FORCE_INLINE_ void _put_u16(LocalVector<uint8_t> &r_buf, uint16_t p_v) {
	uint32_t o = r_buf.size();
	r_buf.resize(o + 2);
	encode_uint16(p_v, r_buf.ptr() + o);
}
static _FORCE_INLINE_ void _put_u32(LocalVector<uint8_t> &r_buf, uint32_t p_v) {
	uint32_t o = r_buf.size();
	r_buf.resize(o + 4);
	encode_uint32(p_v, r_buf.ptr() + o);
}
static _FORCE_INLINE_ void _put_u64(LocalVector<uint8_t> &r_buf, uint64_t p_v) {
	uint32_t o = r_buf.size();
	r_buf.resize(o + 8);
	encode_uint64(p_v, r_buf.ptr() + o);
}
static _FORCE_INLINE_ void _put_f32(LocalVector<uint8_t> &r_buf, float p_v) {
	uint32_t o = r_buf.size();
	r_buf.resize(o + 4);
	encode_float(p_v, r_buf.ptr() + o);
}
static _FORCE_INLINE_ void _put_f64(LocalVector<uint8_t> &r_buf, double p_v) {
	uint32_t o = r_buf.size();
	r_buf.resize(o + 8);
	encode_double(p_v, r_buf.ptr() + o);
}
static _FORCE_INLINE_ void _put_bytes(LocalVector<uint8_t> &r_buf, const uint8_t *p_data, uint32_t p_size) {
	uint32_t o = r_buf.size();
	r_buf.resize(o + p_size);
	if (p_size) {
		memcpy(r_buf.ptr() + o, p_data, p_size);
	}
}
static _FORCE_INLINE_ void _put_string(LocalVector<uint8_t> &r_buf, const String &p_str) {
	CharString cs = p_str.utf8();
	_put_u32(r_buf, uint32_t(cs.length()));
	_put_bytes(r_buf, (const uint8_t *)cs.get_data(), uint32_t(cs.length()));
}

struct Reader {
	const uint8_t *data;
	uint32_t size;
	uint32_t pos = 0;
	bool ok = true;

	_FORCE_INLINE_ bool can_read(uint32_t p_n) {
		if (pos + p_n > size) {
			ok = false;
			return false;
		}
		return true;
	}
	uint8_t u8() {
		if (!can_read(1)) {
			return 0;
		}
		return data[pos++];
	}
	uint16_t u16() {
		if (!can_read(2)) {
			return 0;
		}
		uint16_t v = decode_uint16(data + pos);
		pos += 2;
		return v;
	}
	uint32_t u32() {
		if (!can_read(4)) {
			return 0;
		}
		uint32_t v = decode_uint32(data + pos);
		pos += 4;
		return v;
	}
	uint64_t u64() {
		if (!can_read(8)) {
			return 0;
		}
		uint64_t v = decode_uint64(data + pos);
		pos += 8;
		return v;
	}
	float f32() {
		if (!can_read(4)) {
			return 0;
		}
		float v = decode_float(data + pos);
		pos += 4;
		return v;
	}
	double f64() {
		if (!can_read(8)) {
			return 0;
		}
		double v = decode_double(data + pos);
		pos += 8;
		return v;
	}
	String string() {
		uint32_t len = u32();
		if (!can_read(len)) {
			return String();
		}
		String s = String::utf8((const char *)data + pos, int(len));
		pos += len;
		return s;
	}
	const uint8_t *bytes(uint32_t p_n) {
		if (!can_read(p_n)) {
			return nullptr;
		}
		const uint8_t *p = data + pos;
		pos += p_n;
		return p;
	}
};

// Number of real_t values stored in a trivial field of the given type, or 0.
static uint32_t _real_count(Variant::Type p_type) {
	switch (p_type) {
		case Variant::VECTOR2:
			return 2;
		case Variant::RECT2:
			return 4;
		case Variant::VECTOR3:
			return 3;
		case Variant::TRANSFORM2D:
			return 6;
		case Variant::VECTOR4:
			return 4;
		case Variant::PLANE:
			return 4;
		case Variant::QUATERNION:
			return 4;
		case Variant::AABB:
			return 6;
		case Variant::BASIS:
			return 9;
		case Variant::TRANSFORM3D:
			return 12;
		case Variant::PROJECTION:
			return 16;
		default:
			return 0;
	}
}

static uint32_t _int32_count(Variant::Type p_type) {
	switch (p_type) {
		case Variant::VECTOR2I:
			return 2;
		case Variant::RECT2I:
			return 4;
		case Variant::VECTOR3I:
			return 3;
		case Variant::VECTOR4I:
			return 4;
		default:
			return 0;
	}
}

static void _emit_peer_event(World *p_world, EventId p_id, int p_peer) {
	if (p_world->has_subscribers(p_id)) {
		Event ev(p_id);
		ev.peer = p_peer;
		p_world->emit_event(ev);
	}
}

static void _emit_entity_event(World *p_world, EventId p_id, Entity p_entity, uint32_t p_net_id, int p_peer = 0) {
	if (p_world->has_subscribers(p_id)) {
		Event ev = Event::for_entity(p_id, p_entity);
		ev.net_id = p_net_id;
		ev.peer = p_peer;
		p_world->emit_event(ev);
	}
}

/* Replicator */

Replicator::Replicator(World *p_world) :
		world(p_world) {
	identity_id = world->component_id<NetworkIdentity>();
	WorldObserver obs;
	obs.on_entity_destroyed = _on_entity_destroyed;
	obs.on_component_removed = _on_component_removed;
	obs.userdata = this;
	world->add_observer(obs);
	connected_callable = Callable(memnew(ReplicatorSignalCallable(this, _peer_connected_trampoline)));
	disconnected_callable = Callable(memnew(ReplicatorSignalCallable(this, _peer_disconnected_trampoline)));
}

Replicator::~Replicator() {
	set_peer(Ref<MultiplayerPeer>());
	world->remove_observer(this);
}

static void _peer_connected_trampoline(Replicator *p_r, int p_peer) {
	p_r->poll_peer_connected(p_peer);
}
static void _peer_disconnected_trampoline(Replicator *p_r, int p_peer) {
	p_r->poll_peer_disconnected(p_peer);
}

void Replicator::set_peer(const Ref<MultiplayerPeer> &p_peer) {
	if (peer == p_peer) {
		return;
	}
	if (peer.is_valid()) {
		if (peer->is_connected("peer_connected", connected_callable)) {
			peer->disconnect("peer_connected", connected_callable);
		}
		if (peer->is_connected("peer_disconnected", disconnected_callable)) {
			peer->disconnect("peer_disconnected", disconnected_callable);
		}
	}
	peer = p_peer;
	peers.clear();
	local_to_remote.clear();
	ticks_since_sync = 0;
	if (peer.is_valid()) {
		peer->connect("peer_connected", connected_callable);
		peer->connect("peer_disconnected", disconnected_callable);
		if (is_server()) {
			// Server ids are the wire ids.
			local_to_remote.clear();
		} else {
			// Client side: forget server entities of a previous session.
			for (uint32_t i = 0; i < net_entities.size(); i++) {
				Entity e = net_entities.value_at(i);
				if (world->is_alive(e)) {
					NetworkIdentity *n = world->get_mut<NetworkIdentity>(e);
					if (n) {
						n->net_id = 0;
					}
				}
			}
			net_entities.clear();
		}
	}
}

bool Replicator::is_server() const {
	return peer.is_valid() && peer->is_server();
}

bool Replicator::is_connected() const {
	return peer.is_valid() && peer->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED;
}

int Replicator::get_unique_id() const {
	return peer.is_valid() ? peer->get_unique_id() : 1;
}

void Replicator::get_peer_ids(LocalVector<int> &r_peers) const {
	for (const KeyValue<int, PeerInfo> &kv : peers) {
		r_peers.push_back(kv.key);
	}
}

void Replicator::add_listener(Listener *p_listener) {
	listeners.push_back(p_listener);
}

void Replicator::remove_listener(Listener *p_listener) {
	listeners.erase(p_listener);
}

/* Peer events */

void Replicator::poll_peer_connected(int p_peer) {
	_on_peer_connected(p_peer);
}

void Replicator::poll_peer_disconnected(int p_peer) {
	_on_peer_disconnected(p_peer);
}

void Replicator::_on_peer_connected(int p_peer) {
	PeerInfo &info = peers[p_peer];
	info.ready = false;
	info.known.clear();
	if (is_server()) {
		_send_registry(p_peer);
		info.ready = true;
		_send_snapshot(p_peer);
	}
	for (Listener *l : listeners) {
		l->on_peer_connected(p_peer);
	}
	_emit_peer_event(world, EVENT_PEER_CONNECTED, p_peer);
}

void Replicator::_on_peer_disconnected(int p_peer) {
	peers.erase(p_peer);
	if (is_server()) {
		// Entities owned by the peer go back to the server.
		for (uint32_t i = 0; i < net_entities.size(); i++) {
			Entity e = net_entities.value_at(i);
			NetworkIdentity *n = world->is_alive(e) ? world->get_mut<NetworkIdentity>(e) : nullptr;
			if (n && n->authority == p_peer) {
				n->authority = 1;
				pending_authority.push_back({ n->net_id, 1 });
			}
		}
	}
	for (Listener *l : listeners) {
		l->on_peer_disconnected(p_peer);
	}
	_emit_peer_event(world, EVENT_PEER_DISCONNECTED, p_peer);
}

/* Observers */

void Replicator::_on_entity_destroyed(World &p_world, Entity p_entity, void *p_userdata) {
	Replicator *r = (Replicator *)p_userdata;
	const NetworkIdentity *n = p_world.get<NetworkIdentity>(p_entity);
	if (!n || n->net_id == 0) {
		return;
	}
	r->net_entities.erase(n->net_id);
	if (r->is_server()) {
		r->pending_despawns.push_back(n->net_id);
	}
	for (Listener *l : r->listeners) {
		l->on_entity_despawned(p_entity, n->net_id);
	}
	_emit_entity_event(&p_world, EVENT_ENTITY_DESPAWNED, p_entity, n->net_id);
}

void Replicator::_on_component_removed(World &p_world, Entity p_entity, ComponentId p_component, void *p_userdata) {
	Replicator *r = (Replicator *)p_userdata;
	if (r->applying_remote) {
		return;
	}
	const NetworkIdentity *n = p_world.get<NetworkIdentity>(p_entity);
	if (!n || n->net_id == 0) {
		return;
	}
	if (p_component == r->identity_id) {
		// Losing the identity is a despawn for the other peers.
		r->net_entities.erase(n->net_id);
		if (r->is_server()) {
			r->pending_despawns.push_back(n->net_id);
		}
		for (Listener *l : r->listeners) {
			l->on_entity_despawned(p_entity, n->net_id);
		}
		_emit_entity_event(&p_world, EVENT_ENTITY_DESPAWNED, p_entity, n->net_id);
		return;
	}
	const ComponentType *t = p_world.get_component_type(p_component);
	if (!t || !t->is_replicated()) {
		return;
	}
	if (r->is_server() || (t->is_owner_authority() && n->authority == r->get_unique_id())) {
		r->pending_removes.push_back({ n->net_id, p_component });
	}
}

/* Identity helpers */

void Replicator::replicate(Entity p_entity, int32_t p_authority) {
	NetworkIdentity n;
	n.authority = p_authority;
	const NetworkIdentity *existing = world->get<NetworkIdentity>(p_entity);
	if (existing) {
		n.net_id = existing->net_id;
	}
	world->add<NetworkIdentity>(p_entity, n);
}

uint32_t Replicator::get_net_id(Entity p_entity) const {
	const NetworkIdentity *n = world->get<NetworkIdentity>(p_entity);
	return n ? n->net_id : 0;
}

Entity Replicator::get_entity(uint32_t p_net_id) const {
	const Entity *e = net_entities.getptr(p_net_id);
	return e ? *e : Entity();
}

int32_t Replicator::get_authority(Entity p_entity) const {
	const NetworkIdentity *n = world->get<NetworkIdentity>(p_entity);
	return n ? n->authority : 0;
}

bool Replicator::has_authority(Entity p_entity) const {
	const NetworkIdentity *n = world->get<NetworkIdentity>(p_entity);
	if (!n) {
		return !peer.is_valid() || is_server();
	}
	return n->authority == get_unique_id();
}

void Replicator::set_authority(Entity p_entity, int32_t p_peer) {
	NetworkIdentity *n = world->get_mut<NetworkIdentity>(p_entity);
	ERR_FAIL_NULL_MSG(n, "Entity is not replicated (no NetworkIdentity).");
	if (n->authority == p_peer) {
		return;
	}
	n->authority = p_peer;
	if (is_server() && n->net_id != 0) {
		pending_authority.push_back({ n->net_id, p_peer });
	}
	for (Listener *l : listeners) {
		l->on_authority_changed(p_entity, p_peer);
	}
	_emit_entity_event(world, EVENT_AUTHORITY_CHANGED, p_entity, n->net_id, p_peer);
}

/* Commands */

void Replicator::register_command(const StringName &p_name, const Callable &p_callable) {
	script_commands.insert(p_name, p_callable);
}

void Replicator::register_native_command(const StringName &p_name, NativeCommandFunc p_func, void *p_userdata) {
	NativeCommand c;
	c.func = p_func;
	c.userdata = p_userdata;
	native_commands.insert(p_name, c);
}

void Replicator::unregister_command(const StringName &p_name) {
	script_commands.erase(p_name);
	native_commands.erase(p_name);
}

void Replicator::send_command(int p_peer, const StringName &p_name, const Array &p_args, bool p_reliable) {
	ERR_FAIL_COND_MSG(!peer.is_valid(), "No multiplayer peer set.");
	buf.clear();
	_put_u8(buf, MSG_COMMAND);
	_put_string(buf, p_name);
	int len = 0;
	Error err = encode_variant(p_args, nullptr, len, false);
	ERR_FAIL_COND_MSG(err != OK, "Unable to encode command arguments.");
	_put_u32(buf, uint32_t(len));
	uint32_t o = buf.size();
	buf.resize(o + len);
	encode_variant(p_args, buf.ptr() + o, len, false);
	_send(p_peer, buf.ptr(), buf.size(), p_reliable ? MultiplayerPeer::TRANSFER_MODE_RELIABLE : MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED);
}

/* Wire id mapping */

ComponentId Replicator::_wire_to_local(int p_sender, uint32_t p_wire_id) const {
	if (is_server()) {
		return p_wire_id < world->get_component_type_count() ? ComponentId(p_wire_id) : INVALID_COMPONENT;
	}
	const PeerInfo *info = peers.getptr(p_sender);
	if (!info || p_wire_id >= info->remote_to_local.size()) {
		return INVALID_COMPONENT;
	}
	return info->remote_to_local[p_wire_id];
}

uint32_t Replicator::_local_to_wire(ComponentId p_local) const {
	if (is_server()) {
		return p_local;
	}
	if (p_local >= local_to_remote.size()) {
		return INVALID_COMPONENT;
	}
	return local_to_remote[p_local];
}

bool Replicator::_should_send(const ComponentType &p_type, int32_t p_authority, int p_to_peer) const {
	if (!p_type.is_replicated()) {
		return false;
	}
	if (p_type.is_owner_authority() && p_authority == p_to_peer) {
		return false; // Never echo owner data back to its owner.
	}
	return true;
}

/* Sending */

void Replicator::_send(int p_peer, const uint8_t *p_data, uint32_t p_size, MultiplayerPeer::TransferMode p_mode) {
	if (!peer.is_valid()) {
		return;
	}
	peer->set_transfer_channel(0);
	peer->set_transfer_mode(p_mode);
	peer->set_target_peer(p_peer);
	Error err = peer->put_packet(p_data, int(p_size));
	if (err == OK) {
		stats.packets_sent++;
		stats.bytes_sent += p_size;
	}
}

void Replicator::_send_registry(int p_peer) {
	buf.clear();
	_put_u8(buf, MSG_REGISTRY);
	uint32_t count = world->get_component_type_count();
	_put_u16(buf, uint16_t(count));
	for (uint32_t i = 0; i < count; i++) {
		const ComponentType *t = world->get_component_type(i);
		_put_u16(buf, uint16_t(i));
		_put_u32(buf, t->layout_hash());
		_put_string(buf, t->name);
	}
	_send(p_peer, buf.ptr(), buf.size(), MultiplayerPeer::TRANSFER_MODE_RELIABLE);
}

void Replicator::_send_spawn(int p_peer, Entity p_entity, uint32_t p_net_id) {
	const NetworkIdentity *n = world->get<NetworkIdentity>(p_entity);
	ERR_FAIL_NULL(n);
	LocalVector<uint8_t> packet;
	_put_u8(packet, MSG_SPAWN);
	_put_u32(packet, p_net_id);
	_put_u32(packet, uint32_t(n->authority));
	uint32_t count_pos = packet.size();
	_put_u8(packet, 0);
	uint32_t count = 0;
	ComponentMask mask = world->get_entity_mask(p_entity);
	LocalVector<uint8_t> payload;
	mask.for_each([&](uint32_t p_bit) {
		const ComponentType *t = world->get_component_type(p_bit);
		if (!t->is_replicated() || count >= 255) {
			return;
		}
		const void *c = world->get_component(p_entity, p_bit);
		payload.clear();
		_encode_component(*t, c, payload);
		_put_u16(packet, uint16_t(p_bit));
		_put_u16(packet, uint16_t(payload.size()));
		_put_bytes(packet, payload.ptr(), payload.size());
		count++;
	});
	packet[count_pos] = uint8_t(count);
	_send(p_peer, packet.ptr(), packet.size(), MultiplayerPeer::TRANSFER_MODE_RELIABLE);
	PeerInfo *info = peers.getptr(p_peer);
	if (info) {
		info->known.insert(p_net_id, 1);
	}
}

void Replicator::_send_snapshot(int p_peer) {
	for (uint32_t i = 0; i < net_entities.size(); i++) {
		uint32_t net_id = net_entities.key_at(i);
		Entity e = net_entities.value_at(i);
		if (world->is_alive(e)) {
			_send_spawn(p_peer, e, net_id);
		}
	}
}

void Replicator::_flush_update_packet(int p_to_peer, LocalVector<uint8_t> &p_packet, uint32_t &p_count, MultiplayerPeer::TransferMode p_mode, bool p_force) {
	if (p_count == 0) {
		return;
	}
	bool over = p_mode != MultiplayerPeer::TRANSFER_MODE_RELIABLE && p_packet.size() >= max_unreliable_packet_size;
	if (!p_force && !over) {
		return;
	}
	encode_uint16(uint16_t(p_count), p_packet.ptr() + 1);
	_send(p_to_peer, p_packet.ptr(), p_packet.size(), p_mode);
	stats.last_sync_updates += p_count;
	p_packet.resize(3);
	p_count = 0;
}

// Appends UPDATE records for every replicated component changed since the
// last sync. p_from_owner: only owner-authority components of entities owned
// by this peer (client mode). Otherwise: everything the server may send to
// p_to_peer.
void Replicator::_collect_updates(int p_to_peer, bool p_from_owner, LocalVector<uint8_t> &r_reliable, LocalVector<uint8_t> &r_unreliable, uint32_t &r_reliable_count, uint32_t &r_unreliable_count, int p_exclude_peer) {
	const int self_id = get_unique_id();
	const PeerInfo *info = peers.getptr(p_to_peer);
	LocalVector<uint8_t> payload;

	auto emit = [&](uint32_t p_net_id, const ComponentType &p_type, const void *p_component) {
		uint32_t wire = _local_to_wire(p_type.id);
		if (wire == INVALID_COMPONENT) {
			return;
		}
		payload.clear();
		_encode_component(p_type, p_component, payload);
		bool unreliable = p_type.flags & COMPONENT_FLAG_UNRELIABLE;
		LocalVector<uint8_t> &packet = unreliable ? r_unreliable : r_reliable;
		uint32_t &count = unreliable ? r_unreliable_count : r_reliable_count;
		_put_u32(packet, p_net_id);
		_put_u16(packet, uint16_t(wire));
		_put_u16(packet, uint16_t(payload.size()));
		_put_bytes(packet, payload.ptr(), payload.size());
		count++;
		_flush_update_packet(p_to_peer, packet, count, unreliable ? MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED : MultiplayerPeer::TRANSFER_MODE_RELIABLE, false);
	};

	// Table components: walk archetypes that contain NetworkIdentity.
	const LocalVector<Archetype *> &archetypes = world->get_archetypes();
	for (Archetype *a : archetypes) {
		int32_t id_col = a->get_column_index(identity_id);
		if (id_col < 0 || a->size() == 0) {
			continue;
		}
		const NetworkIdentity *ids = a->column_ptr<NetworkIdentity>(uint32_t(id_col));
		uint32_t rows = a->size();
		for (uint32_t c = 0; c < a->get_column_count(); c++) {
			const Archetype::Column &col = a->get_column(c);
			const ComponentType &t = *col.type;
			if (!t.is_replicated()) {
				continue;
			}
			if (p_from_owner && !t.is_owner_authority()) {
				continue;
			}
			for (uint32_t row = 0; row < rows; row++) {
				if (col.change_ticks[row] <= last_sync_tick) {
					continue;
				}
				const NetworkIdentity &n = ids[row];
				if (n.net_id == 0) {
					continue;
				}
				if (p_from_owner) {
					if (n.authority != self_id) {
						continue;
					}
				} else {
					if (!_should_send(t, n.authority, p_to_peer)) {
						continue;
					}
					if (info && !info->known.has(n.net_id)) {
						continue; // Peer will get a full spawn instead.
					}
				}
				emit(n.net_id, t, col.data + size_t(row) * col.stride);
			}
		}
	}

	// Sparse components.
	const ComponentMask &sparse = world->get_sparse_mask();
	sparse.for_each([&](uint32_t p_bit) {
		const ComponentType *t = world->get_component_type(p_bit);
		if (!t->is_replicated() || (p_from_owner && !t->is_owner_authority())) {
			return;
		}
		SparseComponentStorage *s = world->get_sparse_storage(p_bit);
		for (uint32_t i = 0; i < s->size(); i++) {
			uint32_t idx = s->entity_index_at(i);
			if (s->get_change_tick(idx) <= last_sync_tick) {
				continue;
			}
			Entity e = world->get_entity_at_index(idx);
			const NetworkIdentity *n = world->get<NetworkIdentity>(e);
			if (!n || n->net_id == 0) {
				continue;
			}
			if (p_from_owner) {
				if (n->authority != self_id) {
					continue;
				}
			} else {
				if (!_should_send(*t, n->authority, p_to_peer)) {
					continue;
				}
				if (info && !info->known.has(n->net_id)) {
					continue;
				}
			}
			emit(n->net_id, *t, s->value_at(i));
		}
	});
}

void Replicator::_sync_server() {
	// Assign ids to newly replicated entities.
	const LocalVector<Archetype *> &archetypes = world->get_archetypes();
	for (Archetype *a : archetypes) {
		int32_t id_col = a->get_column_index(identity_id);
		if (id_col < 0) {
			continue;
		}
		NetworkIdentity *ids = a->column_ptr<NetworkIdentity>(uint32_t(id_col));
		uint32_t rows = a->size();
		for (uint32_t row = 0; row < rows; row++) {
			if (ids[row].net_id == 0) {
				ids[row].net_id = next_net_id++;
				Entity e = a->get_entity(row);
				net_entities.insert(ids[row].net_id, e);
				new_spawns.push_back(e);
				for (Listener *l : listeners) {
					l->on_entity_spawned(e, ids[row].net_id);
				}
				_emit_entity_event(world, EVENT_ENTITY_SPAWNED, e, ids[row].net_id);
			}
		}
	}

	LocalVector<uint8_t> reliable;
	LocalVector<uint8_t> unreliable;
	for (KeyValue<int, PeerInfo> &kv : peers) {
		int to = kv.key;
		if (!kv.value.ready) {
			continue;
		}
		// Despawns.
		for (uint32_t net_id : pending_despawns) {
			if (kv.value.known.has(net_id)) {
				buf.clear();
				_put_u8(buf, MSG_DESPAWN);
				_put_u32(buf, net_id);
				_send(to, buf.ptr(), buf.size(), MultiplayerPeer::TRANSFER_MODE_RELIABLE);
				kv.value.known.erase(net_id);
			}
		}
		// Component removals.
		for (const PendingRemove &pr : pending_removes) {
			if (kv.value.known.has(pr.net_id)) {
				buf.clear();
				_put_u8(buf, MSG_REMOVE_COMPONENT);
				_put_u32(buf, pr.net_id);
				_put_u16(buf, uint16_t(pr.component));
				_send(to, buf.ptr(), buf.size(), MultiplayerPeer::TRANSFER_MODE_RELIABLE);
			}
		}
		// Authority changes.
		for (const PendingAuthority &pa : pending_authority) {
			if (kv.value.known.has(pa.net_id)) {
				buf.clear();
				_put_u8(buf, MSG_AUTHORITY);
				_put_u32(buf, pa.net_id);
				_put_u32(buf, uint32_t(pa.authority));
				_send(to, buf.ptr(), buf.size(), MultiplayerPeer::TRANSFER_MODE_RELIABLE);
			}
		}
		// Updates for entities the peer already knows.
		reliable.clear();
		unreliable.clear();
		reliable.resize(3);
		unreliable.resize(3);
		reliable[0] = MSG_UPDATE;
		unreliable[0] = MSG_UPDATE;
		uint32_t rc = 0;
		uint32_t uc = 0;
		_collect_updates(to, false, reliable, unreliable, rc, uc, to);
		_flush_update_packet(to, reliable, rc, MultiplayerPeer::TRANSFER_MODE_RELIABLE, true);
		_flush_update_packet(to, unreliable, uc, MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED, true);
		// Spawns of new entities (full state).
		for (Entity e : new_spawns) {
			if (world->is_alive(e)) {
				_send_spawn(to, e, get_net_id(e));
			}
		}
	}
	pending_despawns.clear();
	pending_removes.clear();
	pending_authority.clear();
	new_spawns.clear();
}

void Replicator::_sync_client() {
	PeerInfo *server = peers.getptr(1);
	if (!server || !server->ready) {
		pending_removes.clear();
		return;
	}
	for (const PendingRemove &pr : pending_removes) {
		uint32_t wire = _local_to_wire(pr.component);
		if (wire == INVALID_COMPONENT) {
			continue;
		}
		buf.clear();
		_put_u8(buf, MSG_REMOVE_COMPONENT);
		_put_u32(buf, pr.net_id);
		_put_u16(buf, uint16_t(wire));
		_send(1, buf.ptr(), buf.size(), MultiplayerPeer::TRANSFER_MODE_RELIABLE);
	}
	pending_removes.clear();

	LocalVector<uint8_t> reliable;
	LocalVector<uint8_t> unreliable;
	reliable.resize(3);
	unreliable.resize(3);
	reliable[0] = MSG_UPDATE;
	unreliable[0] = MSG_UPDATE;
	uint32_t rc = 0;
	uint32_t uc = 0;
	_collect_updates(1, true, reliable, unreliable, rc, uc, -1);
	_flush_update_packet(1, reliable, rc, MultiplayerPeer::TRANSFER_MODE_RELIABLE, true);
	_flush_update_packet(1, unreliable, uc, MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED, true);
}

void Replicator::sync() {
	if (!peer.is_valid()) {
		pending_despawns.clear();
		pending_removes.clear();
		pending_authority.clear();
		new_spawns.clear();
		return;
	}
	ticks_since_sync++;
	if (ticks_since_sync < sync_interval) {
		return;
	}
	ticks_since_sync = 0;
	stats.last_sync_updates = 0;
	if (peer->get_connection_status() != MultiplayerPeer::CONNECTION_CONNECTED) {
		return;
	}
	if (is_server()) {
		_sync_server();
	} else {
		_sync_client();
	}
	last_sync_tick = world->get_tick();
}

/* Receiving */

void Replicator::poll() {
	if (!peer.is_valid()) {
		return;
	}
	peer->poll();
	if (peer->get_connection_status() == MultiplayerPeer::CONNECTION_DISCONNECTED) {
		return;
	}
	while (peer->get_available_packet_count() > 0) {
		// The sender must be queried before the packet is popped.
		int sender = peer->get_packet_peer();
		const uint8_t *data = nullptr;
		int size = 0;
		Error err = peer->get_packet(&data, size);
		if (err != OK) {
			break;
		}
		stats.packets_received++;
		stats.bytes_received += uint64_t(size);
		_handle_packet(sender, data, size);
	}
}

void Replicator::_handle_packet(int p_sender, const uint8_t *p_data, int p_size) {
	if (p_size < 1) {
		return;
	}
	switch (p_data[0]) {
		case MSG_REGISTRY:
			_handle_registry(p_sender, p_data + 1, p_size - 1);
			break;
		case MSG_SPAWN:
			_handle_spawn(p_sender, p_data + 1, p_size - 1);
			break;
		case MSG_DESPAWN:
			_handle_despawn(p_sender, p_data + 1, p_size - 1);
			break;
		case MSG_UPDATE:
			_handle_update(p_sender, p_data + 1, p_size - 1);
			break;
		case MSG_REMOVE_COMPONENT:
			_handle_remove(p_sender, p_data + 1, p_size - 1);
			break;
		case MSG_AUTHORITY:
			_handle_authority(p_sender, p_data + 1, p_size - 1);
			break;
		case MSG_COMMAND:
			_handle_command(p_sender, p_data + 1, p_size - 1);
			break;
		default:
			WARN_PRINT(vformat("ECS replicator: unknown message type %d from peer %d.", p_data[0], p_sender));
			break;
	}
}

void Replicator::_handle_registry(int p_sender, const uint8_t *p_data, int p_size) {
	if (is_server() || p_sender != 1) {
		return; // Only the server defines the wire registry.
	}
	Reader r{ p_data, uint32_t(p_size) };
	uint32_t count = r.u16();
	PeerInfo &info = peers[p_sender];
	info.remote_to_local.clear();
	info.remote_to_local.resize(count);
	local_to_remote.clear();
	local_to_remote.resize(world->get_component_type_count());
	for (uint32_t i = 0; i < local_to_remote.size(); i++) {
		local_to_remote[i] = INVALID_COMPONENT;
	}
	for (uint32_t i = 0; i < count && r.ok; i++) {
		uint32_t wire = r.u16();
		uint32_t hash = r.u32();
		String name = r.string();
		if (!r.ok || wire >= count) {
			break;
		}
		info.remote_to_local[wire] = INVALID_COMPONENT;
		ComponentId local = world->find_component(name);
		if (local == INVALID_COMPONENT) {
			continue;
		}
		const ComponentType *t = world->get_component_type(local);
		if (t->layout_hash() != hash) {
			WARN_PRINT(vformat("ECS replicator: component '%s' has a different layout on the server, it will not be replicated.", name));
			continue;
		}
		info.remote_to_local[wire] = local;
		local_to_remote[local] = wire;
	}
	info.ready = true;
}

void Replicator::_handle_spawn(int p_sender, const uint8_t *p_data, int p_size) {
	if (is_server() || p_sender != 1) {
		return;
	}
	Reader r{ p_data, uint32_t(p_size) };
	uint32_t net_id = r.u32();
	int32_t authority = int32_t(r.u32());
	uint32_t count = r.u8();
	if (!r.ok || net_id == 0) {
		return;
	}
	Entity e;
	const Entity *existing = net_entities.getptr(net_id);
	if (existing && world->is_alive(*existing)) {
		e = *existing;
	} else {
		e = world->create_entity();
		net_entities.insert(net_id, e);
	}
	applying_remote = true;
	NetworkIdentity n;
	n.net_id = net_id;
	n.authority = authority;
	world->add<NetworkIdentity>(e, n);
	for (uint32_t i = 0; i < count && r.ok; i++) {
		uint32_t wire = r.u16();
		uint32_t len = r.u16();
		const uint8_t *payload = r.bytes(len);
		if (!r.ok) {
			break;
		}
		ComponentId local = _wire_to_local(p_sender, wire);
		if (local == INVALID_COMPONENT) {
			continue;
		}
		const ComponentType *t = world->get_component_type(local);
		void *c = world->add_component(e, local, nullptr);
		if (c || t->is_tag()) {
			_decode_component(*t, c, payload, len);
		}
	}
	applying_remote = false;
	for (Listener *l : listeners) {
		l->on_entity_spawned(e, net_id);
	}
	_emit_entity_event(world, EVENT_ENTITY_SPAWNED, e, net_id, authority);
}

void Replicator::_handle_despawn(int p_sender, const uint8_t *p_data, int p_size) {
	if (is_server() || p_sender != 1) {
		return;
	}
	Reader r{ p_data, uint32_t(p_size) };
	uint32_t net_id = r.u32();
	const Entity *e = net_entities.getptr(net_id);
	if (!e) {
		return;
	}
	Entity entity = *e;
	net_entities.erase(net_id);
	if (world->is_alive(entity)) {
		// Clear the id first so the destroy observer doesn't report it again.
		NetworkIdentity *n = world->get_mut<NetworkIdentity>(entity);
		if (n) {
			n->net_id = 0;
		}
		for (Listener *l : listeners) {
			l->on_entity_despawned(entity, net_id);
		}
		_emit_entity_event(world, EVENT_ENTITY_DESPAWNED, entity, net_id);
		applying_remote = true;
		world->destroy_entity(entity);
		applying_remote = false;
	}
}

void Replicator::_handle_update(int p_sender, const uint8_t *p_data, int p_size) {
	Reader r{ p_data, uint32_t(p_size) };
	uint32_t count = r.u16();
	const bool server = is_server();
	if (!server && p_sender != 1) {
		return;
	}
	const int self_id = get_unique_id();
	applying_remote = true;
	for (uint32_t i = 0; i < count && r.ok; i++) {
		uint32_t net_id = r.u32();
		uint32_t wire = r.u16();
		uint32_t len = r.u16();
		const uint8_t *payload = r.bytes(len);
		if (!r.ok) {
			break;
		}
		const Entity *ep = net_entities.getptr(net_id);
		if (!ep || !world->is_alive(*ep)) {
			continue;
		}
		Entity e = *ep;
		ComponentId local = _wire_to_local(p_sender, wire);
		if (local == INVALID_COMPONENT) {
			continue;
		}
		const ComponentType *t = world->get_component_type(local);
		if (!t->is_replicated()) {
			continue;
		}
		const NetworkIdentity *n = world->get<NetworkIdentity>(e);
		if (!n) {
			continue;
		}
		if (server) {
			// Clients may only push owner-authority components of entities they own.
			if (!t->is_owner_authority() || n->authority != p_sender) {
				continue;
			}
		} else {
			// Ignore echoes for components we are authoritative over.
			if (t->is_owner_authority() && n->authority == self_id) {
				continue;
			}
		}
		void *c = world->add_component(e, local, nullptr); // Returns existing or newly added.
		if (c || t->is_tag()) {
			_decode_component(*t, c, payload, len);
			world->mark_changed(e, local);
		}
	}
	applying_remote = false;
}

void Replicator::_handle_remove(int p_sender, const uint8_t *p_data, int p_size) {
	Reader r{ p_data, uint32_t(p_size) };
	uint32_t net_id = r.u32();
	uint32_t wire = r.u16();
	if (!r.ok) {
		return;
	}
	const bool server = is_server();
	if (!server && p_sender != 1) {
		return;
	}
	const Entity *ep = net_entities.getptr(net_id);
	if (!ep || !world->is_alive(*ep)) {
		return;
	}
	ComponentId local = _wire_to_local(p_sender, wire);
	if (local == INVALID_COMPONENT || local == identity_id) {
		return;
	}
	const ComponentType *t = world->get_component_type(local);
	if (!t->is_replicated()) {
		return;
	}
	if (server) {
		const NetworkIdentity *n = world->get<NetworkIdentity>(*ep);
		if (!n || !t->is_owner_authority() || n->authority != p_sender) {
			return;
		}
		// Relay.
		pending_removes.push_back({ net_id, local });
	}
	applying_remote = true;
	world->remove_component(*ep, local);
	applying_remote = false;
}

void Replicator::_handle_authority(int p_sender, const uint8_t *p_data, int p_size) {
	if (is_server() || p_sender != 1) {
		return;
	}
	Reader r{ p_data, uint32_t(p_size) };
	uint32_t net_id = r.u32();
	int32_t authority = int32_t(r.u32());
	if (!r.ok) {
		return;
	}
	const Entity *ep = net_entities.getptr(net_id);
	if (!ep || !world->is_alive(*ep)) {
		return;
	}
	NetworkIdentity *n = world->get_mut<NetworkIdentity>(*ep);
	if (n) {
		n->authority = authority;
	}
	for (Listener *l : listeners) {
		l->on_authority_changed(*ep, authority);
	}
	_emit_entity_event(world, EVENT_AUTHORITY_CHANGED, *ep, net_id, authority);
}

void Replicator::_handle_command(int p_sender, const uint8_t *p_data, int p_size) {
	Reader r{ p_data, uint32_t(p_size) };
	String name = r.string();
	uint32_t len = r.u32();
	const uint8_t *payload = r.bytes(len);
	if (!r.ok) {
		return;
	}
	Variant args_v;
	if (decode_variant(args_v, payload, int(len), nullptr, false) != OK) {
		return;
	}
	Array args = args_v;
	StringName sname = name;
	const NativeCommand *nc = native_commands.getptr(sname);
	if (nc && nc->func) {
		nc->func(*world, p_sender, args, nc->userdata);
		return;
	}
	const Callable *cb = script_commands.getptr(sname);
	if (cb && cb->is_valid()) {
		Variant sender = p_sender;
		Variant vargs = args;
		const Variant *argp[2] = { &sender, &vargs };
		Variant ret;
		Callable::CallError ce;
		cb->callp(argp, 2, ret, ce);
		if (ce.error != Callable::CallError::CALL_OK) {
			ERR_PRINT(vformat("ECS command '%s' handler failed: %s", name, Variant::get_callable_error_text(*cb, argp, 2, ce)));
		}
		return;
	}
	for (Listener *l : listeners) {
		if (l->on_command(p_sender, sname, args)) {
			return;
		}
	}
	WARN_PRINT(vformat("ECS replicator: no handler for command '%s'.", name));
}

/* Component serialization */

void Replicator::_encode_field(const ComponentField &p_field, const uint8_t *p_src, LocalVector<uint8_t> &r_buf) const {
	switch (p_field.kind) {
		case FIELD_KIND_ENTITY: {
			Entity e = *(const Entity *)p_src;
			_put_u32(r_buf, world->is_alive(e) ? get_net_id(e) : 0);
			return;
		}
		case FIELD_KIND_STRING:
			_put_string(r_buf, *(const String *)p_src);
			return;
		case FIELD_KIND_STRING_NAME:
			_put_string(r_buf, *(const StringName *)p_src);
			return;
		case FIELD_KIND_VARIANT: {
			int len = 0;
			const Variant &v = *(const Variant *)p_src;
			if (encode_variant(v, nullptr, len, false) != OK) {
				_put_u32(r_buf, 0);
				return;
			}
			_put_u32(r_buf, uint32_t(len));
			uint32_t o = r_buf.size();
			r_buf.resize(o + len);
			encode_variant(v, r_buf.ptr() + o, len, false);
			return;
		}
		case FIELD_KIND_TRIVIAL:
			break;
	}
	switch (p_field.type) {
		case Variant::BOOL:
			_put_u8(r_buf, *(const bool *)p_src ? 1 : 0);
			return;
		case Variant::INT: {
			int64_t v = 0;
			switch (p_field.size) {
				case 1:
					v = *(const int8_t *)p_src;
					break;
				case 2:
					v = *(const int16_t *)p_src;
					break;
				case 4:
					v = *(const int32_t *)p_src;
					break;
				default:
					v = *(const int64_t *)p_src;
					break;
			}
			_put_u64(r_buf, uint64_t(v));
			return;
		}
		case Variant::FLOAT:
			if (p_field.size == 4) {
				_put_f32(r_buf, *(const float *)p_src);
			} else {
				_put_f64(r_buf, *(const double *)p_src);
			}
			return;
		case Variant::COLOR: {
			const float *f = (const float *)p_src;
			for (int i = 0; i < 4; i++) {
				_put_f32(r_buf, f[i]);
			}
			return;
		}
		case Variant::RID:
			return; // Never meaningful across the network.
		default:
			break;
	}
	uint32_t reals = _real_count(p_field.type);
	if (reals) {
		const real_t *f = (const real_t *)p_src;
		for (uint32_t i = 0; i < reals; i++) {
#ifdef REAL_T_IS_DOUBLE
			_put_f64(r_buf, f[i]);
#else
			_put_f32(r_buf, f[i]);
#endif
		}
		return;
	}
	uint32_t ints = _int32_count(p_field.type);
	if (ints) {
		const int32_t *v = (const int32_t *)p_src;
		for (uint32_t i = 0; i < ints; i++) {
			_put_u32(r_buf, uint32_t(v[i]));
		}
		return;
	}
	// Unknown trivial type: raw bytes.
	_put_bytes(r_buf, p_src, p_field.size);
}

uint32_t Replicator::_decode_field(const ComponentField &p_field, uint8_t *p_dst, const uint8_t *p_data, uint32_t p_size) const {
	Reader r{ p_data, p_size };
	switch (p_field.kind) {
		case FIELD_KIND_ENTITY: {
			uint32_t net_id = r.u32();
			*(Entity *)p_dst = net_id ? get_entity(net_id) : Entity();
			return r.ok ? r.pos : 0;
		}
		case FIELD_KIND_STRING:
			*(String *)p_dst = r.string();
			return r.ok ? r.pos : 0;
		case FIELD_KIND_STRING_NAME:
			*(StringName *)p_dst = StringName(r.string());
			return r.ok ? r.pos : 0;
		case FIELD_KIND_VARIANT: {
			uint32_t len = r.u32();
			const uint8_t *payload = r.bytes(len);
			if (!r.ok) {
				return 0;
			}
			Variant v;
			if (len && decode_variant(v, payload, int(len), nullptr, false) == OK) {
				*(Variant *)p_dst = v;
			}
			return r.pos;
		}
		case FIELD_KIND_TRIVIAL:
			break;
	}
	switch (p_field.type) {
		case Variant::BOOL:
			*(bool *)p_dst = r.u8() != 0;
			return r.ok ? r.pos : 0;
		case Variant::INT: {
			int64_t v = int64_t(r.u64());
			if (!r.ok) {
				return 0;
			}
			switch (p_field.size) {
				case 1:
					*(int8_t *)p_dst = int8_t(v);
					break;
				case 2:
					*(int16_t *)p_dst = int16_t(v);
					break;
				case 4:
					*(int32_t *)p_dst = int32_t(v);
					break;
				default:
					*(int64_t *)p_dst = v;
					break;
			}
			return r.pos;
		}
		case Variant::FLOAT:
			if (p_field.size == 4) {
				*(float *)p_dst = r.f32();
			} else {
				*(double *)p_dst = r.f64();
			}
			return r.ok ? r.pos : 0;
		case Variant::COLOR: {
			float *f = (float *)p_dst;
			for (int i = 0; i < 4; i++) {
				f[i] = r.f32();
			}
			return r.ok ? r.pos : 0;
		}
		case Variant::RID:
			return 0;
		default:
			break;
	}
	uint32_t reals = _real_count(p_field.type);
	if (reals) {
		real_t *f = (real_t *)p_dst;
		for (uint32_t i = 0; i < reals; i++) {
#ifdef REAL_T_IS_DOUBLE
			f[i] = r.f64();
#else
			f[i] = r.f32();
#endif
		}
		return r.ok ? r.pos : 0;
	}
	uint32_t ints = _int32_count(p_field.type);
	if (ints) {
		int32_t *v = (int32_t *)p_dst;
		for (uint32_t i = 0; i < ints; i++) {
			v[i] = int32_t(r.u32());
		}
		return r.ok ? r.pos : 0;
	}
	const uint8_t *raw = r.bytes(p_field.size);
	if (!raw) {
		return 0;
	}
	memcpy(p_dst, raw, p_field.size);
	return r.pos;
}

void Replicator::_encode_component(const ComponentType &p_type, const void *p_component, LocalVector<uint8_t> &r_buf) const {
	if (p_type.size == 0 || p_component == nullptr) {
		return;
	}
	if (p_type.fields.is_empty()) {
		// Opaque native type without reflection: raw copy.
		_put_bytes(r_buf, (const uint8_t *)p_component, p_type.size);
		return;
	}
	for (const ComponentField &f : p_type.fields) {
		_encode_field(f, (const uint8_t *)p_component + f.offset, r_buf);
	}
}

bool Replicator::_decode_component(const ComponentType &p_type, void *p_component, const uint8_t *p_data, uint32_t p_size) const {
	if (p_type.size == 0 || p_component == nullptr) {
		return true;
	}
	if (p_type.fields.is_empty()) {
		if (p_size < p_type.size) {
			return false;
		}
		memcpy(p_component, p_data, p_type.size);
		return true;
	}
	uint32_t pos = 0;
	for (const ComponentField &f : p_type.fields) {
		if (f.kind == FIELD_KIND_TRIVIAL && f.type == Variant::RID) {
			continue;
		}
		uint32_t used = _decode_field(f, (uint8_t *)p_component + f.offset, p_data + pos, p_size - pos);
		if (used == 0) {
			return false;
		}
		pos += used;
	}
	return true;
}

} // namespace ecs
