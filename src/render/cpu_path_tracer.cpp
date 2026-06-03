#include "cpu_path_tracer.h"

#include <algorithm>
#include <cstdint>

namespace godot_rt {
namespace {

    godot::Color black() {
        return godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    std::uint64_t mix_u64(std::uint64_t value) {
        value += 0x9e3779b97f4a7c15ull;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31);
    }

    std::uint64_t sample_seed(const godot::Vector2i& pixel, int pass_index, int sample_index,
                              std::uint64_t base_seed) {
        std::uint64_t seed = mix_u64(base_seed);
        seed ^= mix_u64(static_cast<std::uint32_t>(pixel.x));
        seed ^= mix_u64(static_cast<std::uint32_t>(pixel.y) << 1);
        seed ^= mix_u64(static_cast<std::uint32_t>(pass_index) << 2);
        seed ^= mix_u64(static_cast<std::uint32_t>(sample_index) << 3);
        return mix_u64(seed);
    }

}

void CpuPathTracer::reset(const Scene& new_scene, const Camera& new_camera, CpuPathTracerSettings new_settings) {
    new_settings.image_size.x = std::max(new_settings.image_size.x, 0);
    new_settings.image_size.y = std::max(new_settings.image_size.y, 0);
    new_settings.tile_size.x = std::max(new_settings.tile_size.x, 1);
    new_settings.tile_size.y = std::max(new_settings.tile_size.y, 1);
    new_settings.samples_per_pixel = std::max(new_settings.samples_per_pixel, 1);
    new_settings.max_depth = std::max(new_settings.max_depth, 0);

    scene = new_scene;
    camera = new_camera;
    settings = new_settings;

    film.resize(settings.image_size);
    frame_accumulator.resize(settings.image_size, settings.tile_size);

    if (accel) {
        accel->build(scene);
    }
    rebuild_integrator();
}

void CpuPathTracer::set_accel(std::unique_ptr<AccelInterface> new_accel) {
    accel = std::move(new_accel);
    if (accel) {
        accel->build(scene);
    }
    rebuild_integrator();
}

bool CpuPathTracer::render_next_tile(Tile* out_tile) {
    Tile tile;
    if (!frame_accumulator.next_tile(&tile)) {
        return false;
    }

    render_tile(tile);
    if (out_tile != nullptr) {
        *out_tile = tile;
    }
    return true;
}

void CpuPathTracer::render_tile(const Tile& tile) {
    const int x_begin = std::max(tile.origin.x, 0);
    const int y_begin = std::max(tile.origin.y, 0);
    const int x_end = std::min(tile.origin.x + tile.size.x, settings.image_size.x);
    const int y_end = std::min(tile.origin.y + tile.size.y, settings.image_size.y);

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const godot::Vector2i pixel(x, y);
            for (int sample_index = 0; sample_index < settings.samples_per_pixel; ++sample_index) {
                Rng rng(sample_seed(pixel, tile.pass_index, sample_index, settings.seed));
                const godot::Vector2 jitter = rng.next_2d();
                const godot::Vector2 pixel_sample(
                    static_cast<float>(x) + jitter.x,
                    static_cast<float>(y) + jitter.y
                );

                const RayDifferential ray = camera.generate_primary_ray_differential(pixel_sample, settings.image_size);
                film.add_sample(pixel, trace_path(ray, rng));
            }
        }
    }
}

godot::Color CpuPathTracer::trace_path(const Ray& ray, Rng& rng) const {
    return trace_path(RayDifferential(ray), rng);
}

godot::Color CpuPathTracer::trace_path(const RayDifferential& ray, Rng& rng) const {
    if (!integrator) {
        return black();
    }

    return integrator->trace(ray, rng);
}

Film& CpuPathTracer::get_film() {
    return film;
}

const Film& CpuPathTracer::get_film() const {
    return film;
}

const Scene& CpuPathTracer::get_scene() const {
    return scene;
}

const Camera& CpuPathTracer::get_camera() const {
    return camera;
}

const CpuPathTracerSettings& CpuPathTracer::get_settings() const {
    return settings;
}

void CpuPathTracer::rebuild_integrator() {
    integrator = Integrator::create("random_walk", &scene, accel.get(), settings.max_depth);
}

}
