#ifndef GDEXTENSION_CPP_EXAMPLE_RENDER_STATISTICS_H
#define GDEXTENSION_CPP_EXAMPLE_RENDER_STATISTICS_H

#include <cstdint>

namespace godot_rt {

    // 统计对象按单个 pixel 或 tile 局部累计，再由 `CpuPathTracer` 在低频边界统一归并。
    // `intersection_ms` 是各线程、各次求交耗时的累计值，表示 CPU 求交工作量而非墙钟渲染时长。
    struct RenderStatistics {
        double intersection_ms = 0.0;
        std::int64_t primary_ray_count = 0;
        std::int64_t primary_ray_hit_count = 0;
        std::int64_t primary_ray_miss_count = 0;
    };

    void merge_render_statistics(RenderStatistics* destination, const RenderStatistics& source) noexcept;

}

#endif // GDEXTENSION_CPP_EXAMPLE_RENDER_STATISTICS_H
