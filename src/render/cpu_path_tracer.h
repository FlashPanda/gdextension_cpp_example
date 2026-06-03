#ifndef GDEXTENSION_CPP_EXAMPLE_CPU_PATH_TRACER_H
#define GDEXTENSION_CPP_EXAMPLE_CPU_PATH_TRACER_H

#include <cstdint>
#include <memory>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "../core/rt_random.h"
#include "../core/rt_types.h"
#include "../scene/rt_scene.h"
#include "../accel/accel_interface.h"
#include "film.h"
#include "frame_accumulator.h"
#include "integrators.h"

namespace godot_rt {

    struct CpuPathTracerSettings {
        godot::Vector2i image_size = godot::Vector2i(0, 0);
        godot::Vector2i tile_size = godot::Vector2i(16, 16);
        int samples_per_pixel = 1;
        int max_depth = 4;
        std::uint64_t seed = 1;
    };

    class CpuPathTracer {
    public:
        CpuPathTracer() = default;

        void reset(const Scene& scene, const Camera& camera, CpuPathTracerSettings settings);
        void set_accel(std::unique_ptr<AccelInterface> new_accel);

        bool render_next_tile(Tile* out_tile = nullptr);
        void render_tile(const Tile& tile);
        godot::Color trace_path(const Ray& ray, Rng& rng) const;
        godot::Color trace_path(const RayDifferential& ray, Rng& rng) const;

        Film& get_film();
        const Film& get_film() const;

        const Scene& get_scene() const;
        const Camera& get_camera() const;
        const CpuPathTracerSettings& get_settings() const;

    private:
        void rebuild_integrator();

        Scene scene;
        Camera camera;
        CpuPathTracerSettings settings;
        Film film;
        FrameAccumulator frame_accumulator;
        std::unique_ptr<AccelInterface> accel;
        std::unique_ptr<Integrator> integrator;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_CPU_PATH_TRACER_H
