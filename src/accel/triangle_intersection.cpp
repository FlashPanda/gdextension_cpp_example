#include "triangle_intersection.h"

#include <cmath>

namespace godot_rt {
namespace {

    constexpr real_t T_MIN = 0.0001;
    constexpr real_t DET_EPSILON = 0.00000001;

    godot::Vector3 triangle_normal(const Triangle& triangle) {
        godot::Vector3 normal = (triangle.p1 - triangle.p0).cross(triangle.p2 - triangle.p0);
        if (normal.length_squared() != 0.0) {
            normal.normalize();
        }
        return normal;
    }

}

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

void fill_triangle_hit(const Triangle& triangle, int triangle_index, const Ray& ray,
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

bool is_better_triangle_hit(real_t candidate_t, int candidate_index,
                            real_t closest_t, int closest_index) {
    return candidate_t < closest_t ||
           (candidate_t == closest_t && candidate_index > closest_index);
}

}
