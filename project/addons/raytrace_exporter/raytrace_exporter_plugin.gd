@tool
extends EditorPlugin

const OUTPUT_PATH := ""
const SAMPLES_PER_PIXEL := 256
const MAX_DEPTH := 16
const SEED := 1
const NO_JOB_ID := 0
const TILE_SIZE := 16
const MODE_FULL := 0
const MODE_PIXEL := 1
const MODE_TILE := 2

var _button: Button
var _render_mode_option: OptionButton
var _target_x_spin_box: SpinBox
var _target_y_spin_box: SpinBox
var _active_job_id := NO_JOB_ID
var _cancel_requested := false


func _enter_tree() -> void:
	_render_mode_option = OptionButton.new()
	_render_mode_option.add_item("Full", MODE_FULL)
	_render_mode_option.add_item("Pixel", MODE_PIXEL)
	_render_mode_option.add_item("Tile", MODE_TILE)
	_render_mode_option.tooltip_text = "Render the full image, one selected pixel ray, or one selected 16x16 tile."
	_render_mode_option.item_selected.connect(_on_render_mode_selected)
	add_control_to_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _render_mode_option)

	_target_x_spin_box = _create_target_spin_box("X ")
	_target_y_spin_box = _create_target_spin_box("Y ")
	add_control_to_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _target_x_spin_box)
	add_control_to_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _target_y_spin_box)
	_update_target_controls()

	_button = Button.new()
	_button.text = "Trace PNG"
	_button.tooltip_text = "Render the current 3D editor view to a PNG."
	_button.pressed.connect(_on_trace_pressed)
	add_control_to_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)
	set_process(true)


func _create_target_spin_box(prefix: String) -> SpinBox:
	var spin_box := SpinBox.new()
	spin_box.min_value = 0
	spin_box.max_value = 0
	spin_box.step = 1
	spin_box.allow_greater = false
	spin_box.allow_lesser = false
	spin_box.custom_minimum_size.x = 78
	spin_box.set_prefix(prefix)
	return spin_box


func _on_render_mode_selected(_index: int) -> void:
	_update_target_controls()
	var viewport := get_editor_interface().get_editor_viewport_3d(0)
	if viewport == null:
		return

	var viewport_size := viewport.get_visible_rect().size
	var image_size := Vector2i(int(viewport_size.x), int(viewport_size.y))
	if image_size.x > 0 and image_size.y > 0:
		_configure_target_range(image_size, _current_render_mode())


func _current_render_mode() -> int:
	if _render_mode_option == null:
		return MODE_FULL
	return _render_mode_option.get_selected_id()


func _render_mode_name(mode: int) -> String:
	match mode:
		MODE_PIXEL:
			return "pixel"
		MODE_TILE:
			return "tile"
		_:
			return "full"


func _update_target_controls() -> void:
	var mode := _current_render_mode()
	var show_target := mode != MODE_FULL
	var editable := show_target and _active_job_id == NO_JOB_ID

	if _target_x_spin_box != null:
		_target_x_spin_box.visible = show_target
		_target_x_spin_box.editable = editable
		_target_x_spin_box.tooltip_text = "Pixel X coordinate." if mode == MODE_PIXEL else "16x16 tile X index."
	if _target_y_spin_box != null:
		_target_y_spin_box.visible = show_target
		_target_y_spin_box.editable = editable
		_target_y_spin_box.tooltip_text = "Pixel Y coordinate." if mode == MODE_PIXEL else "16x16 tile Y index."


func _configure_target_range(image_size: Vector2i, mode: int) -> void:
	if _target_x_spin_box == null or _target_y_spin_box == null:
		return

	if mode == MODE_TILE:
		_target_x_spin_box.max_value = max(ceili(float(image_size.x) / float(TILE_SIZE)) - 1, 0)
		_target_y_spin_box.max_value = max(ceili(float(image_size.y) / float(TILE_SIZE)) - 1, 0)
	else:
		_target_x_spin_box.max_value = max(image_size.x - 1, 0)
		_target_y_spin_box.max_value = max(image_size.y - 1, 0)


func _exit_tree() -> void:
	if _active_job_id != NO_JOB_ID and ClassDB.class_exists(&"RayTraceExporter"):
		ClassDB.class_call_static(&"RayTraceExporter", &"cancel_render_job", _active_job_id)
		_active_job_id = NO_JOB_ID

	set_process(false)

	if _button != null:
		remove_control_from_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)
		_button.queue_free()
		_button = null

	if _target_y_spin_box != null:
		remove_control_from_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _target_y_spin_box)
		_target_y_spin_box.queue_free()
		_target_y_spin_box = null

	if _target_x_spin_box != null:
		remove_control_from_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _target_x_spin_box)
		_target_x_spin_box.queue_free()
		_target_x_spin_box = null

	if _render_mode_option != null:
		remove_control_from_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _render_mode_option)
		_render_mode_option.queue_free()
		_render_mode_option = null


