#include "brute_force_accel.h"

#include <cmath>

namespace godot_rt {
namespace {

    constexpr real_t T_MIN = 0.0001;
    constexpr real_t DET_EPSILON = 0.00000001;

    struct TriangleIntersection {
        real_t t = 0.0;
        real_t u = 0.0;
        real_t v = 0.0;
    };

    bool intersect_triangle(const Triangle& triangle, const Ray& ray, real_t t_max,
                            TriangleIntersection* intersection) {
        const godot::Vector3 edge1 = triangle.p1 - triangle.p0;
        const godot::Vector3 edge2 = triangle.p2 - triangle.p0;
        const godot::Vector3 pvec = ray.d.cross(edge2);
        const real_t det = edge1.dot(pvec);

        if (std::abs(det) <= DET_EPSILON) {
            return false;
        }

        const real_t inv_det = 1.0 / det;
        const godot::Vector3 tvec = ray.o - triangle.p0;
        const real_t u = tvec.dot(pvec) * inv_det;
        if (u < 0.0 || u > 1.0) {
            return false;
        }

        const godot::Vector3 qvec = tvec.cross(edge1);
        const real_t v = ray.d.dot(qvec) * inv_det;
        if (v < 0.0 || u + v > 1.0) {
            return false;
        }

        const real_t t = edge2.dot(qvec) * inv_det;
        if (t <= T_MIN || t > t_max) {
            return false;
        }

        if (intersection != nullptr) {
            intersection->t = t;
            intersection->u = u;
            intersection->v = v;
        }

        return true;
    }

    godot::Vector3 triangle_normal(const Triangle& triangle) {
        godot::Vector3 normal = (triangle.p1 - triangle.p0).cross(triangle.p2 - triangle.p0);
        if (normal.length_squared() != 0.0) {
            normal.normalize();
        }
        return normal;
    }

    void fill_hit(const Triangle& triangle, int triangle_index, const Ray& ray,
                  const TriangleIntersection& intersection, Hit* hit) {
        if (hit == nullptr) {
            return;
        }

        const real_t w = 1.0 - intersection.u - intersection.v;
        godot::Vector3 normal = triangle.n0 * w + triangle.n1 * intersection.u + triangle.n2 * intersection.v;
        if (normal.length_squared() == 0.0) {
            normal = triangle_normal(triangle);
        } else {
            normal.normalize();
        }

        hit->hit = true;
        hit->t = static_cast<float>(intersection.t);
        hit->triangleIndex = triangle_index;
        hit->materialId = triangle.materialId;
        hit->position = ray(static_cast<float>(intersection.t));
        hit->wo = -ray.d;
        hit->normal = normal;
        hit->uv = triangle.uv0 * w + triangle.uv1 * intersection.u + triangle.uv2 * intersection.v;
    }

}

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

    fill_hit(triangles[closest_index], closest_index, ray, closest_intersection, hit);
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
