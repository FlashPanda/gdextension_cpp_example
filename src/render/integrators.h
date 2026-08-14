#ifndef GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
#define GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H

#include <functional>
#include <memory>

#include <godot_cpp/core/math_defs.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>

#include "../core/ray.h"
#include "../core/rt_light.h"
#include "../core/rt_random.h"
#include "../core/rt_types.h"
#include "../scene/rt_scene.h"
#include "render_statistics.h"

namespace godot_rt {

    using TraceLogSink = std::function<void(const godot::String&)>;

    class AccelInterface;

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
            int max_depth
        );

        virtual godot::String to_string() const = 0;
        // `statistics` 必须指向只属于当前调用线程的局部统计对象；integrator 不保留该指针。
        virtual TraceResult trace(
            const RayDifferential& ray,
            Rng& rng,
            RenderStatistics* statistics = nullptr
        ) const = 0;

        bool intersect(
            const Ray& ray,
            Hit* hit,
            real_t t_max = Math_INF,
            RenderStatistics* statistics = nullptr
        ) const;
        bool intersect_p(
            const Ray& ray,
            real_t t_max = Math_INF,
            RenderStatistics* statistics = nullptr
        ) const;
        bool unoccluded(const Hit& p0, const Hit& p1, RenderStatistics* statistics = nullptr) const;

        void set_scene(const Scene* new_scene);
        void set_accel(AccelInterface* new_accel);
        void set_max_depth(int new_max_depth);
        void set_log_sink(TraceLogSink new_log_sink);

    protected:
        Integrator(const Scene* scene, AccelInterface* accel, int max_depth);

        bool has_log_sink() const noexcept;
        void log_info(const godot::String& message) const;

        const Scene* scene = nullptr;
        AccelInterface* aggregate = nullptr;
        int max_depth = 0;
        TraceLogSink log_sink;
    };

    class RandomWalkIntegrator final : public Integrator {
    public:
        RandomWalkIntegrator(
            const Scene* scene,
            AccelInterface* accel,
            int max_depth
        );

        static std::unique_ptr<RandomWalkIntegrator> create(
            const Scene* scene,
            AccelInterface* accel,
            int max_depth
        );

        godot::String to_string() const final;
        TraceResult trace(
            const RayDifferential& ray,
            Rng& rng,
            RenderStatistics* statistics = nullptr
        ) const final;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_INTEGRATORS_H
