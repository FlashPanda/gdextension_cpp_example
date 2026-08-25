#ifndef GDEXTENSION_CPP_EXAMPLE_BVH_ACCEL_H
#define GDEXTENSION_CPP_EXAMPLE_BVH_ACCEL_H

#include <cstddef>
#include <vector>

#include "aabb.h"
#include "accel_interface.h"
#include "triangle_intersection.h"

namespace godot_rt {

    class BvhAccel final : public AccelInterface {
    public:
        void build(const Scene& scene) override;
        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const override;
        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const override;

    private:
        struct Node {
            AxisAlignedBounds bounds;
            int left = -1;
            int right = -1;
            std::size_t first = 0;
            std::size_t count = 0;

            bool is_leaf() const {
                return left < 0 && right < 0;
            }
        };

        int build_node(std::size_t first, std::size_t count);
        void intersect_node(int node_index, const Ray& ray, real_t* closest_t,
                            int* closest_index, TriangleIntersection* closest_intersection) const;
        bool intersect_node_p(int node_index, const Ray& ray, real_t t_max) const;

        std::vector<Triangle> triangles;
        std::vector<int> primitive_indices;
        std::vector<Node> nodes;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_BVH_ACCEL_H
