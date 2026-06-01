#ifndef GDEXTENSION_CPP_EXAMPLE_FILM_H
#define GDEXTENSION_CPP_EXAMPLE_FILM_H

#include <vector>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace godot_rt {

    class Film {
    public:
        Film() = default;
        explicit Film(godot::Vector2i size);

        void resize(godot::Vector2i size);
        void clear();

        void add_sample(godot::Vector2i pixel, godot::Color radiance);

        godot::Color get_average(godot::Vector2i pixel) const;
        int get_sample_count(godot::Vector2i pixel) const;
        godot::Vector2i get_size() const;

    private:
        bool contains(godot::Vector2i pixel) const;
        int pixel_index(godot::Vector2i pixel) const;

        godot::Vector2i image_size = godot::Vector2i(0, 0);
        std::vector<godot::Color> radiance_sum;
        std::vector<int> sample_count;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_FILM_H
