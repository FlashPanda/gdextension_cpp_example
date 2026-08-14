extends SceneTree

const IMAGE_SIZE := Vector2i(32, 32)
const TILE_SIZE := 16
const SAMPLE_COUNT := 1
const MAX_DEPTH := 1
const SEED := 20260814

var _scene_root: Node3D
var _camera: Camera3D
var _output_paths: Array[String] = []


func _init() -> void:
    call_deferred("_run")


func _run() -> void:
    if not ClassDB.class_exists(&"RayTraceExporter"):
        _fail("RayTraceExporter GDExtension was not loaded.")
        return

    _create_lit_scene()
    await process_frame
    await process_frame

    var parallel_first := _render("parallel_first", IMAGE_SIZE, {"render_mode": "full"})
    if parallel_first.is_empty():
        return
    var parallel_repeat := _render("parallel_repeat", IMAGE_SIZE, {"render_mode": "full"})
    if parallel_repeat.is_empty():
        return

    var first_image: Image = parallel_first["image"]
    var repeat_image: Image = parallel_repeat["image"]
    if first_image.get_data() != repeat_image.get_data():
        _fail("Repeated full renders with the same seed are not byte-identical.")
        return

    var expected_primary_rays := IMAGE_SIZE.x * IMAGE_SIZE.y * SAMPLE_COUNT
    if int(parallel_first.get("primary_ray_count", -1)) != expected_primary_rays:
        _fail("Full render primary-ray statistics are incomplete.")
        return
    if int(parallel_first.get("primary_ray_hit_count", -1)) + int(
        parallel_first.get("primary_ray_miss_count", -1)
    ) != expected_primary_rays:
        _fail("Full render hit/miss statistics do not sum to the primary-ray count.")
        return

    var serial_primary_rays := 0
    for tile_y in 2:
        for tile_x in 2:
            var tile_coord := Vector2i(tile_x, tile_y)
            var tile_result := _render(
                "serial_tile_%d_%d" % [tile_x, tile_y],
                IMAGE_SIZE,
                {"render_mode": "tile", "target_tile": tile_coord}
            )
            if tile_result.is_empty():
                return
            serial_primary_rays += int(tile_result.get("primary_ray_count", -1))
            if not _tile_region_matches(first_image, tile_result["image"], tile_coord):
                _fail("Parallel full output differs from serial tile (%d, %d)." % [tile_x, tile_y])
                return

    if serial_primary_rays != expected_primary_rays:
        _fail("Serial tile statistics do not match the parallel full render.")
        return

    var single_tile_full := _render("single_tile_full", Vector2i(8, 8), {"render_mode": "full"})
    if single_tile_full.is_empty() or int(single_tile_full.get("primary_ray_count", -1)) != 64:
        _fail("A full render containing only one tile did not complete correctly.")
        return

    var partial_edge_full := _render("partial_edge_full", Vector2i(18, 17), {"render_mode": "full"})
    if partial_edge_full.is_empty() or int(partial_edge_full.get("primary_ray_count", -1)) != 18 * 17:
        _fail("Partial edge tiles did not cover every pixel exactly once.")
        return

    var single_pixel := _render(
        "single_pixel",
        IMAGE_SIZE,
        {"render_mode": "pixel", "target_pixel": Vector2i(7, 9)}
    )
    if single_pixel.is_empty() or int(single_pixel.get("primary_ray_count", -1)) != 1:
        _fail("Single-pixel diagnostic mode changed during parallel integration.")
        return

    var invalid_result = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"render_scene_to_png_with_options",
        _scene_root,
        _camera,
        Vector2i.ZERO,
        {"render_mode": "full"}
    )
    if not (invalid_result is Dictionary) or bool(invalid_result.get("ok", true)):
        _fail("Zero-sized images must still fail validation.")
        return

    if not await _verify_empty_scene():
        return

    var cancelled_tiles := await _verify_async_cancellation()
    if cancelled_tiles < 0:
        return

    print(
        "REQ002_SMOKE PASS hardware_threads=%d target_render_threads=%d " % [
            OS.get_processor_count(),
            maxi(OS.get_processor_count() - 1, 1),
        ] +
        "full_primary_rays=%d serial_primary_rays=%d cancelled_tiles=%d" % [
            expected_primary_rays,
            serial_primary_rays,
            cancelled_tiles,
        ]
    )
    _cleanup_output_files()
    _scene_root.queue_free()
    quit(0)


