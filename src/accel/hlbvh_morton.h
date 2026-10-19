#ifndef GDEXTENSION_CPP_EXAMPLE_HLBVH_MORTON_H
#define GDEXTENSION_CPP_EXAMPLE_HLBVH_MORTON_H

#include <cstdint>
#include <vector>

#include "aabb.h"

namespace godot_rt::detail {

    struct MortonPrimitive {
        int primitive_index;
        uint32_t code;
    };

    // 输入为 world-space 质心与质心包围盒；零跨度轴映射为零，最大边界限制为 10 位。
    uint32_t encode_morton_3d(const godot::Vector3& centroid, const AxisAlignedBounds& bounds);
    // 稳定排序保留碰撞编码的原始图元次序，线程完成顺序不参与图元编号分配。
    void radix_sort_morton(std::vector<MortonPrimitive>& primitives);

}

#endif // GDEXTENSION_CPP_EXAMPLE_HLBVH_MORTON_H
