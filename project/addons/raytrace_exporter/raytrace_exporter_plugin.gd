@tool
extends EditorPlugin

const OUTPUT_PATH := "res://raytrace_output/current_scene.png"
const SAMPLES_PER_PIXEL := 16
const MAX_DEPTH := 4
const SEED := 1

var _button: Button


func _enter_tree() -> void:
	_button = Button.new()
	_button.text = "Trace PNG"
	_button.tooltip_text = "Render the current 3D editor view to a PNG."
	_button.pressed.connect(_on_trace_pressed)
	add_control_to_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)


func _exit_tree() -> void:
	if _button != null:
		remove_control_from_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)
		_button.queue_free()
		_button = null


func _on_trace_pressed() -> void:
	var editor_interface := get_editor_interface()
	var root := editor_interface.get_edited_scene_root()
	if root == null:
		push_error("Ray trace export failed: no edited scene root.")
		return

	var viewport := editor_interface.get_editor_viewport_3d(0)
	if viewport == null:
		push_error("Ray trace export failed: no 3D editor viewport.")
		return

	var camera := viewport.get_camera_3d()
	if camera == null:
		push_error("Ray trace export failed: no 3D editor viewport camera.")
		return

	var viewport_size := viewport.get_visible_rect().size
	var image_size := Vector2i(int(viewport_size.x), int(viewport_size.y))
	if image_size.x <= 0 or image_size.y <= 0:
		push_error("Ray trace export failed: invalid viewport size.")
		return

	_button.disabled = true
	_button.text = "Tracing..."
	await get_tree().process_frame

	var result: Dictionary = RayTraceExporter.render_scene_to_png(
		root,
		camera,
		image_size,
		OUTPUT_PATH,
		SAMPLES_PER_PIXEL,
		MAX_DEPTH,
		SEED
	)

	if _button != null:
		_button.disabled = false
		_button.text = "Trace PNG"

	if not result.get("ok", false):
		push_error("Ray trace export failed: %s" % result.get("error", "unknown error"))
		return

	var filesystem := editor_interface.get_resource_filesystem()
	if filesystem != null:
		filesystem.scan()

	print("Ray trace PNG saved to %s" % result.get("path", OUTPUT_PATH))
