#include "integrators.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "../accel/accel_interface.h"
#include "../util/logger.h"
#include "bsdf/GodotStandardBrdf.h"

namespace godot_rt {
namespace {

    constexpr float RAY_EPSILON = 0.0001f;
    using TimingClock = std::chrono::steady_clock;

    godot::Color black() {
        return godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const Material& material_or_default(const Scene& scene, int material_id) {
        static const Material default_material;
        const std::vector<Material>& materials = scene.get_materials();
        if (material_id < 0 || material_id >= static_cast<int>(materials.size())) {
            return default_material;
        }
        return materials[material_id];
    }

    void add_emission(godot::Color& radiance, const godot::Color& throughput, const godot::Color& emission) {
        radiance.r += throughput.r * emission.r;
        radiance.g += throughput.g * emission.g;
        radiance.b += throughput.b * emission.b;
        radiance.a = 1.0f;
    }

    void multiply_throughput(godot::Color& throughput, const godot::Color& f, float scale) {
        throughput.r *= f.r * scale;
        throughput.g *= f.g * scale;
        throughput.b *= f.b * scale;
        throughput.a = 1.0f;
    }

    void add_direct_lighting(godot::Color& radiance,
                             const godot::Color& throughput,
                             const godot::Color& f,
                             const godot::Color& light_radiance,
                             real_t scale) {
        radiance.r += throughput.r * f.r * light_radiance.r * scale;
        radiance.g += throughput.g * f.g * light_radiance.g * scale;
        radiance.b += throughput.b * f.b * light_radiance.b * scale;
        radiance.a = 1.0f;
    }

    double elapsed_ms(TimingClock::time_point start) {
        return std::chrono::duration<double, std::milli>(TimingClock::now() - start).count();
    }

}

Integrator::~Integrator() = default;

std::unique_ptr<Integrator> Integrator::create(
    const godot::String& name,
    const Scene* scene,
    AccelInterface* accel,
    int max_depth,
    RenderStatistics* statistics
) {
    (void)name;
    return RandomWalkIntegrator::create(scene, accel, max_depth, statistics);
}

bool Integrator::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
    if (aggregate == nullptr) {
        return false;
    }

