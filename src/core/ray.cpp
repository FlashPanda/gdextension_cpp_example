#include "ray.h"

#include <sstream>

namespace godot_rt
{
	bool Ray::has_nan() const
	{
		return (godot::Math::is_nan(o.x) || godot::Math::is_nan(o.y) || godot::Math::is_nan(o.z) ||
                godot::Math::is_nan(d.x) || godot::Math::is_nan(d.y) || godot::Math::is_nan(d.z));
	}

    std::string Ray::to_string() const {
        std::ostringstream stream;
        stream << "Ray(o=(" << o.x << ", " << o.y << ", " << o.z << "), d=("
               << d.x << ", " << d.y << ", " << d.z << "), time=" << time << ")";
        return stream.str();
    }

    std::string RayDifferential::to_string() const {
        std::ostringstream stream;
        stream << "RayDifferential(" << Ray::to_string()
               << ", has_differentials=" << has_differentials << ")";
        return stream.str();
    }
}
