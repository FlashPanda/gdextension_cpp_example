extends SceneTree

const OUTPUT_PATH := "res://req005_async_main_thread_finalize_smoke/full.png"
const CANCEL_OUTPUT_PATH := "res://req005_async_main_thread_finalize_smoke/cancel_after_compute.png"
const IMAGE_SIZE := Vector2i(8, 8)

var _scene_root: Node3D
var _camera: Camera3D
var _output_paths: Array[String] = []


func _init() -> void:
    call_deferred("_run")


func _run() -> void:
    if not ClassDB.class_exists(&"RayTraceExporter"):
        _fail("RayTraceExporter GDExtension was not loaded.")
        return

    _cleanup_known_outputs()
    _create_empty_scene()
    await process_frame

    var start_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"start_render_scene_to_png_with_options",
        _scene_root,
        _camera,
        IMAGE_SIZE,
        {
            "output_path": OUTPUT_PATH,
            "samples_per_pixel": 1,
            "max_depth": 0,
            "seed": 20260814,
            "render_mode": "full",
        }
    )
    if not (start_value is Dictionary) or not bool(start_value.get("ok", false)):
        _fail("Could not start asynchronous render.")
        return

    var start_result: Dictionary = start_value
    _track_output_paths(start_result)
    var job_id := int(start_result.get("job_id", 0))
    if job_id <= 0:
        _fail("Asynchronous render did not return a valid job id.")
        return

    # worker 即使已经完成计算，也必须等待提交线程 poll 后才能创建 Godot Object 或写出文件。
    await create_timer(1.0).timeout
    for path in _output_paths:
        if FileAccess.file_exists(path):
            _fail("Asynchronous worker published a Godot-owned artifact before the first poll: " + path)
            return

    var final_status: Dictionary = {}
    for _poll_index in 600:
        var poll_value = ClassDB.class_call_static(&"RayTraceExporter", &"poll_render_job", job_id)
        if poll_value is Dictionary:
            final_status = poll_value
            if bool(final_status.get("done", false)):
                break
        await process_frame

    if final_status.is_empty() or not bool(final_status.get("done", false)):
        _fail("Main-thread polling did not finalize the asynchronous render.")
        return
    if not bool(final_status.get("ok", false)) or bool(final_status.get("cancelled", true)):
        _fail("Finalized asynchronous render failed: " + String(final_status.get("error", "unknown error")))
        return

    _track_output_paths(final_status)
    for key in ["path", "primary_hit_mask_path", "timing_log_path"]:
        var path := String(final_status.get(key, ""))
        if path.is_empty() or not FileAccess.file_exists(path):
            _fail("Main-thread finalization did not publish expected artifact: " + key)
            return

    var release_value = ClassDB.class_call_static(&"RayTraceExporter", &"release_render_job", job_id)
    if not (release_value is Dictionary) or not bool(release_value.get("released", false)):
        _fail("Finalized asynchronous job was not released cleanly.")
        return

    if not await _verify_cancel_after_compute():
        return

    print("REQ005_SMOKE PASS artifacts_before_poll=0 finalized_on_poll=1 cancelled_artifacts=0")
    _cleanup_output_files()
    _scene_root.queue_free()
    quit(0)


func _create_empty_scene() -> void:
    _scene_root = Node3D.new()
    _scene_root.name = "Req005SmokeRoot"
    get_root().add_child(_scene_root)

    _camera = Camera3D.new()
    _scene_root.add_child(_camera)
    _camera.position = Vector3(0.0, 0.0, 4.0)
    _camera.look_at(Vector3.ZERO)
    _camera.current = true


func _verify_cancel_after_compute() -> bool:
    var start_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"start_render_scene_to_png_with_options",
        _scene_root,
        _camera,
        IMAGE_SIZE,
        {
            "output_path": CANCEL_OUTPUT_PATH,
            "samples_per_pixel": 1,
            "max_depth": 0,
            "seed": 20260814,
            "render_mode": "full",
        }
    )
    if not (start_value is Dictionary) or not bool(start_value.get("ok", false)):
        _fail("Could not start cancellation-boundary render.")
        return false

    var start_result: Dictionary = start_value
    _track_output_paths(start_result)
    var job_id := int(start_result.get("job_id", 0))
    if job_id <= 0:
        _fail("Cancellation-boundary render did not return a valid job id.")
        return false

    # 留出足够时间让 worker 完成纯计算，但在取消前不进行任何 poll。
    await create_timer(1.0).timeout
    var final_status = ClassDB.class_call_static(&"RayTraceExporter", &"cancel_render_job", job_id)
    for _poll_index in 600:
        if final_status is Dictionary and bool(final_status.get("done", false)):
            break
        final_status = ClassDB.class_call_static(&"RayTraceExporter", &"poll_render_job", job_id)
        await process_frame

    if not (final_status is Dictionary) or not bool(final_status.get("done", false)):
        _fail("Cancellation-boundary render did not finish.")
        return false
    if not bool(final_status.get("cancelled", false)) or bool(final_status.get("ok", true)):
        _fail("Cancellation between compute and finalization published a successful result.")
        return false

    for key in ["path", "primary_hit_mask_path", "tile_debug_path"]:
        var path := String(final_status.get(key, ""))
        if not path.is_empty() and FileAccess.file_exists(path):
            _fail("Cancellation between compute and finalization published an image artifact: " + path)
            return false

    var release_value = ClassDB.class_call_static(&"RayTraceExporter", &"release_render_job", job_id)
    if not (release_value is Dictionary) or not bool(release_value.get("released", false)):
        _fail("Cancelled asynchronous job was not released cleanly.")
        return false
    return true


func _track_output_paths(result: Dictionary) -> void:
    for key in ["path", "primary_hit_mask_path", "timing_log_path", "tile_debug_path"]:
        var path := String(result.get(key, ""))
        if not path.is_empty() and not _output_paths.has(path):
            _output_paths.append(path)


func _cleanup_known_outputs() -> void:
    var primary_hit_mask_path := OUTPUT_PATH.trim_suffix(".png") + "_primary_hit_mask.png"
    var timing_log_path := OUTPUT_PATH.trim_suffix(".png") + ".timings.log"
    var cancelled_primary_hit_mask_path := CANCEL_OUTPUT_PATH.trim_suffix(".png") + "_primary_hit_mask.png"
    var cancelled_timing_log_path := CANCEL_OUTPUT_PATH.trim_suffix(".png") + ".timings.log"
    _output_paths = [
        OUTPUT_PATH,
        primary_hit_mask_path,
        timing_log_path,
        CANCEL_OUTPUT_PATH,
        cancelled_primary_hit_mask_path,
        cancelled_timing_log_path,
    ]
    _cleanup_output_files()
    _output_paths.clear()


func _cleanup_output_files() -> void:
    for path in _output_paths:
        var absolute_path := ProjectSettings.globalize_path(path)
        if FileAccess.file_exists(absolute_path):
            DirAccess.remove_absolute(absolute_path)


func _fail(message: String) -> void:
    push_error("REQ005_SMOKE FAIL: " + message)
    _cleanup_output_files()
    if _scene_root != null:
        _scene_root.queue_free()
    quit(1)
