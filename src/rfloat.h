//
// Created by xuelangyun on 2026/3/31.
//

#ifndef GDEXTENSION_CPP_EXAMPLE_RFLOAT_H
#define GDEXTENSION_CPP_EXAMPLE_RFLOAT_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <bit>

#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/math_defs.hpp>

namespace godot
{
    inline uint32_t float_to_bits(float f) {
        return std::bit_cast<uint32_t>(f);
    }

    inline float bits_to_float(uint32_t ui) {
        return std::bit_cast<float>(ui);
    }


    inline float next_float_up(float v) {
        // Handle infinity and negative zero for _NextFloatUp()_
        if (Math::is_inf(v) && v > 0.f)
            return v;
        if (v == -0.f)
            v = 0.f;

        // Advance _v_ to next higher float
        uint32_t ui = float_to_bits(v);
        if (v >= 0)
            ++ui;
        else
            --ui;
        return bits_to_float(ui);
    }

    inline float next_float_down(float v) {
        // Handle infinity and positive zero for _next_float_down()_
        if (Math::is_inf(v) && v < 0.)
            return v;
        if (v == 0.f)
            v = -0.f;
        uint32_t ui = float_to_bits(v);
        if (v > 0)
            --ui;
        else
            ++ui;
        return bits_to_float(ui);
    }

    inline real_t add_round_up(real_t a, real_t b) {
        return next_float_up(a + b);
    }
    inline real_t add_round_down(real_t a, real_t b) {
        return next_float_down(a + b);
    }

    inline real_t sub_round_up(real_t a, real_t b) {
        return add_round_up(a, -b);
    }
    inline real_t sub_round_down(real_t a, real_t b) {
        return add_round_down(a, -b);
    }

    inline real_t mul_round_up(real_t a, real_t b) {
        return next_float_up(a * b);
    }

    inline real_t mul_round_down(real_t a, real_t b) {
        return next_float_down(a * b);
    }

    inline real_t div_round_up(real_t a, real_t b) {
        return next_float_up(a / b);
    }

    inline real_t div_round_down(real_t a, real_t b) {
        return next_float_down(a / b);
    }
}

#endif //GDEXTENSION_CPP_EXAMPLE_RFLOAT_H