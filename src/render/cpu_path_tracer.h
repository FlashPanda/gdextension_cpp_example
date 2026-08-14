#ifndef GDEXTENSION_CPP_EXAMPLE_CPU_PATH_TRACER_H
#define GDEXTENSION_CPP_EXAMPLE_CPU_PATH_TRACER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "../core/rt_random.h"
#include "../core/rt_types.h"
#include "../scene/rt_scene.h"
#include "../accel/accel_interface.h"
#include "film.h"
#include "frame_accumulator.h"
#include "integrators.h"

namespace godot_rt {

    struct CpuPathTracerSettings {
        godot::Vector2i image_size = godot::Vector2i(0, 0);
        godot::Vector2i tile_size = godot::Vector2i(16, 16);
        int samples_per_pixel = 1;
        int max_depth = 4;
        std::uint64_t seed = 1;
    };

    class CpuPathTracer {
    public:
        CpuPathTracer() = default;

        void reset(const Scene& scene, const Camera& camera, CpuPathTracerSettings settings);
        void set_accel(std::unique_ptr<AccelInterface> new_accel);
        void set_log_sink(TraceLogSink new_log_sink);

        bool render_next_tile(Tile* out_tile = nullptr);
        // 允许并行渲染互不重叠的 tile；调用方不得并发覆盖同一像素，也不得同时 reset 或读取 Film。
        void render_tile(const Tile& tile);
        bool render_pixel(godot::Vector2i pixel, int pass_index = 0, int sample_index = 0);
        godot::Color trace_path(const Ray& ray, Rng& rng) const;
        godot::Color trace_path(const RayDifferential& ray, Rng& rng) const;

        Film& get_film();
        const Film& get_film() const;

        const Scene& get_scene() const;
        const Camera& get_camera() const;
        const CpuPathTracerSettings& get_settings() const;
        // 仅在全部 `render_tile` 调用结束后读取，避免与 tile 统计归并并发访问。
        const RenderStatistics& get_statistics() const;
        int get_primary_hit_sample_count(godot::Vector2i pixel) const;
        int get_primary_miss_sample_count(godot::Vector2i pixel) const;

    private:
        void rebuild_integrator();
        void render_sample(
            godot::Vector2i pixel,
            int pass_index,
            int sample_index,
            RenderStatistics* statistics
        );
        TraceResult trace_sample(
            const RayDifferential& ray,
            Rng& rng,
            RenderStatistics* statistics = nullptr
        ) const;
        void merge_statistics(const RenderStatistics& statistics);
        void record_primary_hit(godot::Vector2i pixel, bool hit);
        bool contains_pixel(godot::Vector2i pixel) const;
        std::size_t pixel_index(godot::Vector2i pixel) const;

        Scene scene;
        Camera camera;
        CpuPathTracerSettings settings;
        RenderStatistics render_statistics;
        std::mutex render_statistics_mutex;
        Film film;
        FrameAccumulator frame_accumulator;
        std::vector<int> primary_hit_sample_count;
        std::vector<int> primary_miss_sample_count;
        std::unique_ptr<AccelInterface> accel;
        std::unique_ptr<Integrator> integrator;
        TraceLogSink log_sink;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_CPU_PATH_TRACER_H
