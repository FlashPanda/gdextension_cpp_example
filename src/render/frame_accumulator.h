#ifndef GDEXTENSION_CPP_EXAMPLE_FRAME_ACCUMULATOR_H
#define GDEXTENSION_CPP_EXAMPLE_FRAME_ACCUMULATOR_H

#include <vector>

#include <godot_cpp/variant/vector2i.hpp>

namespace godot_rt {

    struct Tile {
        godot::Vector2i origin = godot::Vector2i(0, 0);
        godot::Vector2i size = godot::Vector2i(0, 0);
        int pass_index = 0;
    };

    class FrameAccumulator {
    public:
        FrameAccumulator() = default;
        FrameAccumulator(godot::Vector2i image_size, godot::Vector2i tile_size = godot::Vector2i(16, 16));

        void resize(godot::Vector2i image_size, godot::Vector2i tile_size = godot::Vector2i(16, 16));
        void reset();
        void clear();

        bool next_tile(Tile* out_tile);

        int get_pass_index() const;
        godot::Vector2i get_image_size() const;

    private:
        struct TileCoord {
            int x = 0;
            int y = 0;
            unsigned int order = 0;
        };

        void rebuild_tiles();
        Tile make_tile(const TileCoord& coord) const;

        godot::Vector2i image_size = godot::Vector2i(0, 0);
        godot::Vector2i tile_size = godot::Vector2i(16, 16);
        godot::Vector2i tile_count = godot::Vector2i(0, 0);
        std::vector<TileCoord> tile_order;
        int next_tile_index = 0;
        int pass_index = 0;
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_FRAME_ACCUMULATOR_H
