#include "cpu_path_tracer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace godot_rt {
namespace {

    constexpr float PI = 3.14159265358979323846f;
    constexpr float RAY_EPSILON = 0.0001f;

    godot::Color black() {
        return godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    std::uint64_t mix_u64(std::uint64_t value) {
        value += 0x9e3779b97f4a7c15ull;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31);
    }

    std::uint64_t sample_seed(const godot::Vector2i& pixel, int pass_index, int sample_index,
                              std::uint64_t base_seed) {
        std::uint64_t seed = mix_u64(base_seed);
        seed ^= mix_u64(static_cast<std::uint32_t>(pixel.x));
        seed ^= mix_u64(static_cast<std::uint32_t>(pixel.y) << 1);
        seed ^= mix_u64(static_cast<std::uint32_t>(pass_index) << 2);
        seed ^= mix_u64(static_cast<std::uint32_t>(sample_index) << 3);
        return mix_u64(seed);
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

void CpuPathTracer::reset(const Scene& new_scene, const Camera& new_camera, CpuPathTracerSettings new_settings) {
    new_settings.image_size.x = std::max(new_settings.image_size.x, 0);
    new_settings.image_size.y = std::max(new_settings.image_size.y, 0);
    new_settings.tile_size.x = std::max(new_settings.tile_size.x, 1);
    new_settings.tile_size.y = std::max(new_settings.tile_size.y, 1);
    new_settings.samples_per_pixel = std::max(new_settings.samples_per_pixel, 1);
    new_settings.max_depth = std::max(new_settings.max_depth, 0);

    scene = new_scene;
    camera = new_camera;
    settings = new_settings;

    film.resize(settings.image_size);
    frame_accumulator.resize(settings.image_size, settings.tile_size);

    if (accel) {
        accel->build(scene);
    }
}

void CpuPathTracer::set_accel(std::unique_ptr<AccelInterface> new_accel) {
    accel = std::move(new_accel);
    if (accel) {
        accel->build(scene);
    }
}

bool CpuPathTracer::render_next_tile(Tile* out_tile) {
    Tile tile;
    if (!frame_accumulator.next_tile(&tile)) {
        return false;
    }

    render_tile(tile);
    if (out_tile != nullptr) {
        *out_tile = tile;
    }
    return true;
}

void CpuPathTracer::render_tile(const Tile& tile) {
    const int x_begin = std::max(tile.origin.x, 0);
    const int y_begin = std::max(tile.origin.y, 0);
    const int x_end = std::min(tile.origin.x + tile.size.x, settings.image_size.x);
    const int y_end = std::min(tile.origin.y + tile.size.y, settings.image_size.y);

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const godot::Vector2i pixel(x, y);
            for (int sample_index = 0; sample_index < settings.samples_per_pixel; ++sample_index) {
                Rng rng(sample_seed(pixel, tile.pass_index, sample_index, settings.seed));
                const godot::Vector2 jitter = rng.next_2d();
                const godot::Vector2 pixel_sample(
                    static_cast<float>(x) + jitter.x,
                    static_cast<float>(y) + jitter.y
                );

                const RayDifferential ray = camera.generate_primary_ray_differential(pixel_sample, settings.image_size);
                film.add_sample(pixel, trace_path(ray, rng));
            }
        }
    }
}

godot::Color CpuPathTracer::trace_path(const Ray& ray, Rng& rng) const {
    return trace_path(RayDifferential(ray), rng);
}

godot::Color CpuPathTracer::trace_path(const RayDifferential& ray, Rng& rng) const {
    if (!accel) {
        return black();
    }

    godot::Color radiance = black();
    godot::Color throughput(1.0f, 1.0f, 1.0f, 1.0f);
    RayDifferential current_ray = ray;

    for (int depth = 0; depth < settings.max_depth; ++depth) {
        Hit hit;
        if (!accel->intersect(current_ray, &hit)) {
            break;
        }

        const Material& material = material_or_default(scene, hit.materialId);
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

Film& CpuPathTracer::get_film() {
    return film;
}

const Film& CpuPathTracer::get_film() const {
    return film;
}

const Scene& CpuPathTracer::get_scene() const {
    return scene;
}

const Camera& CpuPathTracer::get_camera() const {
    return camera;
}

const CpuPathTracerSettings& CpuPathTracer::get_settings() const {
    return settings;
}

}