func _create_lit_scene() -> void:
    _scene_root = Node3D.new()
    _scene_root.name = "Req002SmokeRoot"
    get_root().add_child(_scene_root)

    var world_environment := WorldEnvironment.new()
    var environment := Environment.new()
    environment.tonemap_exposure = 1.0
    environment.tonemap_white = 4.0
    world_environment.environment = environment
    _scene_root.add_child(world_environment)

    _camera = Camera3D.new()
    _scene_root.add_child(_camera)
    _camera.position = Vector3(0.0, 0.0, 4.0)
    _camera.fov = 50.0
    _camera.look_at(Vector3.ZERO)
    _camera.current = true

    var box := MeshInstance3D.new()
    var box_mesh := BoxMesh.new()
    box_mesh.size = Vector3(2.0, 2.0, 1.0)
    box.mesh = box_mesh
    var material := StandardMaterial3D.new()
    material.albedo_color = Color(0.8, 0.7, 0.6, 1.0)
    material.metallic = 0.0
    material.roughness = 0.5
    box.material_override = material
    _scene_root.add_child(box)

    var light := OmniLight3D.new()
    light.position = Vector3(0.0, 0.0, 2.0)
    light.light_energy = 0.2
    light.omni_range = 10.0
    light.omni_attenuation = 1.0
    _scene_root.add_child(light)


func _render(label: String, image_size: Vector2i, overrides: Dictionary) -> Dictionary:
    var output_path := "res://req002_multithreaded_render_smoke/%s.png" % label
    var options := {
        "output_path": output_path,
        "samples_per_pixel": SAMPLE_COUNT,
        "max_depth": MAX_DEPTH,
        "seed": SEED,
        "render_mode": "full",
    }
    options.merge(overrides, true)

    var result_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"render_scene_to_png_with_options",
        _scene_root,
        _camera,
        image_size,
        options
    )
    if not (result_value is Dictionary):
        _fail("RayTraceExporter returned a non-Dictionary result for %s." % label)
        return {}

    var result: Dictionary = result_value
    if not bool(result.get("ok", false)):
        _fail("Render %s failed: %s" % [label, String(result.get("error", "unknown error"))])
        return {}

    _track_output_paths(result)
    var image := Image.load_from_file(String(result.get("path", output_path)))
    if image == null or image.get_width() != image_size.x or image.get_height() != image_size.y:
        _fail("Could not load output image for %s." % label)
        return {}
    result["image"] = image
    return result


func _tile_region_matches(full_image: Image, tile_image: Image, tile_coord: Vector2i) -> bool:
    var origin := tile_coord * TILE_SIZE
    var end := Vector2i(
        mini(origin.x + TILE_SIZE, full_image.get_width()),
        mini(origin.y + TILE_SIZE, full_image.get_height())
    )
    for y in range(origin.y, end.y):
        for x in range(origin.x, end.x):
            if full_image.get_pixel(x, y) != tile_image.get_pixel(x, y):
                return false
    return true


