/**************************************************************************/
/*  test_ecs_replication.h                                                */
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

#include "../components/ecs_builtin_components.h"
#include "../core/ecs_query.h"
#include "../core/ecs_world.h"
#include "../net/ecs_replicator.h"

#include "core/object/class_db.h"
#include "core/templates/list.h"
#include "scene/main/multiplayer_peer.h"

#include "tests/test_macros.h"

namespace TestECSReplication {

using namespace ecs;

// In-memory MultiplayerPeer pair (server id 1 <-> client id 2) used to test
// the replicator without sockets.
class LoopbackPeer : public MultiplayerPeer {
	GDCLASS(LoopbackPeer, MultiplayerPeer);

	struct Packet {
		int from = 0;
		Vector<uint8_t> data;
		TransferMode mode = TRANSFER_MODE_RELIABLE;
	};

	int id = 1;
	LoopbackPeer *other = nullptr;
	List<Packet> incoming;
	Packet current;
	int target = 0;
	bool connected = false;

protected:
	static void _bind_methods() {}

public:
	int sent = 0;

	void setup(int p_id) {
		id = p_id;
		connected = id == 1; // A server is "connected" as soon as it is created, like ENet.
	}
	static void link(LoopbackPeer *p_server, LoopbackPeer *p_client) {
		p_server->other = p_client;
		p_client->other = p_server;
		p_server->connected = true;
		p_client->connected = true;
		p_server->emit_signal(SNAME("peer_connected"), p_client->id);
		p_client->emit_signal(SNAME("peer_connected"), p_server->id);
	}
	void drop_incoming() { incoming.clear(); }

	virtual int get_available_packet_count() const override { return incoming.size(); }
	virtual Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override {
		ERR_FAIL_COND_V(incoming.is_empty(), ERR_UNAVAILABLE);
		current = incoming.front()->get();
		incoming.pop_front();
		*r_buffer = current.data.ptr();
		r_buffer_size = current.data.size();
		return OK;
	}
	virtual Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override {
		ERR_FAIL_NULL_V(other, ERR_UNCONFIGURED);
		if (target != 0 && target != other->id && !(target < 0 && -target != other->id)) {
			return OK; // Not addressed to the other end.
		}
		Packet p;
		p.from = id;
		p.mode = get_transfer_mode();
		p.data.resize(p_buffer_size);
		memcpy(p.data.ptrw(), p_buffer, p_buffer_size);
		other->incoming.push_back(p);
		sent++;
		return OK;
	}
	virtual int get_max_packet_size() const override { return 1 << 24; }

	virtual void set_target_peer(int p_peer_id) override { target = p_peer_id; }
	// Like ENetMultiplayerPeer: describes the *next* packet, before get_packet() pops it.
	virtual int get_packet_peer() const override { return incoming.is_empty() ? 0 : incoming.front()->get().from; }
	virtual TransferMode get_packet_mode() const override { return incoming.is_empty() ? TRANSFER_MODE_RELIABLE : incoming.front()->get().mode; }
	virtual int get_packet_channel() const override { return 0; }
	virtual void disconnect_peer(int p_peer, bool p_force = false) override {}
	virtual bool is_server() const override { return id == 1; }
	virtual void poll() override {}
	virtual void close() override { connected = false; }
	virtual int get_unique_id() const override { return id; }
	virtual ConnectionStatus get_connection_status() const override { return connected ? CONNECTION_CONNECTED : CONNECTION_DISCONNECTED; }
};

static void _ensure_peer_class() {
	if (!ClassDB::class_exists("LoopbackPeer")) {
		GDREGISTER_CLASS(LoopbackPeer);
	}
}

struct Health {
	int32_t value = 100;
	float regen = 0.5f;
};

struct PlayerInput {
	Vector2 move;
	bool jump = false;
};

struct Pos {
	float x = 0;
	float y = 0;
};

struct Marker {};

static void _register(World &w) {
	w.register_component<Health>("Health", COMPONENT_FLAG_REPLICATED);
	ComponentType *t = w.get_component_type_mut(w.component_id<Health>());
	t->add_field("value", Variant::INT, offsetof(Health, value), sizeof(int32_t));
	t->add_field("regen", Variant::FLOAT, offsetof(Health, regen), sizeof(float));

	w.register_component<PlayerInput>("PlayerInput", COMPONENT_FLAG_OWNER_AUTHORITY | COMPONENT_FLAG_UNRELIABLE);
	t = w.get_component_type_mut(w.component_id<PlayerInput>());
	t->add_field("move", Variant::VECTOR2, offsetof(PlayerInput, move), sizeof(Vector2));
	t->add_field("jump", Variant::BOOL, offsetof(PlayerInput, jump), sizeof(bool));

	// Opaque native type (no fields): replicated as raw bytes.
	w.register_component<Pos>("Pos", COMPONENT_FLAG_REPLICATED);
	w.register_component<Marker>("Marker", COMPONENT_FLAG_REPLICATED);

	Dictionary fields;
	fields["name"] = Variant::STRING;
	fields["score"] = Variant::INT;
	fields["target"] = "entity";
	w.register_script_component("Player", fields, COMPONENT_FLAG_REPLICATED);
}

struct Pair {
	World server;
	World client;
	Ref<LoopbackPeer> sp;
	Ref<LoopbackPeer> cp;

