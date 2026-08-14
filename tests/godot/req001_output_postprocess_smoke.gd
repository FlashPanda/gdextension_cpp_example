extends SceneTree

const IMAGE_SIZE := Vector2i(8, 8)
const SAMPLE_COUNT := 1
const MAX_DEPTH := 1
const SEED := 20260806
const MEAN_EPSILON := 0.001

var _scene_root: Node3D
var _camera: Camera3D
var _world_environment: WorldEnvironment
var _world_tonemap: Environment
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

    var active_world := _camera.get_viewport().find_world_3d()
    if active_world == null or active_world.environment != _world_tonemap:
        _fail("WorldEnvironment did not become the active World3D environment.")
        return

    _world_tonemap.tonemap_exposure = 1.0
    _world_tonemap.tonemap_white = 4.0
    await process_frame
    var world_exposure_one := _render_mean("world_exposure_1")
    if world_exposure_one < MEAN_EPSILON:
        _fail("The WorldEnvironment smoke scene produced no visible radiance.")
        return

    _world_tonemap.tonemap_exposure = 2.0
    await process_frame
    var world_exposure_two := _render_mean("world_exposure_2")
    if world_exposure_two <= world_exposure_one + MEAN_EPSILON:
        _fail(
            "WorldEnvironment exposure did not brighten the exported PNG: %.6f -> %.6f" % [
                world_exposure_one,
                world_exposure_two,
            ]
        )
        return

    var camera_override := Environment.new()
    camera_override.tonemap_exposure = 0.0
    camera_override.tonemap_white = 4.0
    _camera.environment = camera_override
    await process_frame
    var camera_override_mean := _render_mean("camera_override_exposure_0")
    if camera_override_mean > MEAN_EPSILON:
        _fail(
            "Camera Environment override did not take precedence over WorldEnvironment: %.6f" %
            camera_override_mean
        )
        return

    _camera.environment = null
    _world_environment.environment = null
    await process_frame
    await process_frame
    var no_environment_mean := _render_mean("no_environment_defaults")
    if no_environment_mean < MEAN_EPSILON:
        _fail("No-Environment fallback produced no visible radiance.")
        return

    var default_environment := Environment.new()
    _world_environment.environment = default_environment
    await process_frame
    await process_frame
    var default_environment_mean := _render_mean("world_default_environment")
    if absf(no_environment_mean - default_environment_mean) > MEAN_EPSILON:
        _fail(
            "No-Environment fallback differs from Godot default Environment: %.6f vs %.6f" % [
                no_environment_mean,
                default_environment_mean,
            ]
        )
        return

    # Tile debug is a diagnostic artifact. REQ-001 must not apply display exposure,
    # Reinhard, or sRGB encoding to its old linear-clamped base image.
    _world_tonemap = Environment.new()
    _world_tonemap.tonemap_white = 4.0
    _world_environment.environment = _world_tonemap
    _world_tonemap.tonemap_exposure = 0.0
    await process_frame
    var tile_debug_exposure_zero := _render_tile_debug_mean("tile_debug_exposure_0")
    _world_tonemap.tonemap_exposure = 2.0
    await process_frame
    var tile_debug_exposure_two := _render_tile_debug_mean("tile_debug_exposure_2")
    if absf(tile_debug_exposure_zero - tile_debug_exposure_two) > MEAN_EPSILON:
        _fail(
            "Tile debug base changed with output exposure: %.6f -> %.6f" % [
                tile_debug_exposure_zero,
                tile_debug_exposure_two,
            ]
        )
        return

    print(
        "REQ001_SMOKE PASS " +
        "world_exposure_1=%.6f world_exposure_2=%.6f camera_override_0=%.6f " % [
            world_exposure_one,
            world_exposure_two,
            camera_override_mean,
        ] +
        "no_environment=%.6f default_environment=%.6f" % [
            no_environment_mean,
            default_environment_mean,
        ]
    )
    _cleanup_output_files()
    _scene_root.queue_free()
    quit(0)


