#include "cpu_path_tracer.h"

#include <algorithm>
#include <cstdint>
#include <utility>

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
    render_statistics = RenderStatistics();

    film.resize(settings.image_size);
    frame_accumulator.resize(settings.image_size, settings.tile_size);
    const std::size_t pixel_count = static_cast<std::size_t>(settings.image_size.x) *
                                    static_cast<std::size_t>(settings.image_size.y);
    primary_hit_sample_count.assign(pixel_count, 0);
    primary_miss_sample_count.assign(pixel_count, 0);

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

void CpuPathTracer::set_log_sink(TraceLogSink new_log_sink) {
    log_sink = std::move(new_log_sink);
    if (integrator) {
        integrator->set_log_sink(log_sink);
    }
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

    // 并行调用必须覆盖互不重叠的像素，使 Film 和命中/未命中计数始终由当前 tile 线程独占写入。
    // 统计先在 tile 局部累计，完成后只加锁归并一次，避免在每条光线热路径上争用全局锁。
    RenderStatistics tile_statistics;
    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const godot::Vector2i pixel(x, y);
            for (int sample_index = 0; sample_index < settings.samples_per_pixel; ++sample_index) {
                render_sample(pixel, tile.pass_index, sample_index, &tile_statistics);
            }
        }
    }
    merge_statistics(tile_statistics);
}

bool CpuPathTracer::render_pixel(godot::Vector2i pixel, int pass_index, int sample_index) {
    if (!contains_pixel(pixel)) {
        return false;
    }

    RenderStatistics pixel_statistics;
    render_sample(pixel, std::max(pass_index, 0), std::max(sample_index, 0), &pixel_statistics);
    merge_statistics(pixel_statistics);
    return true;
}

godot::Color CpuPathTracer::trace_path(const Ray& ray, Rng& rng) const {
    return trace_path(RayDifferential(ray), rng);
}

godot::Color CpuPathTracer::trace_path(const RayDifferential& ray, Rng& rng) const {
    return trace_sample(ray, rng).radiance;
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

const RenderStatistics& CpuPathTracer::get_statistics() const {
    return render_statistics;
}

int CpuPathTracer::get_primary_hit_sample_count(godot::Vector2i pixel) const {
    if (!contains_pixel(pixel)) {
        return 0;
    }

    return primary_hit_sample_count[pixel_index(pixel)];
}

int CpuPathTracer::get_primary_miss_sample_count(godot::Vector2i pixel) const {
    if (!contains_pixel(pixel)) {
        return 0;
    }

    return primary_miss_sample_count[pixel_index(pixel)];
}

void CpuPathTracer::rebuild_integrator() {
    integrator = Integrator::create("random_walk", &scene, accel.get(), settings.max_depth);
    if (integrator) {
        integrator->set_log_sink(log_sink);
    }
}

TraceResult CpuPathTracer::trace_sample(
    const RayDifferential& ray,
    Rng& rng,
    RenderStatistics* statistics
) const {
    if (!integrator) {
        TraceResult result;
        result.radiance = black();
        return result;
    }

    return integrator->trace(ray, rng, statistics);
}

void CpuPathTracer::merge_statistics(const RenderStatistics& statistics) {
    // 这里只同步跨 tile 共享的总统计；Film 和逐像素计数依靠像素独占约束，无需全局锁。
    std::lock_guard<std::mutex> lock(render_statistics_mutex);
    merge_render_statistics(&render_statistics, statistics);
}

void CpuPathTracer::record_primary_hit(godot::Vector2i pixel, bool hit) {
    if (!contains_pixel(pixel)) {
        return;
    }

    const std::size_t index = pixel_index(pixel);
    if (hit) {
        ++primary_hit_sample_count[index];
    } else {
        ++primary_miss_sample_count[index];
    }
}

bool CpuPathTracer::contains_pixel(godot::Vector2i pixel) const {
    return pixel.x >= 0 && pixel.y >= 0 && pixel.x < settings.image_size.x && pixel.y < settings.image_size.y;
}

std::size_t CpuPathTracer::pixel_index(godot::Vector2i pixel) const {
    return static_cast<std::size_t>(pixel.y) * static_cast<std::size_t>(settings.image_size.x) +
           static_cast<std::size_t>(pixel.x);
}

void CpuPathTracer::render_sample(
    godot::Vector2i pixel,
    int pass_index,
    int sample_index,
    RenderStatistics* statistics
) {
    Rng rng(sample_seed(pixel, pass_index, sample_index, settings.seed));
    const godot::Vector2 jitter = rng.next_2d();
    const godot::Vector2 pixel_sample(
        static_cast<float>(pixel.x) + jitter.x,
        static_cast<float>(pixel.y) + jitter.y
    );

    const RayDifferential ray = camera.generate_primary_ray_differential(pixel_sample, settings.image_size);
    const TraceResult result = trace_sample(ray, rng, statistics);
    if (result.primary_ray_tested) {
        record_primary_hit(pixel, result.primary_ray_hit);
    }
    film.add_sample(pixel, result.radiance);
}

}
