#ifndef GDEXTENSION_CPP_EXAMPLE_BRUTE_FORCE_ACCEL_H
#define GDEXTENSION_CPP_EXAMPLE_BRUTE_FORCE_ACCEL_H

#include <vector>

#include "accel_interface.h"

namespace godot_rt {

    class BruteForceAccel final : public AccelInterface {
    public:
        void build(const Scene& scene) override;
        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const override;
        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const override;

    private:
        std::vector<Triangle> triangles;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_BRUTE_FORCE_ACCEL_H
