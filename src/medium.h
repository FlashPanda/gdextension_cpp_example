//
// Created by xuelangyun on 2026/3/31.
//

#ifndef GDEXTENSION_CPP_EXAMPLE_MEDIUM_H
#define GDEXTENSION_CPP_EXAMPLE_MEDIUM_H

#include <variant>

namespace godot
{
    class HomogeneousMedium;
    class GridMedium;
    class RGBGridMedium;
    class CloudMedium;
    class NanoVDBMedium;

    using MediumVariant = std::variant<
        HomogeneousMedium,
        GridMedium,
        RGBGridMedium,
        CloudMedium,
        NanoVDBMedium
    >;

    // 用于进行体积散射
    class Medium
    {
        public:
        Medium() = default;

        template <typename T>
        Medium(T _medium) : medium(std::move(_medium)) {}

        std::string to_string() const;

        // bool is_emissive() const
        // {
        //     return std::visit([](const auto& m) {
        //         return medium.is_emissive();
        //     }, medium);
        // }

        // MediumProperties sample_point(const Vector3 &p,
        //     const SampledWavelengths &lambda) const 
        // {
        //     return std::visit([&](const auto &m) {
        //         return m.sample_point(p, lambda);
        //     }, medium);
        // }

        // RayMajorantIterator sample_ray(
        //     const Ray &ray,
        //     float t_max,
        //     const SampledWavelengths &lambda
        // ) const {
        //     return std::visit([&](const auto &m) {
        //         return m.sample_ray(ray, t_max, lambda);
        //     }, medium);
        // }

    private:
        MediumVariant medium;
    };

    struct MediumInterface {
        // MediumInterface Public Methods
        std::string ToString() const;

        MediumInterface() = default;
        
        MediumInterface(Medium medium) : inside(medium), outside(medium) {}
        
        MediumInterface(Medium inside, Medium outside) : inside(inside), outside(outside) {}

        
        //bool IsMediumTransition() const { return inside != outside; }

        // MediumInterface Public Members
        Medium inside, outside;
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_MEDIUM_H