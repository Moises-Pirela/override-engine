# Override Engine roadmap

Override Engine is replacing Godot's node-based workflow with a native Entity
Component System (ECS). This roadmap tracks that work. Implementation details
of the ECS live in [`modules/ecs/README.md`](modules/ecs/README.md).

Status: ✅ done · 🚧 in progress · 📋 planned

## Phase 1: ECS core ✅

- ✅ Archetype-based storage: structure-of-arrays tables, native component
  layouts (including components declared from scripts), archetype graph edges.
- ✅ Sparse-set storage for tags and components that change often (`Disabled`,
  ...), so adding or removing them never moves the entity.
- ✅ 64-bit generational entity handles.
- ✅ Cached queries with typed iteration and change detection based on ticks.
- ✅ Parallel iteration over `WorkerThreadPool`.
- ✅ Command buffers for deferred, thread-safe structural changes.
- ✅ Stage-based scheduler (frame and fixed-tick stages), with automatic
  parallel batching of systems whose reads and writes don't conflict.
- ✅ Event bus: lifecycle, stage, networking and custom events, plus
  subscriptions that can filter by component.

## Phase 2: Running without the SceneTree ✅

- ✅ `ECSMainLoop`: a whole project runs without creating a single node.
- ✅ Engine changes so projects with a custom `MainLoop` don't need a main
  scene (`main/main.cpp`, the editor's play button, the scene debugger).
- ✅ Built-in components and systems that replace common nodes: 2D/3D
  transforms with parent hierarchy, velocity, `Sprite2D`, `MeshInstance3D`,
  `Camera2D/3D`, and `PhysicsBody2D/3D` (static, kinematic, rigid).
- ✅ Scripting API: `ECSWorld`, `ECSQuery`, `ECSSystem`, plus bulk column
  access through packed arrays.

## Phase 3: Multiplayer ✅

- ✅ Replicator that works directly on any `MultiplayerPeer`, with no
  `MultiplayerAPI`, spawner or synchronizer nodes.
- ✅ Server-assigned network IDs, spawning and despawning, and full snapshots
  for players who join late.
- ✅ Delta replication from change ticks, reliable and unreliable channels,
  and packets batched up to the MTU.
- ✅ Owner authority for components, validated and relayed by the server.
- ✅ Component registry remapped by name, with layout hashes checked across
  peers.
- ✅ RPC-like commands.

## Phase 4: Editor tooling 🚧

- ✅ `ECSPrefab` resource with an inspector: *Add Component* menu, typed
  properties per field, undo/redo.
- ✅ `ECSComponentSchema`: components declared with no code, registered from
  project settings.
- ✅ **ECS Events** panel: custom event types and generated `ECSSystem` scripts.
- ✅ **ECS Profiler** panel and debugger tab: timings per system, memory use,
  archetypes.
- 📋 A world or prefab viewport: place, select and transform entities visually
  instead of only through the inspector.
- 📋 Live entity inspector for a running game (the remote scene tree's
  equivalent).
- 📋 An ECS project template in the project manager.

## Phase 5: Feature parity with the node workflow 📋

Built-in components still needed before the common node types are covered:

- 📋 Physics: collision and contact events, areas or triggers, raycast and
  shape queries, joints, character controller.
- 📋 Lights, environment and other 3D rendering features beyond meshes and
  cameras.
- 📋 Audio playback (2D/3D positional).
- 📋 Animation: skeletons, animation playback, tweens.
- 📋 Tilemaps, particles, text and other 2D features beyond sprites.
- 📋 UI: a way to build interfaces without `Control` nodes, or a deliberate
  bridge to them.
- 📋 Input mapped to components or events.
- 📋 Parallel iteration for queries with sparse component terms (not supported
  yet).

## Phase 6: Networking improvements 📋

- 📋 Client-side prediction and reconciliation for owner-authority components.
- 📋 Interpolation of replicated transforms.
- 📋 Interest management: replicate only what each peer needs.
- 📋 Bandwidth stats in the ECS Profiler.

## Phase 7: Making ECS the default 📋

- 📋 Override Engine documentation and tutorials for the ECS workflow.
- 📋 More demo projects (3D, physics, multiplayer).
- 📋 Decide what happens to the `SceneTree` and node classes: keep them as an
  optional legacy path, or strip them from default builds.
- 📋 Rebrand the editor, project manager and binaries from Godot to Override
  Engine.
