#ifndef GDEXTENSION_CPP_EXAMPLE_BVH_TREE_H
#define GDEXTENSION_CPP_EXAMPLE_BVH_TREE_H

#include <cstddef>
#include <vector>

#include "aabb.h"
#include "accel_interface.h"
#include "triangle_intersection.h"
#include "../render/parallel_task_scheduler.h"

namespace godot_rt {

    enum class BvhSplitMethod {
        Median,
        Sah,
        Hlbvh,
    };

    // 仅供 C++ 构建与故障注入测试使用；0 自动选择线程数，不向 Godot 暴露调度细节。
    struct BvhBuildOptions {
        unsigned int thread_count = 0;
        ThreadLauncher thread_launcher;
    };

    // 内部构建策略可变，节点与求交语义由各 BVH AccelInterface 实现共享。
    class BvhTree {
    public:
        explicit BvhTree(BvhSplitMethod split_method);

        void build(const Scene& scene, const BvhBuildOptions& options = {});
        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const;
        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const;

        std::size_t node_count() const;
        std::size_t root_left_primitive_count() const;

    private:
        friend struct BvhTreeTestAccess;
        struct PrimitiveInfo {
            AxisAlignedBounds bounds;
            godot::Vector3 centroid;
        };

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

        int build_node(std::size_t first, std::size_t count,
                       const std::vector<PrimitiveInfo>& primitive_info);
        int build_hlbvh(const std::vector<PrimitiveInfo>& primitive_info, const BvhBuildOptions& options);
        int build_upper_sah(std::vector<int>& roots, std::size_t first, std::size_t count);
        std::size_t subtree_primitive_count(int node_index) const;
        std::size_t split_median(std::size_t first, std::size_t count, int axis,
                                 const std::vector<PrimitiveInfo>& primitive_info);
        std::size_t split_sah(std::size_t first, std::size_t count, int axis,
                              const AxisAlignedBounds& bounds,
                              const AxisAlignedBounds& centroid_bounds,
                              const std::vector<PrimitiveInfo>& primitive_info);
        void intersect_node(int node_index, const Ray& ray, real_t* closest_t,
                            int* closest_index, TriangleIntersection* closest_intersection) const;
        bool intersect_node_p(int node_index, const Ray& ray, real_t t_max) const;

        BvhSplitMethod split_method;
        int root_index = -1;
        std::vector<Triangle> triangles;
        std::vector<int> primitive_indices;
        std::vector<Node> nodes;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_BVH_TREE_H
