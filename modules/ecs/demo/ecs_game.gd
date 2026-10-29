class_name ECSGame
extends ECSMainLoop

# A complete, node-less game loop: the world, its entities and systems replace
# the scene tree. Run with `godot --path <this folder>`; add `--server` or
# `--client` to try replication over ENet.

const PORT := 7777
const SPEED := 200.0

var ecs: ECSWorld
var inputs: ECSQuery
var player := 0


func _initialize() -> void:
	ecs = get_world()

	# Script defined components are stored natively in archetype columns.
	# "Bouncer" comes from res://bouncer_schema.tres (Project Settings > ECS > Components > Schemas).
	ecs.register_component(&"PlayerInput", {&"move": TYPE_VECTOR2}, ECSWorld.COMPONENT_FLAG_OWNER_AUTHORITY | ECSWorld.COMPONENT_FLAG_UNRELIABLE)
	ecs.register_component(&"Selected", {}, ECSWorld.COMPONENT_FLAG_SPARSE)

	inputs = ecs.query([&"PlayerInput", &"Velocity2D"])

	# Systems: an event driven ECSSystem object (see bounce_system.gd) and a
	# Callable attached to a stage. The built-in movement / render systems
	# integrate velocities and push transforms to the RenderingServer.
	# "reset" is a project event (ECS Events panel / Project Settings > ECS > Events > Custom).
	ecs.add_system_object(BounceSystem.new())
	ecs.add_system(&"player_input", _input_system, ECSWorld.STAGE_PRE_PHYSICS, 0, [&"PlayerInput"], [&"Velocity2D"])
	# Lifecycle subscriptions on top of the engine events.
	ecs.subscribe(ECSWorld.EVENT_ENTITY_SPAWNED, _on_entity_spawned)
	ecs.subscribe(ECSWorld.EVENT_LATE_UPDATE, func(event): if Engine.get_process_frames() % 300 == 0: print("late update, entities: ", ecs.get_entity_count(), " delta: ", event.delta))

	var args := OS.get_cmdline_user_args()
	if "--client" in args:
		var peer := ENetMultiplayerPeer.new()
		peer.create_client("127.0.0.1", PORT)
		ecs.set_multiplayer_peer(peer)
		ecs.subscribe(ECSWorld.EVENT_AUTHORITY_CHANGED, _on_authority_changed)
	else:
		if "--server" in args:
			var peer := ENetMultiplayerPeer.new()
			peer.create_server(PORT)
			ecs.set_multiplayer_peer(peer)
			ecs.peer_connected.connect(_on_peer_connected)
		_spawn_world()

	# A camera is just another entity; it is local to each peer.
	ecs.create_entity({&"LocalTransform2D": null, &"Camera2D": null})


func _spawn_world() -> void:
	var texture: Texture2D = load("res://icon.svg")
	# Balls are instantiated from an ECSPrefab resource authored in the editor.
	var ball: ECSPrefab = load("res://ball.tres")
	for i in 2000:
		var e := ecs.instantiate_prefab(ball)
		ecs.set_field(e, &"LocalTransform2D", &"transform", Transform2D(0.0, Vector2(randf_range(-400, 400), randf_range(-300, 300))))
		# Velocity is assigned by BounceSystem when it receives EVENT_ENTITY_CREATE.
		ecs.set_sprite_texture(e, texture)
		ecs.set_field(e, &"Sprite2D", &"modulate", Color.from_hsv(randf(), 0.7, 1.0))
		ecs.set_field(e, &"Sprite2D", &"size", Vector2(32, 32))
		ecs.replicate(e)

	player = ecs.create_entity({
		&"LocalTransform2D": null,
		&"Velocity2D": null,
		&"PlayerInput": null,
	})
	ecs.set_sprite_texture(player, texture)
	ecs.set_field(player, &"Sprite2D", &"size", Vector2(64, 64))
	ecs.replicate(player)


func _on_peer_connected(id: int) -> void:
	# Hand the player entity to the first client that connects.
	ecs.set_authority(player, id)


func _on_entity_spawned(event: Dictionary) -> void:
	# Entities spawned from a prefab already got their local-only Sprite2D
	# from the prefab (the replicated "Prefab" component carries its path);
	# textures are resources local to each peer, so assign one here.
	var entity: int = event.entity
	if not ecs.is_server() and ecs.has_component(entity, &"LocalTransform2D"):
		var size: Vector2 = ecs.get_field(entity, &"Sprite2D", &"size") if ecs.has_component(entity, &"Sprite2D") else Vector2(32, 32)
		ecs.set_sprite_texture(entity, load("res://icon.svg"))
		ecs.set_field(entity, &"Sprite2D", &"size", size)


func _on_authority_changed(event: Dictionary) -> void:
	if event.peer == ecs.get_unique_id():
		player = event.entity


func _input_system(_delta: float) -> void:
	# The owner of the player entity writes its Input component (replicated
	# owner -> server); the server turns Input into Velocity2D (replicated
	# server -> everyone).
	if player != 0 and ecs.is_alive(player) and ecs.has_authority(player):
		var move := Input.get_vector(&"ui_left", &"ui_right", &"ui_up", &"ui_down")
		ecs.set_field(player, &"PlayerInput", &"move", move)
	if ecs.is_server():
		for e in inputs.get_entities():
			var move: Vector2 = ecs.get_field(e, &"PlayerInput", &"move")
			if ecs.get_field(e, &"Velocity2D", &"linear") != move * SPEED:
				ecs.set_field(e, &"Velocity2D", &"linear", move * SPEED)


func _input(event: InputEvent) -> void:
	if event.is_action_pressed(&"ui_cancel"):
		quit()
	elif event.is_action_pressed(&"ui_accept"):
		ecs.emit_event(&"reset", Vector2.ZERO) # Custom event handled by BounceSystem.
