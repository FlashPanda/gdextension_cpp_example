@tool
extends EditorPlugin

const OUTPUT_PATH := ""
const SAMPLES_PER_PIXEL := 1
const MAX_DEPTH := 4
const SEED := 1
const NO_JOB_ID := 0

var _button: Button
var _active_job_id := NO_JOB_ID
var _cancel_requested := false


func _enter_tree() -> void:
	_button = Button.new()
	_button.text = "Trace PNG"
	_button.tooltip_text = "Render the current 3D editor view to a PNG."
	_button.pressed.connect(_on_trace_pressed)
	add_control_to_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)
	set_process(true)


func _exit_tree() -> void:
	if _active_job_id != NO_JOB_ID and ClassDB.class_exists(&"RayTraceExporter"):
		ClassDB.class_call_static(&"RayTraceExporter", &"cancel_render_job", _active_job_id)
		_active_job_id = NO_JOB_ID

	set_process(false)

	if _button != null:
		remove_control_from_container(EditorPlugin.CONTAINER_SPATIAL_EDITOR_MENU, _button)
		_button.queue_free()
		_button = null


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
	var error := String(result.get("error", "unknown error"))

	ClassDB.class_call_static(&"RayTraceExporter", &"release_render_job", job_id)
	_reset_job_state()

	var filesystem := get_editor_interface().get_resource_filesystem()
	if filesystem != null:
		filesystem.scan()

	if was_cancelled:
		print("Ray trace PNG export cancelled.")
		_print_timing_log_path(timing_log_path)
		return

	if not ok:
		push_error("Ray trace export failed: %s" % error)
		_print_timing_log_path(timing_log_path)
		return

	print("Ray trace PNG saved to %s" % path)
	_print_timing_log_path(timing_log_path)


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

	var result: Dictionary = ClassDB.class_call_static(
		&"RayTraceExporter",
		&"start_render_scene_to_png",
		root,
		camera,
		image_size,
		OUTPUT_PATH,
		SAMPLES_PER_PIXEL,
		MAX_DEPTH,
		SEED
	)

	if not result.get("ok", false):
		push_error("Ray trace export failed: %s" % result.get("error", "unknown error"))
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


func _update_button_progress(progress: float) -> void:
	if _button == null:
		return

	var percent := int(round(clamp(progress, 0.0, 1.0) * 100.0))
	_button.disabled = false
	if _cancel_requested:
		_button.text = "Cancelling %d%%" % percent
	else:
		_button.text = "Cancel Trace %d%%" % percent


func _print_timing_log_path(timing_log_path: String) -> void:
	if not timing_log_path.is_empty():
		print("Ray trace timing log saved to %s" % timing_log_path)
