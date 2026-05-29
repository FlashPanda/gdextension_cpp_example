#ifndef GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H
#define GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H

#include <cstdint>
#include <string>

#include "rt_math.h"
#include "ray.h"
#include "rt_color.h"

namespace godot_rt
{
    enum class MaterialType : std::uint8_t {
        Diffuse,
        Mirror,
        Dielectric,
        Emissive
    };

    struct Hit {
    public:
        Hit() = default;
        Hit(godot::Vector3 p, godot::Vector3 n, godot::Vector2 uv, godot::Vector3 wo)
            : hit(true), position(p), wo(wo), normal(n), uv(uv) {}

        std::string to_string() const;

        RayDifferential spawn_ray(godot::Vector3 d) const {
            return RayDifferential(position, d);
        }

        Ray spawn_ray_to(godot::Vector3 p2) const {
            return Ray(position, p2 - position);
        }

        Ray spawn_ray_to(const Hit& other) const {
            return spawn_ray_to(other.position);
        }

        bool hit = false;
        float t = 0.0f;
        int triangleIndex = -1;
        int materialId = -1;
        godot::Vector3 position;
        godot::Vector3 wo;
        godot::Vector3 normal;
        godot::Vector2 uv;
    };

    struct Triangle {
        godot::Vector3 p0, p1, p2;
        godot::Vector3 n0, n1, n2;
        godot::Vector2 uv0, uv1, uv2;
        int materialId = -1;
    };

    struct Material {
        MaterialType type = MaterialType::Diffuse;
        godot::Color albedo = godot::Color(1.0f, 1.0f, 1.0f, 1.0f);
        godot::Color emission = godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
        float roughness = 0.5f;
        float ior = 1.5f;
    };

    struct Camera {
        godot::Transform3D camera_to_world;
        float fov_y_radians = 0.0f;
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H
