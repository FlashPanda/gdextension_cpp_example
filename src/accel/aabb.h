#ifndef GDEXTENSION_CPP_EXAMPLE_AABB_H
#define GDEXTENSION_CPP_EXAMPLE_AABB_H

#include <godot_cpp/variant/vector3.hpp>

#include "../core/ray.h"
#include "../core/rt_types.h"

namespace godot_rt {

    struct AxisAlignedBounds {
        godot::Vector3 minimum;
        godot::Vector3 maximum;
        bool valid = false;
    };

    AxisAlignedBounds triangle_bounds(const Triangle& triangle);
    AxisAlignedBounds union_bounds(const AxisAlignedBounds& left, const AxisAlignedBounds& right);
    godot::Vector3 bounds_centroid(const AxisAlignedBounds& bounds);
    godot::Vector3 bounds_extent(const AxisAlignedBounds& bounds);
    bool bounds_contains(const AxisAlignedBounds& outer, const AxisAlignedBounds& inner);
    bool intersect_bounds(const AxisAlignedBounds& bounds, const Ray& ray, real_t t_max,
                          real_t* out_t_enter = nullptr);

}

#endif // GDEXTENSION_CPP_EXAMPLE_AABB_H
