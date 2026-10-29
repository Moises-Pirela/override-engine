# Override Engine

## 2D and 3D cross-platform game engine, built around ECS

**Override Engine is a cross-platform game engine for making 2D and 3D games
from a unified interface.** It is a fork of [Godot Engine](https://godotengine.org),
and it keeps Godot's editor, tooling and export pipeline to desktop (Linux,
macOS, Windows), mobile (Android, iOS) and Web platforms.

## An ECS workflow instead of a node-based one

The main difference from Godot is how games are built. Godot organizes a game as
a tree of nodes, where each node mixes data and behavior and scenes are built by
nesting nodes. Override Engine uses an **Entity Component System (ECS)**
workflow instead:

- **Entities** are plain identifiers with no behavior of their own.
- **Components** are plain data attached to entities.
- **Systems** hold the logic, running over every entity that has a given set of
  components.

Keeping data apart from logic makes game state easier to reason about, makes
behavior easy to put together from components rather than deep inheritance, and
stores data in a cache-friendly layout that scales to large numbers of entities.

The ECS lives in [`modules/ecs`](modules/ecs/README.md). It replaces the
node-based workflow rather than sitting on top of it: a project whose main loop
is an `ECSMainLoop` runs without a single node or a `SceneTree`. It includes:

- **Archetype-based storage**: structure-of-arrays tables with native component
  layouts, sparse sets for tags, generational entity handles, and change
  detection based on ticks.
- **Systems and scheduling**: frame and fixed-tick stages, with systems that
  don't conflict run in parallel on the worker thread pool.
- **Events**: lifecycle, stage, networking and custom events that systems can
  subscribe to.
- **Built-in components** in place of common nodes: 2D/3D transforms with
  hierarchy, velocity, sprites, meshes, cameras and physics bodies.
- **Multiplayer**: entity replication straight over a `MultiplayerPeer`, with
  delta sync, owner authority and snapshots for players who join late, and no
  spawner or synchronizer nodes.
- **Editor tooling**: `ECSPrefab` and `ECSComponentSchema` resources with
  inspectors, an **ECS Events** panel and an **ECS Profiler**.
- **Scripting**: `ECSWorld`, `ECSQuery` and `ECSSystem` from GDScript or C++.

Feature parity with the node workflow isn't there yet: audio, animation, UI,
lights and physics contact events, among others, still need ECS components.
See the [roadmap](ROADMAP.md) for what's done and what's next.

### Quick start

Set `application/run/main_loop_type` in your project settings to a script
class that extends `ECSMainLoop`, then register components, create entities and
add systems from it. The demo project in [`modules/ecs/demo`](modules/ecs/demo)
is a complete project with no nodes: 2,000 replicated bouncing sprites over
ENet.

```bash
bin/<editor binary> --path modules/ecs/demo
```

Pass `-- --server` or `-- --client` to run it as a multiplayer server or
client.

## Free and open source

Override Engine is free and open source under the [MIT license](LICENSE.txt),
like the Godot Engine it is based on. No strings attached, no royalties.

Override Engine is not affiliated with or endorsed by the Godot Engine project
or the Godot Foundation. All credit for the original engine goes to
[Juan Linietsky](https://github.com/reduz), [Ariel Manzur](https://github.com/punto-)
and the [Godot contributors](https://github.com/godotengine/godot/graphs/contributors).

## Compiling from source

Override Engine uses the same build system as Godot. See the
[Godot compilation docs](https://docs.godotengine.org/en/latest/engine_details/development/compiling)
for instructions for every supported platform.

To run the ECS unit tests, build with `tests=yes` and run:

```bash
bin/<editor binary> --test --test-case="[ECS]*"
```

## Roadmap

See [ROADMAP.md](ROADMAP.md).

## Contributing

To get started contributing to the project, see the [contributing guide](CONTRIBUTING.md).
It also includes guidelines for reporting bugs.

## Documentation

Override Engine does not have its own documentation site yet. The ECS
architecture is described in [`modules/ecs/README.md`](modules/ecs/README.md),
and the `ECSWorld`, `ECSQuery`, `ECSSystem`, `ECSMainLoop`, `ECSPrefab` and
`ECSComponentSchema` classes are in the built-in class reference. Much of the
[Godot documentation](https://docs.godotengine.org) still applies, except where
the ECS workflow differs from Godot's node-based one.
