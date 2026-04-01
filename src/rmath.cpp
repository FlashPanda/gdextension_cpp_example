#include "rmath.h"

namespace godot
{

    inline Interval Interval::operator/(Interval i) const
    {
        if (in_range(0, i))
            // The interval we're dividing by straddles zero, so just
                // return an interval of everything.
                    return Interval(-std::numeric_limits<real_t>::infinity(), std::numeric_limits<real_t>::infinity());

        real_t lowQuot[4] = {div_round_down(low, i.low), div_round_down(high, i.low),
                            div_round_down(low, i.high), div_round_down(high, i.high)};
        real_t highQuot[4] = {div_round_up(low, i.low), div_round_up(high, i.low),
                             div_round_up(low, i.high), div_round_up(high, i.high)};
        return {std::min({lowQuot[0], lowQuot[1], lowQuot[2], lowQuot[3]}),
                std::max({highQuot[0], highQuot[1], highQuot[2], highQuot[3]})};
    }

Vector3I::Vector3I(const Interval &p_x, const Interval &p_y, const Interval &p_z) {
	x = p_x;
	y = p_y;
	z = p_z;
}

Vector3I::Vector3I(real_t p_x, real_t p_y, real_t p_z) {
	x = Interval(p_x);
	y = Interval(p_y);
	z = Interval(p_z);
}

Vector3I::Vector3I(const Vector3 &p_point) {
	DEV_ASSERT(_is_vector3_valid(p_point));

	x = Interval(p_point.x);
	y = Interval(p_point.y);
	z = Interval(p_point.z);
}

Vector3I::Vector3I(const Vector3 &p_point, const Vector3 &p_error) {
	DEV_ASSERT(_is_vector3_valid(p_point));
	DEV_ASSERT(_is_vector3_valid(p_error));

	x = Interval::from_value_and_error(p_point.x, p_error.x);
	y = Interval::from_value_and_error(p_point.y, p_error.y);
	z = Interval::from_value_and_error(p_point.z, p_error.z);
}

Vector3 Vector3I::error() const {
	return Vector3(
			x.width() * (real_t)0.5,
			y.width() * (real_t)0.5,
			z.width() * (real_t)0.5);
}

bool Vector3I::is_exact() const {
	return x.width() == (real_t)0 &&
			y.width() == (real_t)0 &&
			z.width() == (real_t)0;
}

Vector3I Vector3I::operator-() const {
	return Vector3I(-x, -y, -z);
}

Vector3I Vector3I::operator+(Vector3 &p_vec) const {
	DEV_ASSERT(_is_vector3_valid(p_vec));
	real_t p_x = p_vec.x;
	real_t p_y = p_vec.y;
	real_t p_z = p_vec.z;
	Interval x_i(p_x);
	Interval y_i(p_y);
	Interval z_i(p_z);
	return Vector3I(
			x + x_i,
			y + y_i,
			z + z_i);
}

Vector3I &Vector3I::operator+=(Vector3 &p_vec) {
	DEV_ASSERT(_is_vector3_valid(p_vec));
	real_t p_x = p_vec.x;
	real_t p_y = p_vec.y;
	real_t p_z = p_vec.z;
	x += p_x;
	y += p_y;
	z += p_z;
	return *this;
}

Vector3I Vector3I::operator-(const Vector3I &p_point) const {
	DEV_ASSERT(!p_point.has_nan());

	return Vector3I(
			x - p_point.x,
			y - p_point.y,
			z - p_point.z);
}

Vector3I Vector3I::operator-(const Vector3 &p_point) const {
	DEV_ASSERT(_is_vector3_valid(p_point));
	real_t p_x = p_point.x;
	real_t p_y = p_point.y;
	real_t p_z = p_point.x;
	Interval x_i(p_x);
	Interval y_i(p_y);
	Interval z_i(p_z);

	return Vector3I(
			x - x_i,
			y - y_i,
			z - z_i);
}

Vector3I &Vector3I::operator-=(const Vector3 &p_vec) {
	DEV_ASSERT(_is_vector3_valid(p_vec));

	x -= p_vec.x;
	y -= p_vec.y;
	z -= p_vec.z;
	return *this;
}

bool Vector3I::_is_vector3_valid(const Vector3 &p_vec) {
	return !Math::is_nan(p_vec.x) &&
			!Math::is_nan(p_vec.y) &&
			!Math::is_nan(p_vec.z) &&
			!Math::is_inf(p_vec.x) &&
			!Math::is_inf(p_vec.y) &&
			!Math::is_inf(p_vec.z);
}

bool Vector3I::has_nan() const
{
	return x.has_nan() || y.has_nan() || z.has_nan();
}
}