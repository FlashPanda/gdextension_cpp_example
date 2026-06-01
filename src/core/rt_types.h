#ifndef GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H
#define GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H

#include <algorithm>
#include <cmath>
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

    enum class CameraFovAxis : std::uint8_t {
        Vertical,
        Horizontal
    };

    struct Camera {
        godot::Transform3D camera_to_world;
        float fov_y_radians = 0.0f;
        CameraFovAxis fov_axis = CameraFovAxis::Vertical;

        RayDifferential generate_primary_ray(godot::Vector2 pixel_sample, godot::Vector2i image_size) const {
            const float width = static_cast<float>(std::max(image_size.x, 1));
            const float height = static_cast<float>(std::max(image_size.y, 1));
            const float aspect = width / height;

            const float tan_fov = std::tan(fov_y_radians * 0.5f);
            float tan_x = tan_fov * aspect;
            float tan_y = tan_fov;
            if (fov_axis == CameraFovAxis::Horizontal) {
                tan_x = tan_fov;
                tan_y = tan_fov / aspect;
            }

            const float ndc_x = (pixel_sample.x / width) * 2.0f - 1.0f;
            const float ndc_y = 1.0f - (pixel_sample.y / height) * 2.0f;

            godot::Vector3 local_direction(ndc_x * tan_x, ndc_y * tan_y, -1.0f);
            if (local_direction.length_squared() == 0.0f) {
                local_direction = godot::Vector3(0.0f, 0.0f, -1.0f);
            }
            local_direction.normalize();

            godot::Vector3 world_direction = camera_to_world.basis.xform(local_direction);
            if (world_direction.length_squared() == 0.0f) {
                world_direction = camera_to_world.basis.xform(godot::Vector3(0.0f, 0.0f, -1.0f));
            }
            world_direction.normalize();

            return RayDifferential(camera_to_world.origin, world_direction);
        }
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H