func _create_lit_scene() -> void:
    _scene_root = Node3D.new()
    _scene_root.name = "Req001SmokeRoot"
    get_root().add_child(_scene_root)

    _world_tonemap = Environment.new()
    _world_tonemap.tonemap_exposure = 1.0
    _world_tonemap.tonemap_white = 4.0
    _world_environment = WorldEnvironment.new()
    _world_environment.environment = _world_tonemap
    _scene_root.add_child(_world_environment)

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
    material.albedo_color = Color(0.8, 0.8, 0.8, 1.0)
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


func _render_mean(label: String) -> float:
    var output_path := "res://req001_output_postprocess_smoke/%s.png" % label
    var options := {
        "output_path": output_path,
        "samples_per_pixel": SAMPLE_COUNT,
        "max_depth": MAX_DEPTH,
        "seed": SEED,
        "render_mode": "full",
    }
    var result_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"render_scene_to_png_with_options",
        _scene_root,
        _camera,
        IMAGE_SIZE,
        options
    )
    if not (result_value is Dictionary):
        _fail("RayTraceExporter returned a non-Dictionary result.")
        return -1.0

    var result: Dictionary = result_value
    if not bool(result.get("ok", false)):
        _fail("RayTraceExporter failed: %s" % String(result.get("error", "unknown error")))
        return -1.0

    _track_output_paths(result)
    var image := Image.load_from_file(String(result.get("path", output_path)))
    if image == null or image.get_width() != IMAGE_SIZE.x or image.get_height() != IMAGE_SIZE.y:
        _fail("Could not read the exported smoke PNG for %s." % label)
        return -1.0

    var sum := 0.0
    for y in image.get_height():
        for x in image.get_width():
            var color := image.get_pixel(x, y)
            sum += (color.r + color.g + color.b) / 3.0
    return sum / float(image.get_width() * image.get_height())


func _render_tile_debug_mean(label: String) -> float:
    var output_path := "res://req001_output_postprocess_smoke/%s.png" % label
    var options := {
        "output_path": output_path,
        "samples_per_pixel": SAMPLE_COUNT,
        "max_depth": MAX_DEPTH,
        "seed": SEED,
        "render_mode": "tile",
        "target_tile": Vector2i(0, 0),
    }
    var result_value = ClassDB.class_call_static(
        &"RayTraceExporter",
        &"render_scene_to_png_with_options",
        _scene_root,
        _camera,
        Vector2i(32, 32),
        options
    )
    if not (result_value is Dictionary):
        _fail("RayTraceExporter returned a non-Dictionary tile result.")
        return -1.0

    var result: Dictionary = result_value
    if not bool(result.get("ok", false)):
        _fail("RayTraceExporter tile render failed: %s" % String(result.get("error", "unknown error")))
        return -1.0

    _track_output_paths(result)
    var debug_path := output_path.get_basename() + "_tile_0_0_debug.png"
    _output_paths.append(debug_path)
    var image := Image.load_from_file(debug_path)
    if image == null or image.get_width() != 32 or image.get_height() != 32:
        _fail("Could not read the tile debug PNG for %s." % label)
        return -1.0

    var sum := 0.0
    for y in image.get_height():
        for x in image.get_width():
            var color := image.get_pixel(x, y)
            sum += (color.r + color.g + color.b) / 3.0
    return sum / float(image.get_width() * image.get_height())


func _track_output_paths(result: Dictionary) -> void:
    for key in ["path", "primary_hit_mask_path", "timing_log_path"]:
        var path := String(result.get(key, ""))
        if not path.is_empty():
            _output_paths.append(path)


func _cleanup_output_files() -> void:
    for path in _output_paths:
        var absolute_path := ProjectSettings.globalize_path(path)
        if FileAccess.file_exists(absolute_path):
            DirAccess.remove_absolute(absolute_path)


func _fail(message: String) -> void:
    push_error("REQ001_SMOKE FAIL: " + message)
    _cleanup_output_files()
    if _scene_root != null:
        _scene_root.queue_free()
    quit(1)
