#include "brute_force_accel.h"

#include "triangle_intersection.h"

namespace godot_rt {

void BruteForceAccel::build(const Scene& scene) {
    triangles = scene.get_triangles();
}

bool BruteForceAccel::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
    TriangleIntersection closest_intersection;
    int closest_index = -1;
    real_t closest_t = t_max;

    for (int index = 0; index < static_cast<int>(triangles.size()); ++index) {
        TriangleIntersection intersection;
        if (!intersect_triangle(triangles[index], ray, closest_t, &intersection)) {
            continue;
        }

        closest_t = intersection.t;
        closest_intersection = intersection;
        closest_index = index;
    }

    if (closest_index < 0) {
        return false;
    }

    fill_triangle_hit(triangles[closest_index], closest_index, ray, closest_intersection, hit);
    return true;
}

bool BruteForceAccel::intersect_p(const Ray& ray, real_t t_max) const {
    for (const Triangle& triangle : triangles) {
        if (intersect_triangle(triangle, ray, t_max, nullptr)) {
            return true;
        }
    }

    return false;
}

}
