//
// Created by caizh on 2026/5/21.
//

#ifndef GDEXTENSION_CPP_EXAMPLE_INDEX_RT_LIGHT_H
#define GDEXTENSION_CPP_EXAMPLE_INDEX_RT_LIGHT_H

#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>

#include "rt_color.h"
#include "rt_math.h"

#include <string>

namespace godot_rt {

    class Light {
    public:
        Light(const godot::Transform3D& render_from_light);

        godot::Color l(godot::Vector3 p, godot::Vector3 n, godot::Vector2 uv, godot::Vector3 w) const {
            return godot::Color(0.f, 0.f, 0.f, 0.f);
        }

        godot::Color le(const Ray& ray) {
            return godot::Color(0.f, 0.f, 0.f, 0.f);
        }

    protected:
        std::string base_to_string() const;


        godot::Transform3D render_from_light;
    };

    class OmniLight : public Light {
    public:
        OmniLight(const godot::Transform3D& render_from_light, godot::Color c, real_t i)
            : Light(render_from_light), color(c), intensity(i)
        {}

        godot::Color sample_le(godot::Vector2 u1, godot::Vector2 u2, real_t time) const;

        void pdf_le(const Ray& real_t* pdf_pos, real_t* pdf_dir) const;

        void sample_li() const {

        }

        std::string to_string() const;

        real_t pdf_li() const;

    private:
        godot::Color color;
        real_t intensity;
    };

    class DirectionalLight : public Light {
    public:
        DirectionalLight(const godot::Transform3D& render_from_light, godot::Color c, real_t i)
            : Light(render_from_light), color(c), intensity(i)
        {}

        real_t pdf_li() {
            return 0;
        }

        godot::Color sample_le(godot::Vector2 u1, godot::Vector2 u2, real_t time) const;

        void pdf_le(const Ray&, real_t* pdf_pos, real_t* pdf_dir) const;

        std::string to_string() const;

        void sample_li() const {

        }

    private:
        godot::Color color;
        real_t intensity;
    };

    class SpotLight : public Light {
    public:
        SpotLight(const godot::Transform3D& render_from_light, godot::Color c, real_t i, real_t total_width, real_t falloff_start);

        real_t pdf_li(godot::Vector3) const;

        godot::Color sample_le(godot::Vector2 u1, godot::Vector2 u2, real_t time) const;

        void pdf_le(const Ray&, real_t* pdf_pos, real_t* pdf_dir) const;

        std::string to_string() const;

        void sample_li() const {

        }

    private:
        godot::Color color;
        real_t intensity;
        real_t cos_falloff_start, cos_falloff_end;
    };

}


#endif //GDEXTENSION_CPP_EXAMPLE_INDEX_RT_LIGHT_H
