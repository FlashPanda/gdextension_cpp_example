#ifndef GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
#define GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H

#include <cstdint>
#include <memory>

#include <godot_cpp/core/math_defs.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>

#include "../core/ray.h"
#include "../core/rt_light.h"
#include "../core/rt_random.h"
#include "../core/rt_types.h"
#include "../scene/rt_scene.h"

namespace godot_rt {

    class AccelInterface;

    struct RenderStatistics {
        double intersection_ms = 0.0;
        std::int64_t primary_ray_count = 0;
        std::int64_t primary_ray_hit_count = 0;
        std::int64_t primary_ray_miss_count = 0;
    };

    struct TraceResult {
        godot::Color radiance = godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
        bool primary_ray_tested = false;
        bool primary_ray_hit = false;
    };

    class Integrator {
    public:
        virtual ~Integrator();

        static std::unique_ptr<Integrator> create(
            const godot::String& name,
            const Scene* scene,
            AccelInterface* accel,
            int max_depth,
            RenderStatistics* statistics = nullptr
        );

        virtual godot::String to_string() const = 0;
        virtual TraceResult trace(const RayDifferential& ray, Rng& rng) const = 0;

        bool intersect(const Ray& ray, Hit* hit, real_t t_max = Math_INF) const;
        bool intersect_p(const Ray& ray, real_t t_max = Math_INF) const;
        bool unoccluded(const Hit& p0, const Hit& p1) const;

        void set_scene(const Scene* new_scene);
        void set_accel(AccelInterface* new_accel);
        void set_max_depth(int new_max_depth);

    protected:
        Integrator(const Scene* scene, AccelInterface* accel, int max_depth, RenderStatistics* statistics);

        const Scene* scene = nullptr;
        AccelInterface* aggregate = nullptr;
        RenderStatistics* statistics = nullptr;
        int max_depth = 0;
    };

    class RandomWalkIntegrator final : public Integrator {
    public:
        RandomWalkIntegrator(
            const Scene* scene,
            AccelInterface* accel,
            int max_depth,
            RenderStatistics* statistics = nullptr
        );

        static std::unique_ptr<RandomWalkIntegrator> create(
            const Scene* scene,
            AccelInterface* accel,
            int max_depth,
            RenderStatistics* statistics = nullptr
        );

        godot::String to_string() const final;
        TraceResult trace(const RayDifferential& ray, Rng& rng) const final;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
