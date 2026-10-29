/**************************************************************************/
/*  ecs_replicator.h                                                      */
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

#include "../core/ecs_component.h"
#include "../core/ecs_entity.h"
#include "../core/ecs_sparse_set.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/callable.h"
#include "scene/main/multiplayer_peer.h"

namespace ecs {

class World;
class Archetype;

// Native entity/component replication on top of a MultiplayerPeer.
//
// There is no SceneTree, MultiplayerAPI, spawner or synchronizer node
// involved: the replicator reads the archetype tables directly, uses the
// per-column change ticks for delta compression and writes packets straight
// to the peer.
//
// Model:
//  - Entities with a NetworkIdentity component are replicated. The server
//    assigns network ids and broadcasts SPAWN / DESPAWN.
//  - Components flagged COMPONENT_FLAG_REPLICATED flow server -> clients.
//  - Components flagged COMPONENT_FLAG_OWNER_AUTHORITY flow from the peer
//    that has authority over the entity to the server, which relays them to
//    the other peers (never back to the owner).
//  - Component ids on the wire are the server's ids; clients receive the
//    server registry on connection and remap by name (layout hashes are
//    verified so incompatible definitions are ignored instead of corrupting
//    memory).
//  - Commands are lightweight RPCs: a name and an Array of arguments.
class Replicator {
public:
	enum Message : uint8_t {
		MSG_REGISTRY = 1,
		MSG_SPAWN,
		MSG_DESPAWN,
		MSG_UPDATE,
		MSG_REMOVE_COMPONENT,
		MSG_AUTHORITY,
		MSG_COMMAND,
	};

	class Listener {
	public:
		virtual void on_peer_connected(int p_peer) {}
		virtual void on_peer_disconnected(int p_peer) {}
		virtual void on_entity_spawned(Entity p_entity, uint32_t p_net_id) {}
		virtual void on_entity_despawned(Entity p_entity, uint32_t p_net_id) {}
		virtual void on_authority_changed(Entity p_entity, int p_peer) {}
		// Return true when the command was handled.
		virtual bool on_command(int p_sender, const StringName &p_name, const Array &p_args) { return false; }
		virtual ~Listener() {}
	};

	typedef void (*NativeCommandFunc)(World &p_world, int p_sender, const Array &p_args, void *p_userdata);

	struct Stats {
		uint64_t packets_sent = 0;
		uint64_t packets_received = 0;
		uint64_t bytes_sent = 0;
		uint64_t bytes_received = 0;
		uint32_t last_sync_updates = 0;
	};

private:
	struct PeerInfo {
		bool ready = false; // Registry exchanged.
		LocalVector<ComponentId> remote_to_local; // Client side: wire id -> local id.
		ECSSparseSet<uint8_t> known; // Server side: net ids this peer knows about.
	};

	struct NativeCommand {
		NativeCommandFunc func = nullptr;
		void *userdata = nullptr;
	};

	World *world = nullptr;
	Ref<MultiplayerPeer> peer;
	Callable connected_callable;
	Callable disconnected_callable;

	HashMap<int, PeerInfo> peers;
	ECSSparseSet<Entity> net_entities; // net id -> entity.
	LocalVector<ComponentId> local_to_remote; // Client side: local id -> wire id (INVALID if unknown to server).
	uint32_t next_net_id = 1;
	uint32_t last_sync_tick = 0;
	uint32_t sync_interval = 1; // In physics ticks.
	uint32_t ticks_since_sync = 0;
	uint32_t max_unreliable_packet_size = 1200;
	ComponentId identity_id = INVALID_COMPONENT;
	bool applying_remote = false;

	LocalVector<Entity> new_spawns;
	LocalVector<uint32_t> pending_despawns;
	struct PendingRemove {
		uint32_t net_id;
		ComponentId component;
	};
	LocalVector<PendingRemove> pending_removes;
	struct PendingAuthority {
		uint32_t net_id;
		int32_t authority;
	};
	LocalVector<PendingAuthority> pending_authority;

	HashMap<StringName, Callable> script_commands;
	HashMap<StringName, NativeCommand> native_commands;
	LocalVector<Listener *> listeners;
	Stats stats;

	// Scratch buffers.
	LocalVector<uint8_t> buf;

	static void _on_entity_destroyed(World &p_world, Entity p_entity, void *p_userdata);
	static void _on_component_removed(World &p_world, Entity p_entity, ComponentId p_component, void *p_userdata);

	void _on_peer_connected(int p_peer);
	void _on_peer_disconnected(int p_peer);

