# Override Engine ECS

A native, archetype driven Entity Component System with built-in multiplayer
replication. It is a **replacement** for the node driven workflow, not a layer on
top of it: an `ecs::World` never touches `Node` or `SceneTree`, it talks to the
`RenderingServer` and to a `MultiplayerPeer` directly, and `ECSMainLoop` runs a
whole project without instantiating a single node.

```
modules/ecs/
├── core/        storage & scheduling (no Object dependency besides Variant/StringName)
│   ├── ecs_bitset.h            fixed width bitset -> archetype signatures / query masks
│   ├── ecs_sparse_set.h        paged sparse set  -> sparse components, net ids, peer state
│   ├── ecs_sparse_storage.h    sparse component storage (sparse set + packed blob array)
│   ├── ecs_entity.h            Entity = 32 bit index + 32 bit generation
│   ├── ecs_component.*         component type descriptions, reflection, script layouts
│   ├── ecs_archetype.*         SoA tables + archetype graph edges
│   ├── ecs_query.*             cached archetype matching, typed / parallel iteration
│   ├── ecs_event.h             event ids, masks, subscriptions (lifecycle hooks)
│   ├── ecs_system.h            stages, system descriptors (stage and/or event driven)
│   ├── ecs_command_buffer.h    deferred structural changes (thread safe)
│   └── ecs_world.*             entities, registry, scheduler, observers
├── net/ecs_replicator.*        spawn / despawn / delta replication / authority / commands
├── components/                 built-in components and systems (transforms, sprites, meshes, cameras, physics)
├── resources/ecs_prefab.*      ECSPrefab / ECSComponentSchema resources, ECSComponentLibrary
├── resources/ecs_event_library.* project-level custom event types
├── editor/ecs_editor_plugin.*  inspector plugins for prefabs and schemas
├── editor/ecs_events_panel.*   "ECS Events" bottom panel (event types, system script generator)
├── editor/ecs_profiler_panel.* "ECS Profiler" bottom panel + debugger session tab
├── bindings/ecs_profiler.*     game-side EngineProfiler streaming world snapshots to the editor
├── icons/                      editor icons
├── bindings/                   ECSWorld, ECSQuery, ECSSystem (RefCounted) and ECSMainLoop (MainLoop)
├── doc_classes/                class reference
└── tests/                      doctest unit tests (core + replication over a loopback peer)
```

## Storage model

* **Entities** are 64 bit handles (`index | generation << 32`). Slots are
  recycled; the generation makes stale handles detectable in O(1).
* **Archetypes** hold every entity with the same set of *table* components in
  structure-of-arrays columns (`Archetype::Column`), one contiguous buffer per
  component, plus a per-column array of change ticks. Rows are swap-removed so
  columns never have holes and iteration is a linear scan. Component layouts
  are native (`Transform3D` is 48 bytes, not a `Variant`), including components
  declared from scripts.
* **Bitsets** (`ECSBitSet<512>`) describe archetype signatures and query masks.
  Matching an archetype against a query is `contains_all(include) &&
  !intersects(exclude)` on 8 machine words. The world keeps a
  `HashMap<ComponentMask, Archetype*>` plus cached add/remove edges between
  archetypes so repeated migrations are a single hash lookup.
* **Sparse sets** (`ECSSparseSet`, paged sparse index + dense arrays) back:
  * components flagged `COMPONENT_FLAG_SPARSE` (`SparseComponentStorage`):
    adding/removing them never migrates the entity, so they are the right tool
    for tags and state that flips every frame (`Disabled`, `Selected`, …).
    Queries check sparse terms per entity against the sparse set;
  * network id → entity maps and per-peer "known entity" sets in the replicator.
* **Queries** cache the list of matching archetypes and, because archetypes are
  append-only, only test archetypes created after the last refresh.
  `Query::each<const A, B>()` gives typed SoA iteration and marks written
  columns changed; `Query::for_each_parallel()` splits rows over the
  `WorkerThreadPool`.
