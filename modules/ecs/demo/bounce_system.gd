class_name BounceSystem
extends ECSSystem

# Event driven system (CryEngine style): declare the events you want in
# _get_events() and handle them all in _process_event().

const SPEED := 200.0

var bounds := Rect2(Vector2(-400, -300), Vector2(800, 600))
var movers: ECSQuery


func _get_events() -> Array:
	return [ECSWorld.EVENT_FIXED_UPDATE, ECSWorld.EVENT_ENTITY_CREATE, &"reset"]


func _get_component_filter() -> Array:
	return [&"Bouncer"] # Entity events only for bouncers.


func _on_registered() -> void:
	movers = get_world().query([&"LocalTransform2D", &"Velocity2D", &"Bouncer"])


func _process_event(event: Dictionary) -> void:
	var ecs := get_world()
	match event.event:
		ECSWorld.EVENT_ENTITY_CREATE:
			# OnEntityCreate: give every new bouncer a random heading (velocities are replicated).
			if ecs.is_server():
				ecs.set_component(event.entity, &"Velocity2D", {&"linear": Vector2(randf_range(-1, 1), randf_range(-1, 1)).normalized() * SPEED, &"angular": randf_range(-2, 2)})
		ECSWorld.EVENT_FIXED_UPDATE:
			if ecs.is_server():
				_bounce()
		_:
			if event.name == &"reset":
				# Custom event with a payload: move every bouncer to the given position.
				var xforms := []
				xforms.resize(movers.count())
				xforms.fill(Transform2D(0.0, event.payload))
				movers.set_field_array(&"LocalTransform2D", &"transform", xforms)


func _bounce() -> void:
	# Bulk column access: packed arrays straight out of the archetype tables.
	var xforms: Array = movers.get_field_array(&"LocalTransform2D", &"transform")
	var velocities: PackedVector2Array = movers.get_field_array(&"Velocity2D", &"linear")
	var bounces: PackedInt64Array = movers.get_field_array(&"Bouncer", &"bounces")
	var changed := false
	for i in xforms.size():
		var origin: Vector2 = xforms[i].origin
		var v: Vector2 = velocities[i]
		var bounced := false
		if (origin.x < bounds.position.x and v.x < 0) or (origin.x > bounds.end.x and v.x > 0):
			v.x = -v.x
			bounced = true
		if (origin.y < bounds.position.y and v.y < 0) or (origin.y > bounds.end.y and v.y > 0):
			v.y = -v.y
			bounced = true
		if bounced:
			velocities[i] = v
			bounces[i] += 1
			changed = true
	if changed:
		movers.set_field_array(&"Velocity2D", &"linear", velocities)
		movers.set_field_array(&"Bouncer", &"bounces", bounces)
