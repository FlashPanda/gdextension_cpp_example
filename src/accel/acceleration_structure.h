#ifndef GDEXTENSION_CPP_EXAMPLE_ACCELERATION_STRUCTURE_H
#define GDEXTENSION_CPP_EXAMPLE_ACCELERATION_STRUCTURE_H

#include <memory>
#include <string>

#include "accel_interface.h"

namespace godot_rt {

    enum class AccelerationType {
        BruteForce,
        Bvh,
        Octree,
    };

    const char* acceleration_type_name(AccelerationType type);
    bool parse_acceleration_type(const std::string& value, AccelerationType* out_type);
    std::unique_ptr<AccelInterface> create_acceleration_structure(AccelerationType type);

}

#endif // GDEXTENSION_CPP_EXAMPLE_ACCELERATION_STRUCTURE_H
