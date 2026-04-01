#include "ray.h"

namespace godot
{
	bool Ray::has_nan() const
	{
		return (Math::is_nan(o.x) || Math::is_nan(o.y) || Math::is_nan(o.z) || Math::is_nan(d.x) || Math::is_nan(d.y) || Math::is_nan(d.z));
	}
}