#include "raytrace_exporter.h"

#include <algorithm>
#include <memory>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/color.hpp>

#include "accel/brute_force_accel.h"
#include "render/cpu_path_tracer.h"
#include "render/film.h"
#include "render/frame_accumulator.h"
#include "scene/scene_extractor.h"

using namespace godot;

namespace {

    constexpr int TILE_SIZE = 16;
    constexpr const char DEFAULT_OUTPUT_PATH[] = "res://raytrace_output/current_scene.png";

    Dictionary make_result(bool ok,
                           const String& path,
                           const String& error,
                           const Vector2i& image_size,
                           int32_t samples_per_pixel) {
        Dictionary result;
        result["ok"] = ok;
        result["path"] = path;
        result["error"] = error;
        result["width"] = image_size.x;
        result["height"] = image_size.y;
        result["samples_per_pixel"] = samples_per_pixel;
        return result;
    }

    float clamp01(float value) {
        return std::max(0.0f, std::min(value, 1.0f));
    }

    godot_rt::Camera make_render_camera(const Camera3D* camera) {
        godot_rt::Camera render_camera;
        render_camera.camera_to_world = camera->get_camera_transform();
        render_camera.fov_y_radians = Math::deg_to_rad(camera->get_fov());
        render_camera.fov_axis = camera->get_keep_aspect_mode() == Camera3D::KEEP_WIDTH
                                     ? godot_rt::CameraFovAxis::Horizontal
                                     : godot_rt::CameraFovAxis::Vertical;
        return render_camera;
    }

    bool ensure_output_dir(const String& output_path, String* error) {
        const String output_dir = output_path.get_base_dir();
        if (output_dir.is_empty()) {
            return true;
        }

        const Error mkdir_error = DirAccess::make_dir_recursive_absolute(output_dir);
        if (mkdir_error != OK && mkdir_error != ERR_ALREADY_EXISTS) {
            if (error != nullptr) {
                *error = "Could not create output directory: " + output_dir;
            }
            return false;
        }

        return true;
    }

    Ref<Image> film_to_image(const godot_rt::Film& film, const Vector2i& image_size) {
        Ref<Image> image = Image::create_empty(image_size.x, image_size.y, false, Image::FORMAT_RGBA8);
        if (image.is_null()) {
            return image;
        }

        for (int y = 0; y < image_size.y; ++y) {
            for (int x = 0; x < image_size.x; ++x) {
                const Color radiance = film.get_average(Vector2i(x, y));
                image->set_pixel(
                    x,
                    y,
                    Color(clamp01(radiance.r), clamp01(radiance.g), clamp01(radiance.b), 1.0f)
                );
            }
        }

        return image;
    }

    void render_one_pass(godot_rt::CpuPathTracer& tracer, const Vector2i& image_size) {
        for (int y = 0; y < image_size.y; y += TILE_SIZE) {
            for (int x = 0; x < image_size.x; x += TILE_SIZE) {
                godot_rt::Tile tile;
                tile.origin = Vector2i(x, y);
                tile.size = Vector2i(
                    std::min(TILE_SIZE, image_size.x - x),
                    std::min(TILE_SIZE, image_size.y - y)
                );
                tile.pass_index = 0;
                tracer.render_tile(tile);
            }
        }
    }

} // namespace

void RayTraceExporter::_bind_methods() {
    ClassDB::bind_static_method(
        "RayTraceExporter",
        D_METHOD("render_scene_to_png", "root", "camera", "image_size", "output_path",
                 "samples_per_pixel", "max_depth", "seed"),
        &RayTraceExporter::render_scene_to_png,
        DEFVAL(String(DEFAULT_OUTPUT_PATH)),
        DEFVAL(16),
        DEFVAL(4),
        DEFVAL(1)
    );
}

Dictionary RayTraceExporter::render_scene_to_png(Node* root,
                                                 Camera3D* camera,
                                                 Vector2i image_size,
                                                 const String& output_path,
                                                 int32_t samples_per_pixel,
                                                 int32_t max_depth,
                                                 int64_t seed) {
    const int32_t normalized_spp = std::max(samples_per_pixel, 1);
    const String save_path = output_path.is_empty() ? String(DEFAULT_OUTPUT_PATH) : output_path;

    if (root == nullptr) {
        return make_result(false, save_path, "No edited scene root.", image_size, normalized_spp);
    }
    if (camera == nullptr) {
        return make_result(false, save_path, "No editor viewport camera.", image_size, normalized_spp);
    }
    if (camera->get_projection() != Camera3D::PROJECTION_PERSPECTIVE) {
        return make_result(false, save_path, "Only perspective editor cameras are supported.", image_size, normalized_spp);
    }
    if (image_size.x <= 0 || image_size.y <= 0) {
        return make_result(false, save_path, "Invalid viewport image size.", image_size, normalized_spp);
    }

    String error;
    if (!ensure_output_dir(save_path, &error)) {
        return make_result(false, save_path, error, image_size, normalized_spp);
    }

    godot_rt::SceneExtractor extractor;
    const godot_rt::Scene scene = extractor.extract(root);
    const godot_rt::Camera render_camera = make_render_camera(camera);

    godot_rt::CpuPathTracerSettings settings;
    settings.image_size = image_size;
    settings.tile_size = Vector2i(TILE_SIZE, TILE_SIZE);
    settings.samples_per_pixel = normalized_spp;
    settings.max_depth = std::max(max_depth, 0);
    settings.seed = static_cast<std::uint64_t>(std::max<int64_t>(seed, 0));

    godot_rt::CpuPathTracer tracer;
    tracer.reset(scene, render_camera, settings);
    tracer.set_accel(std::make_unique<godot_rt::BruteForceAccel>());
    render_one_pass(tracer, image_size);

    Ref<Image> image = film_to_image(tracer.get_film(), image_size);
    if (image.is_null()) {
        return make_result(false, save_path, "Could not create output image.", image_size, normalized_spp);
    }

    const Error save_error = image->save_png(save_path);
    if (save_error != OK) {
        return make_result(false, save_path, "Could not save PNG.", image_size, normalized_spp);
    }

    return make_result(true, save_path, String(), image_size, normalized_spp);
}
