#ifndef GDEXTENSION_CPP_EXAMPLE_BVH_ACCEL_H
#define GDEXTENSION_CPP_EXAMPLE_BVH_ACCEL_H

#include "accel_interface.h"
#include "bvh_tree.h"

namespace godot_rt {

    class BvhAccel final : public AccelInterface {
    public:
        BvhAccel();

        void build(const Scene& scene) override;
        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const override;
        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const override;

    private:
        BvhTree tree;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_BVH_ACCEL_H
