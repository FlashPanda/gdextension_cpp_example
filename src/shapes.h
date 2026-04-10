#ifndef GDEXTENSION_CPP_EXAMPLE_SHAPES_H
#define GDEXTENSION_CPP_EXAMPLE_SHAPES_H

#include <godot_cpp/core/math_defs.hpp>
#include <string>
#include "interaction.h"

namespace godot
{
    struct ShapeSample {
        Interaction intr;
        real_t pdf;
        std::string to_string() const;
    };
}

#endif