	void _send(int p_peer, const uint8_t *p_data, uint32_t p_size, MultiplayerPeer::TransferMode p_mode);
	void _send_registry(int p_peer);
	void _send_spawn(int p_peer, Entity p_entity, uint32_t p_net_id);
	void _send_snapshot(int p_peer);
	void _handle_packet(int p_sender, const uint8_t *p_data, int p_size);
	void _handle_registry(int p_sender, const uint8_t *p_data, int p_size);
	void _handle_spawn(int p_sender, const uint8_t *p_data, int p_size);
	void _handle_despawn(int p_sender, const uint8_t *p_data, int p_size);
	void _handle_update(int p_sender, const uint8_t *p_data, int p_size);
	void _handle_remove(int p_sender, const uint8_t *p_data, int p_size);
	void _handle_authority(int p_sender, const uint8_t *p_data, int p_size);
	void _handle_command(int p_sender, const uint8_t *p_data, int p_size);

	void _encode_component(const ComponentType &p_type, const void *p_component, LocalVector<uint8_t> &r_buf) const;
	bool _decode_component(const ComponentType &p_type, void *p_component, const uint8_t *p_data, uint32_t p_size) const;
	void _encode_field(const ComponentField &p_field, const uint8_t *p_src, LocalVector<uint8_t> &r_buf) const;
	uint32_t _decode_field(const ComponentField &p_field, uint8_t *p_dst, const uint8_t *p_data, uint32_t p_size) const;

	ComponentId _wire_to_local(int p_sender, uint32_t p_wire_id) const;
	uint32_t _local_to_wire(ComponentId p_local) const;
	bool _should_send(const ComponentType &p_type, int32_t p_authority, int p_to_peer) const;

	void _sync_server();
	void _sync_client();
	void _collect_updates(int p_to_peer, bool p_from_owner, LocalVector<uint8_t> &r_reliable, LocalVector<uint8_t> &r_unreliable, uint32_t &r_reliable_count, uint32_t &r_unreliable_count, int p_exclude_peer);
	void _flush_update_packet(int p_to_peer, LocalVector<uint8_t> &p_packet, uint32_t &p_count, MultiplayerPeer::TransferMode p_mode, bool p_force);

public:
	void set_peer(const Ref<MultiplayerPeer> &p_peer);
	Ref<MultiplayerPeer> get_peer() const { return peer; }
	bool has_peer() const { return peer.is_valid(); }
	bool is_server() const;
	bool is_connected() const;
	int get_unique_id() const;
	void get_peer_ids(LocalVector<int> &r_peers) const;

	// Drains incoming packets. Called from World::physics_update().
	void poll();
	// Sends spawns / despawns / changed components. Called from World::physics_update().
	void sync();

	void set_sync_interval(uint32_t p_ticks) { sync_interval = MAX(p_ticks, 1u); }
	uint32_t get_sync_interval() const { return sync_interval; }

	// Explicitly marks an entity as replicated (adds NetworkIdentity).
	void replicate(Entity p_entity, int32_t p_authority = 1);
	uint32_t get_net_id(Entity p_entity) const;
	Entity get_entity(uint32_t p_net_id) const;
	int32_t get_authority(Entity p_entity) const;
	// Server only; broadcasts the change.
	void set_authority(Entity p_entity, int32_t p_peer);
	bool has_authority(Entity p_entity) const;

	// Commands (RPC-like). Handlers receive (sender_id, args).
	void register_command(const StringName &p_name, const Callable &p_callable);
	void register_native_command(const StringName &p_name, NativeCommandFunc p_func, void *p_userdata);
	void unregister_command(const StringName &p_name);
	void send_command(int p_peer, const StringName &p_name, const Array &p_args, bool p_reliable = true);

	void add_listener(Listener *p_listener);
	void remove_listener(Listener *p_listener);

	// Peer lifecycle notifications. Normally driven by the peer's
	// peer_connected / peer_disconnected signals; exposed for custom peers.
	void poll_peer_connected(int p_peer);
	void poll_peer_disconnected(int p_peer);

	const Stats &get_stats() const { return stats; }

	// Exposed for tests.
	void encode_component(const ComponentType &p_type, const void *p_component, LocalVector<uint8_t> &r_buf) const { _encode_component(p_type, p_component, r_buf); }
	bool decode_component(const ComponentType &p_type, void *p_component, const uint8_t *p_data, uint32_t p_size) const { return _decode_component(p_type, p_component, p_data, p_size); }

	explicit Replicator(World *p_world);
	~Replicator();
};

} // namespace ecs
