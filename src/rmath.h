
#ifndef GDEXTENSION_CPP_EXAMPLE_RMATH_H
#define GDEXTENSION_CPP_EXAMPLE_RMATH_H

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <limits>
#include "rfloat.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/core/math_defs.hpp>

namespace godot
{



class Interval {
  public:
    Interval() = default;
    explicit Interval(real_t v) : low(v), high(v) {}
    constexpr Interval(real_t low, real_t high)
        : low(std::min(low, high)), high(std::max(low, high)) {}

    static Interval from_value_and_error(real_t v, real_t err) {
        Interval i;
        if (err == 0)
            i.low = i.high = v;
        else {
            i.low = sub_round_down(v, err);
            i.high = add_round_up(v, err);
        }
        return i;
    }

    Interval &operator=(real_t v) {
        low = high = v;
        return *this;
    }

    real_t upper_bound() const { return high; }
    real_t lower_bound() const { return low; }
    real_t midpoint() const { return (low + high) / 2; }
    real_t width() const { return high - low; }

    real_t operator[](int i) const {
        DEV_ASSERT(i == 0 || i == 1);
        return (i == 0) ? low : high;
    }

    explicit operator real_t() const { return midpoint(); }

    bool exactly(real_t v) const { return low == v && high == v; }

    bool operator==(real_t v) const { return exactly(v); }

    Interval operator-() const { return {-high, -low}; }

    Interval operator+(Interval i) const {
        return {add_round_down(low, i.low), add_round_up(high, i.high)};
    }

    Interval operator-(Interval i) const {
        return {sub_round_down(low, i.high), sub_round_up(high, i.low)};
    }

    Interval operator*(Interval i) const {
        float lp[4] = {mul_round_down(low, i.low), mul_round_down(high, i.low),
                       mul_round_down(low, i.high), mul_round_down(high, i.high)};
        float hp[4] = {mul_round_up(low, i.low), mul_round_up(high, i.low),
                       mul_round_up(low, i.high), mul_round_up(high, i.high)};
        return {std::min({lp[0], lp[1], lp[2], lp[3]}),
                std::max({hp[0], hp[1], hp[2], hp[3]})};
    }

    Interval operator/(Interval i) const;

    bool operator==(Interval i) const {
        return low == i.low && high == i.high;
    }

    bool operator!=(real_t f) const { return f < low || f > high; }

    std::string to_string() const;

    Interval &operator+=(Interval i) {
        *this = Interval(*this + i);
        return *this;
    }
    Interval &operator-=(Interval i) {
        *this = Interval(*this - i);
        return *this;
    }
    Interval &operator*=(Interval i) {
        *this = Interval(*this * i);
        return *this;
    }
    Interval &operator/=(Interval i) {
        *this = Interval(*this / i);
        return *this;
    }
    Interval &operator+=(real_t f) { return *this += Interval(f); }
    Interval &operator-=(real_t f) { return *this -= Interval(f); }
    Interval &operator*=(real_t f) {
        if (f > 0)
            *this = Interval(mul_round_down(f, low), mul_round_up(f, high));
        else
            *this = Interval(mul_round_down(f, high), mul_round_up(f, low));
        return *this;
    }
    Interval &operator/=(real_t f) {
        if (f > 0)
            *this = Interval(div_round_down(low, f), div_round_up(high, f));
        else
            *this = Interval(div_round_down(high, f), div_round_up(low, f));
        return *this;
    }

    static const Interval Pi;

  private:
    //friend struct SOA<Interval>;
    real_t low, high;
};

    inline bool in_range(real_t v, Interval i) {
        return v >= i.lower_bound() && v <= i.upper_bound();
    }
    inline bool in_range(Interval a, Interval b) {
        return a.lower_bound() <= b.upper_bound() && a.upper_bound() >= b.lower_bound();
    }

class Vector3I
{
public:
    Interval x;
    Interval y;
    Interval z;

    Vector3I() = default;
    Vector3I(const Interval &p_x, const Interval &p_y, const Interval &p_z);
    Vector3I(real_t x, real_t y, real_t z);
    explicit Vector3I(const Vector3 &p);

    Vector3I(const Vector3& p, const Vector3& e);

    Vector3 error() const;
    bool is_exact() const;

    // 一元负号。
    Vector3I operator-() const;

    // 点 + 向量。
    Vector3I operator+(Vector3 &p_vec) const;
    Vector3I &operator+=(Vector3 &p_vec);

    // 点 - 点。
    // 注意：从严格几何语义上说，point - point 更像 vector。
    // 但 PBRT 这里返回的是区间坐标差，我们为了迁移兼容性保留这一行为。
    Vector3I operator-(const Vector3I &p_point) const;

    // 点 - 精确点。
    Vector3I operator-(const Vector3 &p_point) const;

    // 点 - 向量。
    Vector3I &operator-=(const Vector3 &p_vec);

private:
    static bool _is_vector3_valid(const Vector3 &p_vec);
};

}

#endif //GDEXTENSION_CPP_EXAMPLE_RMATH_H