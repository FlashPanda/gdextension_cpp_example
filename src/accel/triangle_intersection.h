#ifndef GDEXTENSION_CPP_EXAMPLE_TRIANGLE_INTERSECTION_H
#define GDEXTENSION_CPP_EXAMPLE_TRIANGLE_INTERSECTION_H

#include "../core/ray.h"
#include "../core/rt_types.h"

namespace godot_rt {

    struct TriangleIntersection {
        real_t t = 0.0;
        real_t u = 0.0;
        real_t v = 0.0;
    };

    bool intersect_triangle(const Triangle& triangle, const Ray& ray, real_t t_max,
                            TriangleIntersection* intersection);
    void fill_triangle_hit(const Triangle& triangle, int triangle_index, const Ray& ray,
                           const TriangleIntersection& intersection, Hit* hit);
    bool is_better_triangle_hit(real_t candidate_t, int candidate_index,
                                real_t closest_t, int closest_index);

}

#endif // GDEXTENSION_CPP_EXAMPLE_TRIANGLE_INTERSECTION_H
