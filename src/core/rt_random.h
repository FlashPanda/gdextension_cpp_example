#ifndef GDEXTENSION_CPP_EXAMPLE_INDEX_RT_RANDOM_H
#define GDEXTENSION_CPP_EXAMPLE_INDEX_RT_RANDOM_H

#include <cstdint>

#include <godot_cpp/variant/vector2.hpp>

namespace godot_rt {

    class Rng {
    public:
        explicit Rng(std::uint64_t seed = 1)
            : state(seed == 0 ? 0x9e3779b97f4a7c15ull : seed) {}

        std::uint32_t next_u32() {
            std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
        }

        float next_float() {
            return static_cast<float>((static_cast<double>(next_u32()) + 0.5) *
                                      (1.0 / 4294967296.0));
        }

        godot::Vector2 next_2d() {
            return godot::Vector2(next_float(), next_float());
        }

    private:
        std::uint64_t state;
    };

}

#endif //GDEXTENSION_CPP_EXAMPLE_INDEX_RT_RANDOM_H
