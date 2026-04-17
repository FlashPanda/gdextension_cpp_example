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
                : pi(p), time(time)/*, medium(medium)*/ {
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
                : pi(p), time(time)/*, medium(medium)*/ {}

            Interaction(Vector3 p, const MediumInterface* medium_interface)
                : pi(p)/*, medium_interface(medium_interface)*/ {}

            Interaction(Vector3 p, real_t time, const MediumInterface* medium_interface)
                : pi(p), time(time)/*, medium_interface(medium_interface)*/ {}

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
                return RayDifferential(offset_ray_origin(d, false), d, time/*, get_medium(d)*/);
            }

            Ray spawn_ray_to(Vector3 p2) const {
                Ray r = godot::spawn_ray_to(pi, n, time, p2);
                // r.medium = get_medium(r.d);
                return r;
            }

            Ray spawn_ray_to(const Interaction& it) const {
                Ray r = godot::spawn_ray_to(pi, n, time, it.pi, it.n);
                // r.medium = get_medium(r.d);
                return r;
            }

            // Medium get_medium(Vector3 w) const {
            //     if (medium_interface)
            //         return w.dot(n) > 0? medium_interface->outside : medium_interface->inside;
            //     return medium;
            // }

            std::string to_string() const;

            Vector3I pi;
            real_t time = 0;
            Vector3 wo;
            Vector3 n;
            Vector2 uv;

            // const MediumInterface* medium_interface = nullptr;
            // Medium medium;// = nullptr;
    };

    class MediumInteraction : public Interaction {
    public:
        MediumInteraction()  {}

        MediumInteraction(Vector3 p, Vector3 wo, real_t time, Medium medium /*,PhaseFunction phase*/)
            : Interaction(p, wo, time, medium, true)/*, phase(phase)*/ {}

        std::string to_string() const
        {
            return "MediumInteraction";
        }

        // use for volume scattering
        // PhaseFunction phase;
    };

    class SurfaceInteraction : public Interaction {
    public:
        SurfaceInteraction() = default;
        SurfaceInteraction(Vector3I pi, Vector2 uv, Vector3 wo, Vector3 dpdu, Vector3 dpdv,
            Vector3 dndu, Vector3 dndv, real_t time, bool flipNormal)
            : Interaction(pi, wo, uv), dpdu(dpdu), dpdv(dpdv), dndu(dndu), dndv(dndv) {} 
            
        
        Vector3 dpdu, dpdv;
        Vector3 dndu, dndv;

        struct {
            Vector3 n;
            Vector3 dpdu, dpdv;
            Vector3 dndu, dndv;
        } shading;

        int face_index = 0;
        Material material;
        // Light area_light;    // 交点是否会发光，只有面积光源才会用到，godot里没有面积光源。
        Vector3 dpdx, dpdy;
        real_t dudx = 0;
        real_t dvdx = 0;
        real_t dudy = 0;
        real_t dvdy = 0;
    };


}

#endif