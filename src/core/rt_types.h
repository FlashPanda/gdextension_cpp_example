#ifndef GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H
#define GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H

namespace godot_rt
{
    enum class MaterialType : uint8_t {
        Diffuse,
        Mirror,
        Dielectric,
        Emissive
    };
}

#endif //GDEXTENSION_CPP_EXAMPLE_RT_TYPES_H