	Pair() {
		_ensure_peer_class();
		_register(server);
		_register(client);
		sp.instantiate();
		cp.instantiate();
		sp->setup(1);
		cp->setup(2);
		server.get_replicator()->set_peer(sp);
		client.get_replicator()->set_peer(cp);
		LoopbackPeer::link(sp.ptr(), cp.ptr());
		// Let the client process the registry.
		client.physics_update(0.016);
	}

	// One full round trip: server tick (sends), client tick (receives & sends), server tick (receives).
	void step() {
		server.physics_update(0.016);
		client.physics_update(0.016);
		server.physics_update(0.016);
	}
};

TEST_CASE("[ECS][Replication] Registry handshake and spawn / update / remove / despawn") {
	Pair p;
	Replicator *sr = p.server.get_replicator();
	Replicator *cr = p.client.get_replicator();
	CHECK(sr->is_server());
	CHECK_FALSE(cr->is_server());
	CHECK(cr->get_unique_id() == 2);

	Entity e = p.server.create_entity();
	p.server.add<Health>(e, Health{ 75, 1.0f });
	p.server.add<Pos>(e, Pos{ 3, 4 });
	p.server.add<Marker>(e);
	sr->replicate(e);
	CHECK(sr->get_net_id(e) == 0); // Assigned on sync.

	p.step();
	uint32_t net_id = sr->get_net_id(e);
	CHECK(net_id != 0);
	Entity ce = cr->get_entity(net_id);
	REQUIRE(p.client.is_alive(ce));
	REQUIRE(p.client.has<Health>(ce));
	CHECK(p.client.get<Health>(ce)->value == 75);
	CHECK(p.client.get<Health>(ce)->regen == 1.0f);
	REQUIRE(p.client.has<Pos>(ce));
	CHECK(p.client.get<Pos>(ce)->x == 3);
	CHECK(p.client.has<Marker>(ce));
	CHECK(cr->get_authority(ce) == 1);
	CHECK_FALSE(cr->has_authority(ce));
	CHECK(sr->has_authority(e));

	// Delta update: only changed components are sent.
	int before = p.sp->sent;
	p.step();
	CHECK(p.sp->sent == before); // Nothing changed, nothing sent.
	p.server.get_mut<Health>(e)->value = 50;
	p.step();
	CHECK(p.sp->sent == before + 1);
	CHECK(p.client.get<Health>(ce)->value == 50);
	CHECK(p.client.get<Pos>(ce)->x == 3);

	// Component added later.
	ComponentId player = p.server.find_component("Player");
	void *pl = p.server.add_component(e, player);
	Dictionary v;
	v["name"] = "bob";
	v["score"] = 7;
	v["target"] = int64_t(e.id); // Self reference, remapped through net ids.
	p.server.get_component_type(player)->from_dictionary(pl, v);
	p.step();
	ComponentId cplayer = p.client.find_component("Player");
	REQUIRE(p.client.has_component(ce, cplayer));
	Dictionary cv = p.client.get_component_type(cplayer)->to_dictionary(p.client.get_component(ce, cplayer));
	CHECK(String(cv["name"]) == "bob");
	CHECK(int(cv["score"]) == 7);
	CHECK(uint64_t(int64_t(cv["target"])) == ce.id);

	// Component removal.
	p.server.remove<Health>(e);
	p.step();
	CHECK_FALSE(p.client.has<Health>(ce));
	CHECK(p.client.has<Pos>(ce));

	// Despawn.
	p.server.destroy_entity(e);
	p.step();
	CHECK_FALSE(p.client.is_alive(ce));
	CHECK(cr->get_entity(net_id).is_null());
}

TEST_CASE("[ECS][Replication] Late joining peer receives a full snapshot") {
	_ensure_peer_class();
	World server;
	_register(server);
	Ref<LoopbackPeer> sp;
	sp.instantiate();
	sp->setup(1);
	server.get_replicator()->set_peer(sp);
	for (int i = 0; i < 5; i++) {
		Entity e = server.create_entity();
		server.add<Health>(e, Health{ i, 0 });
		server.get_replicator()->replicate(e);
	}
	server.physics_update(0.016); // Assign ids, nobody to send to.

	World client;
	_register(client);
	Ref<LoopbackPeer> cp;
	cp.instantiate();
	cp->setup(2);
	client.get_replicator()->set_peer(cp);
	LoopbackPeer::link(sp.ptr(), cp.ptr());
	client.physics_update(0.016);
	Query q;
	q.init_with<Health, NetworkIdentity>(&client);
	CHECK(q.count() == 5);
	int sum = 0;
	q.each<const Health>([&](Entity, const Health &h) { sum += h.value; });
	CHECK(sum == 0 + 1 + 2 + 3 + 4);
}

TEST_CASE("[ECS][Replication] Owner authority flows client -> server and authority changes are broadcast") {
	Pair p;
	Replicator *sr = p.server.get_replicator();
	Replicator *cr = p.client.get_replicator();

	Entity e = p.server.create_entity();
	p.server.add<PlayerInput>(e);
	p.server.add<Health>(e);
	sr->replicate(e, 2); // Owned by the client.
	p.step();
	Entity ce = cr->get_entity(sr->get_net_id(e));
	REQUIRE(p.client.is_alive(ce));
	CHECK(cr->get_authority(ce) == 2);
	CHECK(cr->has_authority(ce));

	// Client writes its input; the server receives it.
	PlayerInput *in = p.client.get_mut<PlayerInput>(ce);
	in->move = Vector2(1, -1);
	in->jump = true;
	p.step();
	CHECK(p.server.get<PlayerInput>(e)->move == Vector2(1, -1));
	CHECK(p.server.get<PlayerInput>(e)->jump);

	// The server does not echo owner data back: client keeps its own value.
	in = p.client.get_mut<PlayerInput>(ce);
	in->move = Vector2(5, 5);
	p.server.get_mut<PlayerInput>(e)->move = Vector2(9, 9); // Server-side change, should not reach the owner.
	p.step();
	CHECK(p.client.get<PlayerInput>(ce)->move == Vector2(5, 5));
	CHECK(p.server.get<PlayerInput>(e)->move == Vector2(5, 5));

	// A client cannot push components it does not own.
	p.client.get_mut<Health>(ce)->value = 1;
	p.step();
	CHECK(p.server.get<Health>(e)->value == 100);

	// Authority transfer back to the server.
	sr->set_authority(e, 1);
	p.step();
	CHECK(cr->get_authority(ce) == 1);
	CHECK_FALSE(cr->has_authority(ce));
	p.client.get_mut<PlayerInput>(ce)->move = Vector2(7, 7);
	p.step();
	CHECK(p.server.get<PlayerInput>(e)->move == Vector2(5, 5)); // Rejected.
}

static int command_hits = 0;
static int command_sender = 0;
static Array command_args;
static void _cmd(World &, int p_sender, const Array &p_args, void *) {
	command_hits++;
	command_sender = p_sender;
	command_args = p_args;
}

TEST_CASE("[ECS][Replication] Commands") {
	Pair p;
	command_hits = 0;
	p.server.get_replicator()->register_native_command("hello", _cmd, nullptr);
	Array args;
	args.push_back(42);
	args.push_back("text");
	args.push_back(Vector3(1, 2, 3));
	p.client.get_replicator()->send_command(1, "hello", args);
	p.server.physics_update(0.016);
	CHECK(command_hits == 1);
	CHECK(command_sender == 2);
	REQUIRE(command_args.size() == 3);
	CHECK(int(command_args[0]) == 42);
	CHECK(String(command_args[1]) == "text");
	CHECK(Vector3(command_args[2]) == Vector3(1, 2, 3));
}

TEST_CASE("[ECS][Replication] Component encode / decode round trip") {
	World w;
	_register(w);
	Replicator *r = w.get_replicator();
	ComponentId player = w.find_component("Player");
	const ComponentType *t = w.get_component_type(player);
	Entity e = w.create_entity();
	void *c = w.add_component(e, player);
	Dictionary v;
	v["name"] = "alice";
	v["score"] = -12345;
	t->from_dictionary(c, v);
	LocalVector<uint8_t> buf;
	r->encode_component(*t, c, buf);
	CHECK(buf.size() > 0);

	Entity e2 = w.create_entity();
	void *c2 = w.add_component(e2, player);
	CHECK(r->decode_component(*t, c2, buf.ptr(), buf.size()));
	Dictionary out = t->to_dictionary(c2);
	CHECK(String(out["name"]) == "alice");
	CHECK(int(out["score"]) == -12345);

	// Truncated payload is rejected.
	CHECK_FALSE(r->decode_component(*t, c2, buf.ptr(), 3));

	// Layout hash depends on field definitions.
	const ComponentType *h = w.get_component_type(w.component_id<Health>());
	CHECK(h->layout_hash() != t->layout_hash());
}

TEST_CASE("[ECS][Replication] Mismatched component layouts are ignored safely") {
	_ensure_peer_class();
	World server;
	_register(server);
	World client;
	// Same name, different layout.
	client.register_component<PlayerInput>("Health", COMPONENT_FLAG_REPLICATED);
	ComponentType *t = client.get_component_type_mut(client.component_id<PlayerInput>());
	t->add_field("move", Variant::VECTOR2, offsetof(PlayerInput, move), sizeof(Vector2));
	client.register_component<Pos>("Pos", COMPONENT_FLAG_REPLICATED);

	Ref<LoopbackPeer> sp;
	Ref<LoopbackPeer> cp;
	sp.instantiate();
	cp.instantiate();
	sp->setup(1);
	cp->setup(2);
	server.get_replicator()->set_peer(sp);
	client.get_replicator()->set_peer(cp);
	LoopbackPeer::link(sp.ptr(), cp.ptr());
	ERR_PRINT_OFF;
	client.physics_update(0.016);
	ERR_PRINT_ON;

	Entity e = server.create_entity();
	server.add<Health>(e, Health{ 5, 5 });
	server.add<Pos>(e, Pos{ 1, 1 });
	server.get_replicator()->replicate(e);
	server.physics_update(0.016);
	client.physics_update(0.016);
	Entity ce = client.get_replicator()->get_entity(1);
	REQUIRE(client.is_alive(ce));
	CHECK(client.has<Pos>(ce));
	CHECK_FALSE(client.has<PlayerInput>(ce)); // "Health" with a different layout was skipped.
}

} // namespace TestECSReplication