func _verify_async_cancellation() -> int:
    var output_path := "res://req002_multithreaded_render_smoke/cancelled.png"
    var start_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"start_render_scene_to_png_with_options",
        _scene_root,
        _camera,
        Vector2i(256, 256),
        {
            "output_path": output_path,
            "samples_per_pixel": 256,
            "max_depth": 0,
            "seed": SEED,
            "render_mode": "full",
        }
    )
    if not (start_value is Dictionary) or not bool(start_value.get("ok", false)):
        _fail("Could not start asynchronous cancellation render.")
        return -1

    var start_result: Dictionary = start_value
    _track_output_paths(start_result)
    var job_id := int(start_result.get("job_id", 0))
    if job_id <= 0:
        _fail("Asynchronous render did not return a valid job id.")
        return -1

    ClassDB.class_call_static(&"RayTraceExporter", &"cancel_render_job", job_id)

    var final_status: Dictionary = {}
    for _poll_index in 600:
        var poll_value = ClassDB.class_call_static(&"RayTraceExporter", &"poll_render_job", job_id)
        if poll_value is Dictionary:
            final_status = poll_value
            if bool(final_status.get("done", false)):
                break
        await process_frame

    if final_status.is_empty() or not bool(final_status.get("done", false)):
        _fail("Cancelled asynchronous render did not finish within the polling limit.")
        return -1
    if not bool(final_status.get("cancelled", false)) or bool(final_status.get("ok", true)):
        _fail("Cancelled asynchronous render published a successful result.")
        return -1
    if float(final_status.get("progress", 1.0)) >= 1.0:
        _fail("Cancellation did not stop the renderer before all tiles completed.")
        return -1

    var release_value = ClassDB.class_call_static(&"RayTraceExporter", &"release_render_job", job_id)
    if not (release_value is Dictionary) or not bool(release_value.get("ok", false)) or not bool(
        release_value.get("released", false)
    ):
        _fail("Cancelled render job was not released cleanly.")
        return -1
    var released_poll = ClassDB.class_call_static(&"RayTraceExporter", &"poll_render_job", job_id)
    if not (released_poll is Dictionary) or bool(released_poll.get("exists", true)):
        _fail("Released render job is still present in the job table.")
        return -1

    return int(round(float(final_status.get("progress", 0.0)) * 256.0))


func _verify_empty_scene() -> bool:
    var empty_root := Node3D.new()
    empty_root.name = "Req002EmptyRoot"
    get_root().add_child(empty_root)
    var empty_camera := Camera3D.new()
    empty_root.add_child(empty_camera)
    empty_camera.position = Vector3(0.0, 0.0, 4.0)
    empty_camera.look_at(Vector3.ZERO)
    empty_camera.current = true
    await process_frame

    var output_path := "res://req002_multithreaded_render_smoke/empty_scene.png"
    var result_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"render_scene_to_png_with_options",
        empty_root,
        empty_camera,
        Vector2i(8, 8),
        {
            "output_path": output_path,
            "samples_per_pixel": 1,
            "max_depth": 1,
            "seed": SEED,
            "render_mode": "full",
        }
    )
    if not (result_value is Dictionary) or not bool(result_value.get("ok", false)):
        empty_root.queue_free()
        _fail("Empty-scene full render failed.")
        return false

    var result: Dictionary = result_value
    _track_output_paths(result)
    if int(result.get("primary_ray_count", -1)) != 64 or int(
        result.get("primary_ray_miss_count", -1)
    ) != 64:
        empty_root.queue_free()
        _fail("Empty-scene statistics did not report one miss per pixel.")
        return false

    var image := Image.load_from_file(String(result.get("path", output_path)))
    if image == null:
        empty_root.queue_free()
        _fail("Empty-scene PNG could not be loaded.")
        return false
    for y in image.get_height():
        for x in image.get_width():
            if image.get_pixel(x, y) != Color(0.0, 0.0, 0.0, 1.0):
                empty_root.queue_free()
                _fail("Empty-scene output contains non-black radiance.")
                return false

    empty_root.queue_free()
    return true


func _track_output_paths(result: Dictionary) -> void:
    for key in ["path", "primary_hit_mask_path", "timing_log_path", "tile_debug_path"]:
        var path := String(result.get(key, ""))
        if not path.is_empty():
            _output_paths.append(path)


func _cleanup_output_files() -> void:
    for path in _output_paths:
        var absolute_path := ProjectSettings.globalize_path(path)
        if FileAccess.file_exists(absolute_path):
            DirAccess.remove_absolute(absolute_path)


func _fail(message: String) -> void:
    push_error("REQ002_SMOKE FAIL: " + message)
    _cleanup_output_files()
    if _scene_root != null:
        _scene_root.queue_free()
    quit(1)
