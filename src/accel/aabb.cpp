#include "aabb.h"

#include <algorithm>
#include <cmath>

namespace godot_rt {
namespace {

    constexpr real_t DIRECTION_EPSILON = 0.000000000001;

    real_t component(const godot::Vector3& value, int axis) {
        switch (axis) {
            case 0:
                return value.x;
            case 1:
                return value.y;
            default:
                return value.z;
        }
    }

}

AxisAlignedBounds triangle_bounds(const Triangle& triangle) {
    AxisAlignedBounds bounds;
    bounds.minimum = godot::Vector3(
        std::min({triangle.p0.x, triangle.p1.x, triangle.p2.x}),
        std::min({triangle.p0.y, triangle.p1.y, triangle.p2.y}),
        std::min({triangle.p0.z, triangle.p1.z, triangle.p2.z})
    );
    bounds.maximum = godot::Vector3(
        std::max({triangle.p0.x, triangle.p1.x, triangle.p2.x}),
        std::max({triangle.p0.y, triangle.p1.y, triangle.p2.y}),
        std::max({triangle.p0.z, triangle.p1.z, triangle.p2.z})
    );
    bounds.valid = true;
    return bounds;
}

AxisAlignedBounds union_bounds(const AxisAlignedBounds& left, const AxisAlignedBounds& right) {
    if (!left.valid) {
        return right;
    }
    if (!right.valid) {
        return left;
    }

    AxisAlignedBounds result;
    result.minimum = godot::Vector3(
        std::min(left.minimum.x, right.minimum.x),
        std::min(left.minimum.y, right.minimum.y),
        std::min(left.minimum.z, right.minimum.z)
    );
    result.maximum = godot::Vector3(
        std::max(left.maximum.x, right.maximum.x),
        std::max(left.maximum.y, right.maximum.y),
        std::max(left.maximum.z, right.maximum.z)
    );
    result.valid = true;
    return result;
}

godot::Vector3 bounds_centroid(const AxisAlignedBounds& bounds) {
    return bounds.valid ? (bounds.minimum + bounds.maximum) * 0.5 : godot::Vector3();
}

godot::Vector3 bounds_extent(const AxisAlignedBounds& bounds) {
    return bounds.valid ? bounds.maximum - bounds.minimum : godot::Vector3();
}

bool bounds_contains(const AxisAlignedBounds& outer, const AxisAlignedBounds& inner) {
    return outer.valid && inner.valid &&
           inner.minimum.x >= outer.minimum.x && inner.maximum.x <= outer.maximum.x &&
           inner.minimum.y >= outer.minimum.y && inner.maximum.y <= outer.maximum.y &&
           inner.minimum.z >= outer.minimum.z && inner.maximum.z <= outer.maximum.z;
}

bool intersect_bounds(const AxisAlignedBounds& bounds, const Ray& ray, real_t t_max,
                      real_t* out_t_enter) {
    if (!bounds.valid || t_max < 0.0) {
        return false;
    }

    real_t t_enter = 0.0;
    real_t t_exit = t_max;
    // slab 区间始终裁剪在 `[0, t_max]`；平行轴必须位于 slab 内，否则整盒不可达。
    for (int axis = 0; axis < 3; ++axis) {
        const real_t origin = component(ray.o, axis);
        const real_t direction = component(ray.d, axis);
        const real_t minimum = component(bounds.minimum, axis);
        const real_t maximum = component(bounds.maximum, axis);

        if (std::abs(direction) <= DIRECTION_EPSILON) {
            if (origin < minimum || origin > maximum) {
                return false;
            }
            continue;
        }

        const real_t inverse_direction = 1.0 / direction;
        real_t axis_enter = (minimum - origin) * inverse_direction;
        real_t axis_exit = (maximum - origin) * inverse_direction;
        if (axis_enter > axis_exit) {
            std::swap(axis_enter, axis_exit);
        }
        t_enter = std::max(t_enter, axis_enter);
        t_exit = std::min(t_exit, axis_exit);
        if (t_enter > t_exit) {
            return false;
        }
    }

    if (out_t_enter != nullptr) {
        *out_t_enter = t_enter;
    }
    return true;
}

}
