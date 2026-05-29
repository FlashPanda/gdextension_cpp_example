// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef GDEXTENSION_CPP_EXAMPLE_RAY_H
#define GDEXTENSION_CPP_EXAMPLE_RAY_H


#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include "rt_math.h"

#include <string>

namespace godot_rt {

    class Ray {
    public:
        bool has_nan() const;

        std::string to_string() const;

        godot::Vector3 operator()(float t) const { return o + d * t; }

        Ray() = default;
        Ray(godot::Vector3 o, godot::Vector3 d, real_t time = 0.0)
            : o(o), d(d), time(time)
        {

        }

        // Ray Public Members
        godot::Vector3 o;	// origin point
        godot::Vector3 d;	// direction vector
        real_t time = 0.0;
    };

    class RayDifferential : public Ray{
    public:
        RayDifferential() = default;
        RayDifferential(godot::Vector3 o, godot::Vector3 d, real_t time = 0.0)
            : Ray(o, d, time) {}
        explicit RayDifferential(const Ray &ray) : Ray(ray) {}

        void scale_differentials(real_t s) {
            rx_origin = o + (rx_origin - o) * s;
            ry_origin = o + (ry_origin - o) * s;
            rx_direction = d + (rx_direction - d) * s;
            ry_direction = d + (ry_direction - d) * s;
        }

        bool has_nan() const {
            return Ray::has_nan() || (
                has_differentials && (
                    godot::Math::is_nan(rx_origin.x) || godot::Math::is_nan(rx_origin.y) || godot::Math::is_nan(rx_origin.z) ||
                    godot::Math::is_nan(ry_origin.x) || godot::Math::is_nan(ry_origin.y) || godot::Math::is_nan(ry_origin.z) ||
                    godot::Math::is_nan(rx_direction.x) || godot::Math::is_nan(rx_direction.y) || godot::Math::is_nan(rx_direction.z) ||
                    godot::Math::is_nan(ry_direction.x) || godot::Math::is_nan(ry_direction.y) || godot::Math::is_nan(ry_direction.z)
                )
            );
        }
        
        std::string to_string() const;

        bool has_differentials = false;
        godot::Vector3 rx_origin, ry_origin;
        godot::Vector3 rx_direction, ry_direction;
    };

    inline Ray spawn_ray(godot::Vector3 origin, godot::Vector3 d, real_t time = 0.0) {
        return Ray(origin, d, time);
    }

    inline Ray spawn_ray_to(godot::Vector3 p_from, godot::Vector3 p_to, real_t time = 0.0) {
        return Ray(p_from, p_to - p_from, time);
    }

}  // namespace godot_rt

#endif