* **Change detection** is tick based: every column row stores the tick of its
  last mutable access. Render sync and replication only touch what changed.

## Events (engine lifecycle hooks)

Inspired by CryEngine's entity events: a system declares an **event mask** and
handles everything in one `ProcessEvent`-style callback. `ecs::World` owns an
event bus with built-in events (`ecs_event.h`):

| Event | When |
|---|---|
| `EVENT_ENTITY_CREATE` (OnEntityCreate), `EVENT_COMPONENT_ADDED` | queued, delivered at the next flush once the entity is fully initialized |
| `EVENT_ENTITY_DESTROY`, `EVENT_COMPONENT_REMOVED` | immediate, while the data is still readable |
| `EVENT_PRE_UPDATE`, `EVENT_UPDATE`, `EVENT_LATE_UPDATE`, `EVENT_RENDER` | after the systems of the matching frame stage |
| `EVENT_PRE_FIXED_UPDATE`, `EVENT_FIXED_UPDATE`, `EVENT_LATE_FIXED_UPDATE` | after the systems of the matching fixed stage |
| `EVENT_PEER_CONNECTED/DISCONNECTED`, `EVENT_ENTITY_SPAWNED/DESPAWNED`, `EVENT_AUTHORITY_CHANGED` | networking |
| `register_event(name)` | user events, raised with `emit_event()` (immediate) or `queue_event()` (thread safe, deferred) |

Custom event **types** are declared in the editor: the **ECS Events** bottom
panel adds/removes names ("OnPlayerHeal") stored in the `ecs/events/custom`
project setting (`ECSEventLibrary`), registers them in every `ECSWorld` and can
generate an `ECSSystem` script subscribed to the selected events. Raising
`world.emit_event(&"OnPlayerHeal", amount, entity)` then runs every system /
subscription that referenced the name.

Subscriptions (`World::subscribe`, `SubscriptionDesc`) take a native handler or
a script `Callable`, an optional component filter (entity events only for
entities having those components), an optional subject entity and an order.
`SystemDesc` gained `events` / `event_func` so a system can be stage driven,
event driven, or both. From scripts: `ECSWorld.subscribe()` or an `ECSSystem`
subclass (`_get_events()`, `_get_component_filter()`, `_process_event()`)
registered with `add_system_object()`. Emitters check `has_subscribers()` so
unused events cost nothing on the hot paths.

## Systems & scheduling

Systems are plain functions (`SystemFunc`) or script `Callable`s attached to a
stage (`PRE_UPDATE`, `UPDATE`, `POST_UPDATE`, `RENDER` for frames;
`PRE_PHYSICS`, `PHYSICS`, `POST_PHYSICS`, `NETWORK` for fixed ticks). Within a
stage, systems sharing the same `order` that declare non-conflicting read/write
component sets and are marked `thread_safe` are batched and executed in
parallel on the `WorkerThreadPool`. Structural changes from systems go through
`World::commands()` and are applied after each batch.

Built-in systems replace the usual nodes: `Velocity2D/3D` integration,
`Parent` based transform propagation (`LocalTransform*` → `GlobalTransform*`),
`Sprite2D` → canvas items, `MeshInstance3D` → render instances, `Camera2D/3D`
→ viewport camera. All of them are change-tick driven.

## Multiplayer

`ecs::Replicator` works directly on a `MultiplayerPeer` (ENet, WebRTC,
WebSocket, …). No `MultiplayerAPI`, spawner or synchronizer nodes.

* Entities with a `NetworkIdentity` component (`World::get_replicator()->replicate(e)`)
  are spawned on every client with their replicated components; the server
  assigns network ids, late joiners receive a full snapshot.
* `COMPONENT_FLAG_REPLICATED` components are delta synced server → clients
  using the change ticks; `COMPONENT_FLAG_UNRELIABLE` selects unreliable
  ordered transfer and packets are batched up to the MTU.
* `COMPONENT_FLAG_OWNER_AUTHORITY` components are sent by the peer that owns the
  entity (`set_authority`) to the server, which validates ownership and relays
  them to the other peers (never back to the owner).
