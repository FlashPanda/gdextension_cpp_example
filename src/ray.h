// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef GDEXTENSION_CPP_EXAMPLE_RAY_H
#define GDEXTENSION_CPP_EXAMPLE_RAY_H


#include <godot_cpp/variant/vector3.hpp>
#include "medium.h"
#include "rmath.h"
#include "rfloat.h"

#include <string>

namespace godot {

    class Ray {
    public:
        bool has_nan() const;

        std::string to_string() const;

        Vector3 operator()(float t) const { return o + d * t; }

        Ray() = default;
        Ray(Vector3 o, Vector3 d, float time = 0.f, Medium medium = Medium())
            : o(o), d(d), time(time), medium(medium) {}

        // Ray Public Members
        Vector3 o;	// origin point
        Vector3 d;	// direction vector
        float time = 0;
        Medium medium;// = nullptr;
    };

    class RayDifferential : public Ray{

    };

    inline Vector3 offset_ray_origin(Vector3I pi, Vector3 n, Vector3 w) {
        // Find vector _offset_ to corner of error bounds and compute initial _po_
        real_t d = n.abs().dot(pi.error());
        Vector3 offset = d * Vector3(n);
        if (w.dot(n) < 0)
            offset = -offset;
		Vector3 pi_mid(pi.x.midpoint(), pi.y.midpoint(), pi.z.midpoint());
        Vector3 po = pi_mid + offset;

        // Round offset point _po_ away from _p_
        for (int i = 0; i < 3; ++i) {
            if (offset[i] > 0)
                po[i] = next_float_up(po[i]);
            else if (offset[i] < 0)
                po[i] = next_float_down(po[i]);
        }

        return po;
    }

    inline Ray spawn_ray(Vector3I pi, Vector3 n, real_t time, Vector3 d) {
        return Ray(offset_ray_origin(pi, n, d), d, time);
    }

    inline Ray spawn_ray_to(Vector3I p_from, Vector3 n, real_t time, Vector3 p_to) {
		Vector3 p_mid(p_from.x.midpoint(), p_from.y.midpoint(), p_from.z.midpoint());
        Vector3 d = p_to - p_mid;
        return spawn_ray(p_from, n, time, d);
    }

    inline Ray spawn_ray_to(Vector3I p_from, Vector3 n_from, real_t time,
                                       Vector3I p_to, Vector3 n_to) {
		Vector3 p_to_mid(p_to.x.midpoint(), p_to.y.midpoint(), p_to.z.midpoint());
		Vector3 p_from_mid(p_from.x.midpoint(), p_from.y.midpoint(), p_from.z.midpoint());
        Vector3 pf = offset_ray_origin(p_from, n_from, p_to_mid - p_from_mid);
        Vector3 pt = offset_ray_origin(p_to, n_to, pf - p_to_mid);
        return Ray(pf, pt - pf, time);
    }

}  // namespace godot

#endif
