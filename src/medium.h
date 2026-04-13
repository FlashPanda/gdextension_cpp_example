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

    class Medium
    {
        public:
        Medium() = default;

        template <typename T>
        Medium(T _medium) : medium(std::move(_medium)) {}

        std::string to_string() const;

        bool is_emissive() const;

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