    const auto intersect_start = TimingClock::now();
    const bool result = aggregate->intersect(ray, hit, t_max);
    if (statistics != nullptr) {
        statistics->intersection_ms += elapsed_ms(intersect_start);
    }
    return result;
}

bool Integrator::intersect_p(const Ray& ray, real_t t_max) const {
    if (aggregate == nullptr) {
        return false;
    }

    const auto intersect_start = TimingClock::now();
    const bool result = aggregate->intersect_p(ray, t_max);
    if (statistics != nullptr) {
        statistics->intersection_ms += elapsed_ms(intersect_start);
    }
    return result;
}

bool Integrator::unoccluded(const Hit& p0, const Hit& p1) const {
    godot::Vector3 direction = p1.position - p0.position;
    const real_t distance = direction.length();
    if (distance <= RAY_EPSILON) {
        return true;
    }

    direction /= distance;
    const Ray shadow_ray(p0.position + direction * RAY_EPSILON, direction);
    return !intersect_p(shadow_ray, distance - RAY_EPSILON);
}

void Integrator::set_scene(const Scene* new_scene) {
    scene = new_scene;
}

void Integrator::set_accel(AccelInterface* new_accel) {
    aggregate = new_accel;
}

void Integrator::set_max_depth(int new_max_depth) {
    max_depth = std::max(new_max_depth, 0);
}

Integrator::Integrator(
    const Scene* new_scene,
    AccelInterface* accel,
    int new_max_depth,
    RenderStatistics* new_statistics
)
    : scene(new_scene),
      aggregate(accel),
      statistics(new_statistics),
      max_depth(std::max(new_max_depth, 0)) {}

RandomWalkIntegrator::RandomWalkIntegrator(
    const Scene* scene,
    AccelInterface* accel,
    int max_depth,
    RenderStatistics* statistics
) : Integrator(scene, accel, max_depth, statistics) {}

std::unique_ptr<RandomWalkIntegrator> RandomWalkIntegrator::create(
    const Scene* scene,
    AccelInterface* accel,
    int max_depth,
    RenderStatistics* statistics
) {
    return std::make_unique<RandomWalkIntegrator>(scene, accel, max_depth, statistics);
}

godot::String RandomWalkIntegrator::to_string() const {
    return "RandomWalkIntegrator";
}

TraceResult RandomWalkIntegrator::trace(const RayDifferential& ray, Rng& rng) const {
    TraceResult result;
    if (scene == nullptr || aggregate == nullptr) {
        return result;
    }

    godot::Color radiance = black();
    godot::Color throughput(1.0f, 1.0f, 1.0f, 1.0f);
    RayDifferential current_ray = ray;

    for (int depth = 0; depth < max_depth; ++depth) {
        Logger::info(
            godot::String("current ray: ") +
            godot::String(current_ray.to_string().c_str()) +
            godot::String(", depth = ") +
            godot::String::num_int64(depth)
        );

        Hit hit;
        const bool ray_hit = intersect(current_ray, &hit);
        if (depth == 0) {
            result.primary_ray_tested = true;
            result.primary_ray_hit = ray_hit;
        }
        if (depth == 0 && statistics != nullptr) {
            ++statistics->primary_ray_count;
            if (ray_hit) {
                ++statistics->primary_ray_hit_count;
            } else {
                ++statistics->primary_ray_miss_count;
            }
        }
        if (!ray_hit) {
            Logger::info(
    godot::String("!ray_hit")
);
            break;
        }

        const Material& material = material_or_default(*scene, hit.materialId);
        const godot::Color emission = sample_material_emission(material, hit.uv);
        if (brdf::max_rgb(emission) > 0.0f) {
            add_emission(radiance, throughput, emission);
        }

        godot::Vector3 normal = hit.normal;
        if (normal.length_squared() == 0.0f) {
            Logger::info(godot::String("!normal.length_squared() == 0.0f"));
            break;
        }
        normal.normalize();
        if (normal.dot(current_ray.d) > 0.0f) {
            normal = -normal;
        }

        godot::Vector3 wo = -current_ray.d;
        if (wo.length_squared() == 0.0f) {
            Logger::info(godot::String("wo.length_squared() == 0.0f"));
            break;
        }
        wo.normalize();
        if (normal.dot(wo) <= 0.0f) {
            Logger::info(godot::String("normal.dot(wo) <= 0.0f"));
            break;
        }

        const GodotStandardParams bsdf_params = make_godot_standard_params(material, hit.uv, normal);
        const GodotStandardBRDF bsdf(bsdf_params);

        for (const Light& light : scene->get_lights()) {
            const LightSample light_sample = light.sample_li(hit.position);
            if (!light_sample.valid) {
                Logger::info(godot::String("!light_sample.valid"));
                continue;
            }

            const real_t cos_theta = normal.dot(light_sample.wi);
            if (cos_theta <= 0.0) {
                Logger::info(godot::String("cos_theta <= 0.0"));
                continue;
            }

            if (light.casts_shadow) {
                real_t shadow_t_max = light_sample.distance;
                if (std::isfinite(static_cast<double>(shadow_t_max))) {
                    shadow_t_max = std::max(shadow_t_max - RAY_EPSILON, static_cast<real_t>(0.0));
                }

                const Ray shadow_ray(hit.position + normal * RAY_EPSILON, light_sample.wi);
                if (intersect_p(shadow_ray, shadow_t_max)) {
                    continue;
                }
            }

            const godot::Color f = bsdf.eval(wo, light_sample.wi);
            if (brdf::is_black(f)) {
                Logger::info(godot::String("brdf::is_black(f)"));
                continue;
            }

            Logger::info(
                godot::String("before add_direct_lighting: radiance=(") +
                godot::String::num(radiance.r, 6) + ", " +
                godot::String::num(radiance.g, 6) + ", " +
                godot::String::num(radiance.b, 6) + ", " +
                godot::String::num(radiance.a, 6) + "), light_sample.radiance=(" +
                godot::String::num(light_sample.radiance.r, 6) + ", " +
                godot::String::num(light_sample.radiance.g, 6) + ", " +
                godot::String::num(light_sample.radiance.b, 6) + ", " +
                godot::String::num(light_sample.radiance.a, 6) + ")"
            );
            add_direct_lighting(radiance, throughput, f, light_sample.radiance, cos_theta);
        }

        const BsdfSample bsdf_sample = bsdf.sample(wo, rng);
        const float sample_cos_theta = normal.dot(bsdf_sample.wi);
        if (bsdf_sample.pdf <= 0.0f || sample_cos_theta <= 0.0f || brdf::is_black(bsdf_sample.f)) {
            break;
        }

        multiply_throughput(
            throughput,
            bsdf_sample.f,
            static_cast<float>(sample_cos_theta) / bsdf_sample.pdf
        );
        if (brdf::max_rgb(throughput) <= 0.0f) {
            Logger::info(godot::String("brdf::max_rgb(throughput) <= 0.0f"));
            break;
        }

        current_ray = RayDifferential(hit.position + normal * RAY_EPSILON, bsdf_sample.wi);
    }

    radiance.a = 1.0f;
    result.radiance = radiance;

    Logger::info(
        godot::String("final radiance=(") +
        godot::String::num(result.radiance.r, 6) + ", " +
        godot::String::num(result.radiance.g, 6) + ", " +
        godot::String::num(result.radiance.b, 6) + ", " +
        godot::String::num(result.radiance.a, 6) + ")"
    );
    return result;
}

}