* Component ids on the wire are the server's; clients receive the server's
  registry on connection and remap by name, verifying a layout hash so
  incompatible definitions are ignored rather than decoded into the wrong
  memory. Entity reference fields are remapped through network ids.
* `send_command()` / `register_command()` provide RPC-like messages.

## Using it

From C++:

```cpp
struct Position { float x, y; };
struct Velocity { float dx, dy; };

ecs::World world;
world.register_component<Position>("Position");
world.register_component<Velocity>("Velocity");

ecs::Entity e = world.create_entity();
world.add<Position>(e, { 0, 0 });
world.add<Velocity>(e, { 1, 0 });

ecs::Query q;
q.init_with<Position, Velocity>(&world);
q.each<Position, const Velocity>([](ecs::Entity, Position &p, const Velocity &v) {
	p.x += v.dx;
});
```

From GDScript (see the class reference of `ECSWorld`, `ECSQuery` and
`ECSMainLoop`): set `application/run/main_loop_type` to a class extending
`ECSMainLoop`, register components, create entities, add systems.

## Editor: prefabs and component schemas

* **`ECSPrefab`** (Resource) — an entity definition: components with field
  values plus child prefabs. In the inspector an *Add Component* menu offers
  every built-in component and project schema (or a custom name for components
  registered from code); each field of each component is exposed as a regular
  typed property (`components/<Component>/<field>`, generated from the
  component reflection) so it gets the proper editor, undo/redo and revert.
  Spawn with `ECSWorld.instantiate_prefab()`, capture a live entity with
  `ECSWorld.create_prefab()`.
* **`ECSComponentSchema`** (Resource) — a component type declared without
  code: name, typed fields (edited with a dedicated inspector control) and
  flags. Schemas listed in *Project Settings > ECS > Components > Schemas* are
  registered in every `ECSWorld` and known to the prefab editor.
* Entities instantiated from a saved prefab carry a replicated `Prefab`
  component (`res://tank.tres`, `res://tank.tres#0` for the first child...).
  Clients use it to re-create the prefab's local-only components (sprites,
  meshes, shapes) when a replicated entity is spawned; `replicate(entity, 1,
  true)` replicates a whole prefab hierarchy.

`ECSComponentLibrary` (resources/ecs_prefab.h) is the edit-time/runtime
registry behind both: built-in component reflection plus loaded schemas.

## Profiler

`ecs::World` keeps per-system / per-subscription timings (last, average, max,
runs), the duration of the last `update()` / `physics_update()`, the number of
events dispatched per frame and a memory breakdown (`get_memory_usage()`:
archetype tables + change ticks, sparse storages, entity records).
`World::get_profile()` / `ECSWorld.get_profile()` snapshot all of it together
with the systems **in execution order** (stage batches, then the stage's event
subscribers, then entity/custom event systems) and the archetype list.

In the editor the **ECS Profiler** bottom panel shows this for the worlds
living in the editor process (or a design-time preview world, i.e. the
schedule a new world gets) and, while the game runs, the debugger gets an
**ECS Profiler** session tab fed by the game-side `ECSProfiler`
(`EngineProfiler` "ecs", `ecs:profile` messages at 10 Hz).

## Demo

`modules/ecs/demo` is a complete node-less project (`ECSGame extends ECSMainLoop`):
2000 bouncing replicated sprites, a player entity owned by the first client,
bulk column updates from GDScript and ENet replication. Run it with
`godot --path modules/ecs/demo`, `godot --path modules/ecs/demo -- --server`
and `godot --path modules/ecs/demo -- --client`.

## Engine changes outside the module

* `main/main.cpp`: a project whose `application/run/main_loop_type` is not the
  `SceneTree` no longer needs a main scene.
* `editor/editor_node.cpp`: the editor's play button follows the same rule.

## Tests

Build with `tests=yes` and run `godot --test --test-case="[ECS]*"`. The
benchmark case is skipped by default (`--no-skip` to run it).
