#ifndef GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
#define GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H

#include <functional>
#include <memory>

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/core/math_defs.hpp>

namespace godot_rt
{
    class Sampler;
    class Light;
    class Ray;
    class Camera;
    class Hit;
    class AccelInterface;

    // 积分器基类
    class Integrator
    {
    public:
        virtual ~Integrator();

        static std::unique_ptr<Integrator> create(
            const godot::String& name,
            Camera* camera,
            Sampler* sampler,
            AccelInterface* accel,
            godot::Vector<Light> lights
            );

        virtual godot::String to_string() const = 0;

        virtual void Render() = 0;

        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const;

        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const;

        // 判断两点之间是否未被遮挡
        bool unoccluded(const Hit& p0, const Hit& p1) const;

        AccelInterface* aggregate;
        godot::Vector<Light> lights;
    protected:
        Integrator(AccelInterface* accel, godot::Vector<Light> lights);
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
