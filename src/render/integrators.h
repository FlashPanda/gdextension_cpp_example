#ifndef GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
#define GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H

#include <functional>
#include <memory>
#include <vector>

#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/core/math_defs.hpp>
#include <godot_cpp/variant/color.hpp>

#include "../core/ray.h"

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
            std::vector<Light> lights
            );

        virtual godot::String to_string() const = 0;

        virtual void render() = 0;

        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const;

        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const;

        // 判断两点之间是否未被遮挡
        bool unoccluded(const Hit& p0, const Hit& p1) const;

        AccelInterface* aggregate;
        std::vector<Light> lights;
    protected:
        Integrator(AccelInterface* accel, std::vector<Light> lights);
    };

    class ImageTileIntegrator: public Integrator {
    public:
        ImageTileIntegrator(Camera* camera, Sampler* sampler, AccelInterface* accel,
            std::vector<Light> lights)
                : Integrator(accel, lights) , camera(camera), sampler_propertype(sampler){}

        void render();

        virtual void evaluate_pixel_sample(godot::Vector2i p_pixel, int sample_index, Sampler* sampler) = 0;

    protected:
        Camera* camera;
        Sampler* sampler_propertype;
    };

    class RayIntegrator: public ImageTileIntegrator {
    public:
        RayIntegrator(Camera* camera, Sampler* sampler, AccelInterface* accel, std::vector<Light> lights)
            : ImageTileIntegrator(camera, sampler, accel, lights) {}

        void evaluate_pixel_sample(godot::Vector2i p_pixel, int sample_index, Sampler* sampler) final;

        virtual godot::Color li(RayDifferential ray, Sampler* sampler) const = 0;
    };

    class RandomWalkIntegrator: public RayIntegrator {
    public:
        RandomWalkIntegrator(int max_depth, Camera* camera, Sampler* sampler,
            AccelInterface* accel, std::vector<Light> lights):
        RayIntegrator(camera, sampler, accel, lights), max_depth(max_depth) {}

        static std::unique_ptr<RandowmWalkIntegrator> create(
            Camera* camera, Sampler* sampler, AccelInterface* accel, std::vector<Light> lights);

        godot::String to_string() const final;

        godot::Color li(RayDifferential ray, Sampler* sampler) const {
            return li_random_walk(ray, sampler, 0);
        }

    private:
        godot::Color li_random_walk(RayDifferential ray, Sampler* sampler, int depth) const;

        int max_depth;
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