func _process(_delta: float) -> void:
	if _active_job_id == NO_JOB_ID:
		return

	if not ClassDB.class_exists(&"RayTraceExporter"):
		push_error("Ray trace export failed: RayTraceExporter GDExtension is not loaded.")
		_reset_job_state()
		return

	var result: Dictionary = ClassDB.class_call_static(
		&"RayTraceExporter",
		&"poll_render_job",
		_active_job_id
	)

	if not result.get("exists", false):
		push_error("Ray trace export failed: render job disappeared.")
		_reset_job_state()
		return

	_update_button_progress(float(result.get("progress", 0.0)))

	if not result.get("done", false):
		return

	var job_id := _active_job_id
	var was_cancelled := bool(result.get("cancelled", false))
	var ok := bool(result.get("ok", false))
	var path := String(result.get("path", OUTPUT_PATH))
	var timing_log_path := String(result.get("timing_log_path", ""))
	var total_ms := float(result.get("total_ms", 0.0))
	var render_mode := String(result.get("render_mode", "full"))
	var triangle_count := int(result.get("triangle_count", 0))
	var intersection_ms := float(result.get("intersection_ms", 0.0))
	var error := String(result.get("error", "unknown error"))

	ClassDB.class_call_static(&"RayTraceExporter", &"release_render_job", job_id)
	_reset_job_state()

	var filesystem := get_editor_interface().get_resource_filesystem()
	if filesystem != null:
		filesystem.scan()

	if was_cancelled:
		print("Ray trace PNG export cancelled.")
		_print_timing_log_path(timing_log_path)
		_print_elapsed_time(total_ms, render_mode)
		_print_render_statistics(triangle_count, intersection_ms, result)
		return

	if not ok:
		push_error("Ray trace export failed: %s" % error)
		_print_timing_log_path(timing_log_path)
		_print_elapsed_time(total_ms, render_mode)
		_print_render_statistics(triangle_count, intersection_ms, result)
		return

	print("Ray trace PNG saved to %s" % path)
	_print_timing_log_path(timing_log_path)
	_print_elapsed_time(total_ms, render_mode)
	_print_render_statistics(triangle_count, intersection_ms, result)


func _on_trace_pressed() -> void:
	if _active_job_id != NO_JOB_ID:
		_cancel_active_job()
		return

	_start_render_job()


func _start_render_job() -> void:
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

	if not ClassDB.class_exists(&"RayTraceExporter"):
		push_error("Ray trace export failed: RayTraceExporter GDExtension is not loaded.")
		return

	var render_mode_id := _current_render_mode()
	_configure_target_range(image_size, render_mode_id)
	var target := Vector2i(
		int(_target_x_spin_box.value) if _target_x_spin_box != null else 0,
		int(_target_y_spin_box.value) if _target_y_spin_box != null else 0
	)
	var render_options := {
		"output_path": OUTPUT_PATH,
		"samples_per_pixel": SAMPLES_PER_PIXEL,
		"max_depth": MAX_DEPTH,
		"seed": SEED,
		"render_mode": _render_mode_name(render_mode_id),
		"target_pixel": target,
		"target_tile": target,
	}

	if not ClassDB.class_has_method(&"RayTraceExporter", &"start_render_scene_to_png_with_options"):
		push_error(
			"Ray trace export failed: RayTraceExporter.start_render_scene_to_png_with_options is not loaded. " +
			"Rebuild the GDExtension and make sure project/bin/gdexample.gdextension points to that DLL."
		)
		return

	var result_value = ClassDB.class_call_static(
		&"RayTraceExporter",
		&"start_render_scene_to_png_with_options",
		root,
		camera,
		image_size,
		render_options
	)
	if not (result_value is Dictionary):
		push_error(
			"Ray trace export failed: native exporter returned %s instead of Dictionary." %
			type_string(typeof(result_value))
		)
		return

	var result: Dictionary = result_value

	if not result.get("ok", false):
		push_error("Ray trace export failed: %s" % result.get("error", "unknown error"))
		_print_timing_log_path(String(result.get("timing_log_path", "")))
		_print_elapsed_time(
			float(result.get("total_ms", 0.0)),
			String(result.get("render_mode", _render_mode_name(render_mode_id)))
		)
		_print_render_statistics(
			int(result.get("triangle_count", 0)),
			float(result.get("intersection_ms", 0.0)),
			result
		)
		return

	_active_job_id = int(result.get("job_id", NO_JOB_ID))
	_cancel_requested = false
	if _active_job_id == NO_JOB_ID:
		push_error("Ray trace export failed: no render job was created.")
		return

	_update_button_progress(float(result.get("progress", 0.0)))


