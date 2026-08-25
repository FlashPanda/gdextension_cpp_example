#include "acceleration_structure.h"

#include <algorithm>
#include <cctype>

#include "brute_force_accel.h"
#include "bvh_accel.h"
#include "octree_accel.h"

namespace godot_rt {

const char* acceleration_type_name(AccelerationType type) {
    switch (type) {
        case AccelerationType::Bvh:
            return "bvh";
        case AccelerationType::Octree:
            return "octree";
        case AccelerationType::BruteForce:
        default:
            return "brute_force";
    }
}

bool parse_acceleration_type(const std::string& value, AccelerationType* out_type) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    AccelerationType type = AccelerationType::BruteForce;
    if (normalized == "brute_force" || normalized == "none") {
        type = AccelerationType::BruteForce;
    } else if (normalized == "bvh") {
        type = AccelerationType::Bvh;
    } else if (normalized == "octree" || normalized == "oct") {
        type = AccelerationType::Octree;
    } else {
        return false;
    }

    if (out_type != nullptr) {
        *out_type = type;
    }
    return true;
}

std::unique_ptr<AccelInterface> create_acceleration_structure(AccelerationType type) {
    switch (type) {
        case AccelerationType::Bvh:
            return std::make_unique<BvhAccel>();
        case AccelerationType::Octree:
            return std::make_unique<OctreeAccel>();
        case AccelerationType::BruteForce:
        default:
            return std::make_unique<BruteForceAccel>();
    }
}

}
