#ifndef GDEXTENSION_CPP_EXAMPLE_OCTREE_ACCEL_H
#define GDEXTENSION_CPP_EXAMPLE_OCTREE_ACCEL_H

#include <array>
#include <vector>

#include "aabb.h"
#include "accel_interface.h"
#include "triangle_intersection.h"

namespace godot_rt {

    class OctreeAccel final : public AccelInterface {
    public:
        void build(const Scene& scene) override;
        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const override;
        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const override;

    private:
        struct Node {
            AxisAlignedBounds bounds;
            std::array<int, 8> children{{-1, -1, -1, -1, -1, -1, -1, -1}};
            std::vector<int> primitive_indices;
        };

        int build_node(const AxisAlignedBounds& bounds, std::vector<int> primitive_indices, int depth);
        void intersect_node(int node_index, const Ray& ray, real_t* closest_t,
                            int* closest_index, TriangleIntersection* closest_intersection) const;
        bool intersect_node_p(int node_index, const Ray& ray, real_t t_max) const;

        std::vector<Triangle> triangles;
        std::vector<Node> nodes;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_OCTREE_ACCEL_H