func _cancel_active_job() -> void:
	if _active_job_id == NO_JOB_ID or _cancel_requested:
		return

	_cancel_requested = true
	_update_button_progress(0.0)

	if not ClassDB.class_exists(&"RayTraceExporter"):
		_reset_job_state()
		return

	var result: Dictionary = ClassDB.class_call_static(
		&"RayTraceExporter",
		&"cancel_render_job",
		_active_job_id
	)
	if not result.get("exists", false):
		_reset_job_state()
		return

	_update_button_progress(float(result.get("progress", 0.0)))


func _reset_job_state() -> void:
	_active_job_id = NO_JOB_ID
	_cancel_requested = false
	if _button != null:
		_button.disabled = false
		_button.text = "Trace PNG"
	if _render_mode_option != null:
		_render_mode_option.disabled = false
	_update_target_controls()


func _update_button_progress(progress: float) -> void:
	if _button == null:
		return

	var percent := int(round(clamp(progress, 0.0, 1.0) * 100.0))
	_button.disabled = false
	if _render_mode_option != null:
		_render_mode_option.disabled = true
	if _target_x_spin_box != null:
		_target_x_spin_box.editable = false
	if _target_y_spin_box != null:
		_target_y_spin_box.editable = false
	if _cancel_requested:
		_button.text = "Cancelling %d%%" % percent
	else:
		_button.text = "Cancel Trace %d%%" % percent


func _print_timing_log_path(timing_log_path: String) -> void:
	if not timing_log_path.is_empty():
		print("Ray trace timing log saved to %s" % timing_log_path)


func _print_elapsed_time(total_ms: float, render_mode: String) -> void:
	print("Ray trace elapsed time (%s): %.3f ms" % [render_mode, total_ms])


func _bool_text(value: bool) -> String:
	return "true" if value else "false"


func _print_render_statistics(triangle_count: int, intersection_ms: float, result: Dictionary) -> void:
	print("Ray trace triangles: %d" % triangle_count)
	print(
		"Ray trace primary rays: total=%d hit=%d miss=%d" % [
			int(result.get("primary_ray_count", 0)),
			int(result.get("primary_ray_hit_count", 0)),
			int(result.get("primary_ray_miss_count", 0)),
		]
	)

	var primary_hit_mask_path := String(result.get("primary_hit_mask_path", ""))
	if not primary_hit_mask_path.is_empty():
		if bool(result.get("ok", false)):
			print("Ray trace primary hit mask saved to %s" % primary_hit_mask_path)
		else:
			print("Ray trace primary hit mask path: %s" % primary_hit_mask_path)

	var tile_debug_path := String(result.get("tile_debug_path", ""))
	if not tile_debug_path.is_empty():
		if bool(result.get("ok", false)):
			print("Ray trace tile debug overlay saved to %s" % tile_debug_path)
		else:
			print("Ray trace tile debug overlay path: %s" % tile_debug_path)

	print(
		"Ray trace lights: %d (directional: %d, omni: %d, spot: %d, shadow: %d)" % [
			int(result.get("light_count", 0)),
			int(result.get("directional_light_count", 0)),
			int(result.get("omni_light_count", 0)),
			int(result.get("spot_light_count", 0)),
			int(result.get("shadow_light_count", 0)),
		]
	)

	var lights_value = result.get("lights", [])
	if lights_value is Array:
		for light_value in lights_value:
			if not (light_value is Dictionary):
				continue

			var light: Dictionary = light_value
			print(
				"Ray trace light[%d]: type=%s energy=%.3f color=%s range=%.3f attenuation=%.3f spot_angle=%.3f spot_attenuation=%.3f casts_shadow=%s position=%s direction=%s" % [
					int(light.get("index", 0)),
					String(light.get("type", "")),
					float(light.get("energy", 0.0)),
					str(light.get("color", Color.BLACK)),
					float(light.get("range", 0.0)),
					float(light.get("attenuation", 0.0)),
					float(light.get("spot_angle_degrees", 0.0)),
					float(light.get("spot_attenuation", 0.0)),
					_bool_text(bool(light.get("casts_shadow", false))),
					str(light.get("position", Vector3.ZERO)),
					str(light.get("direction", Vector3.ZERO)),
				]
			)

	print("Ray trace intersection time: %.3f ms" % intersection_ms)
