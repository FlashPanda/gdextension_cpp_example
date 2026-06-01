#ifndef GDEXTENSION_CPP_EXAMPLE_ACCEL_INTERFACE_H
#define GDEXTENSION_CPP_EXAMPLE_ACCEL_INTERFACE_H

#include "../core/ray.h"
#include "../scene/rt_scene.h"

namespace godot_rt {

    class AccelInterface {
    public:
        virtual ~AccelInterface() = default;

        virtual void build(const Scene& scene) = 0;
        virtual bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const = 0;

        virtual bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const {
            Hit hit;
            return intersect(ray, &hit, t_max);
        }
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_ACCEL_INTERFACE_H
