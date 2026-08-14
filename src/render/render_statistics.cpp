#include "render_statistics.h"

namespace godot_rt {

void merge_render_statistics(RenderStatistics* destination, const RenderStatistics& source) noexcept {
    if (destination == nullptr) {
        return;
    }

    // 本函数不自行加锁；调用方必须保证 `destination` 独占，或在外层持有对应互斥锁。
    destination->intersection_ms += source.intersection_ms;
    destination->primary_ray_count += source.primary_ray_count;
    destination->primary_ray_hit_count += source.primary_ray_hit_count;
    destination->primary_ray_miss_count += source.primary_ray_miss_count;
}

}
