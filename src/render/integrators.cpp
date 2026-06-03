#include "integrators.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "../accel/accel_interface.h"

namespace godot_rt {
namespace {

    constexpr float PI = 3.14159265358979323846f;
    constexpr float RAY_EPSILON = 0.0001f;

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

    float max_rgb(const godot::Color& color) {
        return std::max(color.r, std::max(color.g, color.b));
    }

    void add_emission(godot::Color& radiance, const godot::Color& throughput, const godot::Color& emission) {
        radiance.r += throughput.r * emission.r;
        radiance.g += throughput.g * emission.g;
        radiance.b += throughput.b * emission.b;
        radiance.a = 1.0f;
    }

    void multiply_throughput(godot::Color& throughput, const godot::Color& albedo) {
        throughput.r *= albedo.r;
        throughput.g *= albedo.g;
        throughput.b *= albedo.b;
        throughput.a = 1.0f;
    }

    void add_direct_lighting(godot::Color& radiance,
                             const godot::Color& throughput,
                             const godot::Color& albedo,
                             const godot::Color& light_radiance,
                             real_t scale) {
        radiance.r += throughput.r * albedo.r * light_radiance.r * scale;
        radiance.g += throughput.g * albedo.g * light_radiance.g * scale;
        radiance.b += throughput.b * albedo.b * light_radiance.b * scale;
        radiance.a = 1.0f;
    }

    godot::Vector3 sample_cosine_hemisphere(const godot::Vector3& normal, Rng& rng) {
        const godot::Vector2 u = rng.next_2d();
        const float r = std::sqrt(u.x);
        const float theta = 2.0f * PI * u.y;
        const float x = r * std::cos(theta);
        const float y = r * std::sin(theta);
        const float z = std::sqrt(std::max(0.0f, 1.0f - u.x));

        godot::Vector3 n = normal;
        n.normalize();

        godot::Vector3 tangent;
        if (std::abs(n.x) > std::abs(n.z)) {
            tangent = godot::Vector3(-n.y, n.x, 0.0f);
        } else {
            tangent = godot::Vector3(0.0f, -n.z, n.y);
        }
        if (tangent.length_squared() == 0.0f) {
            tangent = godot::Vector3(1.0f, 0.0f, 0.0f);
        }
        tangent.normalize();

        godot::Vector3 bitangent = n.cross(tangent);
        if (bitangent.length_squared() == 0.0f) {
            bitangent = godot::Vector3(0.0f, 1.0f, 0.0f);
        }
        bitangent.normalize();

        godot::Vector3 direction = tangent * x + bitangent * y + n * z;
        direction.normalize();
        return direction;
    }

}

Integrator::~Integrator() = default;

std::unique_ptr<Integrator> Integrator::create(
    const godot::String& name,
    const Scene* scene,
    AccelInterface* accel,
    int max_depth
) {
    (void)name;
    return RandomWalkIntegrator::create(scene, accel, max_depth);
}

bool Integrator::intersect(const Ray& ray, Hit* hit, real_t t_max) const {
    if (aggregate == nullptr) {
        return false;
    }
    return aggregate->intersect(ray, hit, t_max);
}

bool Integrator::intersect_p(const Ray& ray, real_t t_max) const {
    if (aggregate == nullptr) {
        return false;
    }
    return aggregate->intersect_p(ray, t_max);
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

Integrator::Integrator(const Scene* new_scene, AccelInterface* accel, int new_max_depth)
    : scene(new_scene),
      aggregate(accel),
      max_depth(std::max(new_max_depth, 0)) {}

RandomWalkIntegrator::RandomWalkIntegrator(
    const Scene* scene,
    AccelInterface* accel,
    int max_depth
) : Integrator(scene, accel, max_depth) {}

std::unique_ptr<RandomWalkIntegrator> RandomWalkIntegrator::create(
    const Scene* scene,
    AccelInterface* accel,
    int max_depth
) {
    return std::make_unique<RandomWalkIntegrator>(scene, accel, max_depth);
}

godot::String RandomWalkIntegrator::to_string() const {
    return "RandomWalkIntegrator";
}

godot::Color RandomWalkIntegrator::trace(const RayDifferential& ray, Rng& rng) const {
    if (scene == nullptr || aggregate == nullptr) {
        return black();
    }

    godot::Color radiance = black();
    godot::Color throughput(1.0f, 1.0f, 1.0f, 1.0f);
    RayDifferential current_ray = ray;

    for (int depth = 0; depth < max_depth; ++depth) {
        Hit hit;
        if (!intersect(current_ray, &hit)) {
            break;
        }

        const Material& material = material_or_default(*scene, hit.materialId);
        if (max_rgb(material.emission) > 0.0f) {
            add_emission(radiance, throughput, material.emission);
        }

        if (material.type == MaterialType::Emissive) {
            break;
        }

        godot::Vector3 normal = hit.normal;
        if (normal.length_squared() == 0.0f) {
            break;
        }
        normal.normalize();
        if (normal.dot(current_ray.d) > 0.0f) {
            normal = -normal;
        }

        for (const Light& light : scene->get_lights()) {
            const LightSample light_sample = light.sample_li(hit.position);
            if (!light_sample.valid) {
                continue;
            }

            const real_t cos_theta = normal.dot(light_sample.wi);
            if (cos_theta <= 0.0) {
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

            add_direct_lighting(radiance, throughput, material.albedo, light_sample.radiance, cos_theta / PI);
        }

        multiply_throughput(throughput, material.albedo);
        if (max_rgb(throughput) <= 0.0f) {
            break;
        }

        const godot::Vector3 bounce_direction = sample_cosine_hemisphere(normal, rng);
        current_ray = RayDifferential(hit.position + normal * RAY_EPSILON, bounce_direction);
    }

    radiance.a = 1.0f;
    return radiance;
}

}
