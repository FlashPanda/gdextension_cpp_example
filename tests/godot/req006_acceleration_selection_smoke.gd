extends SceneTree

const IMAGE_SIZE := Vector2i(32, 32)
const OUTPUT_DIR := "res://req006_acceleration_selection_smoke"
const SEED := 20260824

var _scene_root: Node3D
var _camera: Camera3D
var _output_paths: Array[String] = []


func _init() -> void:
    call_deferred("_run")


func _run() -> void:
    if not ClassDB.class_exists(&"RayTraceExporter"):
        _fail("RayTraceExporter GDExtension was not loaded.")
        return

    _create_scene()
    await process_frame
    await process_frame

    var results: Array[Dictionary] = []
    for acceleration in ["brute_force", "bvh", "octree"]:
        var result := _render_sync(acceleration, "full", {})
        if result.is_empty():
            return
        results.append(result)

    var reference_image: Image = results[0]["image"]
    for index in range(1, results.size()):
        var image: Image = results[index]["image"]
        if image.get_data() != reference_image.get_data():
            _fail("%s output differs from brute_force." % results[index]["acceleration"])
            return
        if int(results[index].get("primary_ray_hit_count", -1)) != int(
            results[0].get("primary_ray_hit_count", -2)
        ):
            _fail("Acceleration structures disagree on primary-ray hit count.")
            return

    var bvh_timing_path := String(results[1].get("timing_log_path", ""))
    var timing_file := FileAccess.open(bvh_timing_path, FileAccess.READ)
    if timing_file == null or not timing_file.get_as_text().contains("acceleration: bvh"):
        _fail("Timing log did not record the selected BVH acceleration.")
        return

    var pixel_result := _render_sync(
        "bvh",
        "pixel",
        {"target_pixel": Vector2i(16, 16)}
    )
    if pixel_result.is_empty() or int(pixel_result.get("primary_ray_count", -1)) != 1:
        _fail("BVH pixel mode did not render exactly one primary ray.")
        return

    var tile_result := _render_sync(
        "octree",
        "tile",
        {"target_tile": Vector2i.ZERO}
    )
    if tile_result.is_empty() or int(tile_result.get("primary_ray_count", -1)) != 256:
        _fail("Octree tile mode did not render the selected tile.")
        return

    if not await _verify_async_octree():
        return
    if not _verify_invalid_acceleration():
        return

    print("REQ006_SMOKE PASS acceleration=brute_force,bvh,octree pixels=%d" % (IMAGE_SIZE.x * IMAGE_SIZE.y))
    _cleanup()
    quit(0)


func _create_scene() -> void:
    _scene_root = Node3D.new()
    _scene_root.name = "Req006SmokeRoot"
    get_root().add_child(_scene_root)

    _camera = Camera3D.new()
    _scene_root.add_child(_camera)
    _camera.position = Vector3(0.0, 0.0, 4.0)
    _camera.fov = 50.0
    _camera.look_at(Vector3.ZERO)
    _camera.current = true

    var box := MeshInstance3D.new()
    var mesh := BoxMesh.new()
    mesh.size = Vector3(2.0, 2.0, 1.0)
    box.mesh = mesh
    var material := StandardMaterial3D.new()
    material.albedo_color = Color(0.7, 0.5, 0.3, 1.0)
    material.roughness = 0.6
    box.material_override = material
    _scene_root.add_child(box)

    var light := OmniLight3D.new()
    light.position = Vector3(0.0, 1.0, 2.0)
    light.light_energy = 0.2
    light.omni_range = 10.0
    _scene_root.add_child(light)


func _render_sync(acceleration: String, render_mode: String, overrides: Dictionary) -> Dictionary:
    var label := "%s_%s" % [acceleration, render_mode]
    var output_path := "%s/%s.png" % [OUTPUT_DIR, label]
    var options := {
        "output_path": output_path,
        "samples_per_pixel": 1,
        "max_depth": 1,
        "seed": SEED,
        "render_mode": render_mode,
        "acceleration": acceleration,
    }
    options.merge(overrides, true)
    var value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"render_scene_to_png_with_options",
        _scene_root,
        _camera,
        IMAGE_SIZE,
        options
    )
    if not (value is Dictionary):
        _fail("Native exporter returned a non-Dictionary result for %s." % label)
        return {}

    var result: Dictionary = value
    if not bool(result.get("ok", false)):
        _fail("Render %s failed: %s" % [label, result.get("error", "unknown error")])
        return {}
    if String(result.get("acceleration", "")) != acceleration:
        _fail("Render %s did not report the selected acceleration." % label)
        return {}

    _track_paths(result)
    var image := Image.load_from_file(String(result.get("path", output_path)))
    if image == null:
        _fail("Could not load %s output image." % label)
        return {}
    result["image"] = image
    return result


func _verify_async_octree() -> bool:
    var output_path := "%s/async_octree.png" % OUTPUT_DIR
    var start_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"start_render_scene_to_png_with_options",
        _scene_root,
        _camera,
        Vector2i(8, 8),
        {
            "output_path": output_path,
            "samples_per_pixel": 1,
            "max_depth": 0,
            "seed": SEED,
            "render_mode": "full",
            "acceleration": "octree",
        }
    )
    if not (start_value is Dictionary) or not bool(start_value.get("ok", false)):
        _fail("Could not start async Octree render.")
        return false

    var job_id := int(start_value.get("job_id", 0))
    var final_status: Dictionary = {}
    for _poll_index in 600:
        var poll_value = ClassDB.class_call_static(&"RayTraceExporter", &"poll_render_job", job_id)
        if poll_value is Dictionary:
            final_status = poll_value
            if bool(final_status.get("done", false)):
                break
        await process_frame

    if not bool(final_status.get("done", false)) or not bool(final_status.get("ok", false)):
        _fail("Async Octree render did not complete successfully.")
        return false
    if String(final_status.get("acceleration", "")) != "octree":
        _fail("Async render did not preserve the selected Octree acceleration.")
        return false

    _track_paths(final_status)
    ClassDB.class_call_static(&"RayTraceExporter", &"release_render_job", job_id)
    return true


func _verify_invalid_acceleration() -> bool:
    var value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"render_scene_to_png_with_options",
        _scene_root,
        _camera,
        IMAGE_SIZE,
        {"acceleration": "grid"}
    )
    if not (value is Dictionary) or bool(value.get("ok", true)):
        _fail("Unknown acceleration values must fail validation.")
        return false
    if not String(value.get("error", "")).contains("acceleration"):
        _fail("Unknown acceleration error does not identify the invalid option.")
        return false
    return true


func _track_paths(result: Dictionary) -> void:
    for key in ["path", "primary_hit_mask_path", "timing_log_path", "tile_debug_path"]:
        var path := String(result.get(key, ""))
        if not path.is_empty():
            _output_paths.append(path)


func _cleanup() -> void:
    for path in _output_paths:
        var absolute_path := ProjectSettings.globalize_path(path)
        if FileAccess.file_exists(absolute_path):
            DirAccess.remove_absolute(absolute_path)
    if _scene_root != null:
        _scene_root.queue_free()


func _fail(message: String) -> void:
    push_error("REQ006_SMOKE FAIL: " + message)
    _cleanup()
    quit(1)
