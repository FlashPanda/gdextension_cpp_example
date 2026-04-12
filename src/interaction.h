#ifndef GDEXTENSION_CPP_EXAMPLE_INTERACTION_H
#define GDEXTENSION_CPP_EXAMPLE_INTERACTION_H

#include "rmath.h"
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include "medium.h"
#include "ray.h"

namespace godot
{
    class Interaction {
        public:
            Interaction() = default;

            Interaction(Vector3I pi, Vector3 n, Vector2 uv, Vector3 wo, real_t time)
                : pi(pi), n(n), uv(uv), wo(wo), time(time) {}

            Vector3 p() const {return pi.to_vector3();}

            bool is_surface_interaction() const {return  n != Vector3(0, 0, 0);}

            bool is_medium_interaction() const {return !is_surface_interaction();}

            const SurfaceInteraction& as_surface() const {
                DEV_ASSERT(is_surface_interaction());
                return (const SurfaceInteraction&)*this;
            }

            SurfaceInteraction& as_surface() {
                DEV_ASSERT(is_surface_interaction());
                return (SurfaceInteraction&)*this;
            }

            Interaction(Vector3 p, Vector3 wo, real_t time, Medium medium, bool is_wo = true)
                : pi(p), time(time), medium(medium) {
                    if (is_wo)
                        wo = wo;
                    else 
                        n = wo;
            }

            Interaction(Vector3 p, Vector2 uv)
                : pi(p), uv(uv) {}

            Interaction(const Vector3I& pi, Vector3 n, real_t time = 0, Vector2 uv = {})
                : pi(pi), n(n), time(time), uv(uv) {}

            Interaction(const Vector3I& pi, Vector3 n, Vector2 uv)
                : pi(pi), n(n), uv(uv) {}
            Interaction(Vector3 p, real_t time, Medium medium)
                : pi(p), time(time), medium(medium) {}

            Interaction(Vector3 p, const MediumInterface* medium_interface)
                : pi(p), medium_interface(medium_interface) {}

            Interaction(Vector3 p, real_t time, const MediumInterface* medium_interface)
                : pi(p), time(time), medium_interface(medium_interface) {}

            const MediumInteraction& as_medium() const {
                DEV_ASSERT(is_medium_interaction());
                return (const MediumInteraction &)*this;
            }

            MediumInteraction& as_medium() {
                DEV_ASSERT(is_medium_interaction());
                return (MediumInteraction &)*this;
            }

            Vector3 offset_ray_origin(Vector3 t, bool is_pt) const {
                Vector3 offset;
                if (is_pt){
                    offset = t - p();
                }
                else {
                    offset = t;
                }

                return godot::offset_ray_origin(pi, n, offset);
            }

            RayDifferential spawn_ray(Vector3 d) const {
                return RayDifferential(offset_ray_origin(d, false), d, time, get_medium(d));
            }

            Ray spawn_ray_to(Vector3 p2) const {
                Ray r = godot::spawn_ray_to(pi, n, time, p2);
                r.medium = get_medium(r.d)
                return r;
            }

            Ray spawn_ray_to(const Interaction& it) const {
                Ray r = godot::spawn_ray_to(pi, n, time, it.pi, it.n);
                r.medium = get_medium(r.d);
                return r;
            }

            Medium get_medium(Vector3 w) const {
                if (medium_interface)
                    return w.dot(n) > 0? medium_interface->outside : medium_interface->inside;
                return medium;
            }

            std::string to_string() const;

            Vector3I pi;
            real_t time = 0;
            Vector3 wo;
            Vector3 n;
            Vector2 uv;

            const MediumInterface* medium_interface = nullptr;
            Medium medium;// = nullptr;
    };

    class MediumInteraction : public Interaction {
    public:
        MediumInteraction() 
    };

    class SurfaceInteraction : public Interaction {
    };


}

#endif