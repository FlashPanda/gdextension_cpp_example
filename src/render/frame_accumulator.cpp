#include "frame_accumulator.h"

#include <algorithm>

namespace godot_rt {

    namespace {

        int ceil_div(int value, int divisor) {
            return divisor > 0 ? (value + divisor - 1) / divisor : 0;
        }

        unsigned int morton_order(int x, int y) {
            unsigned int order = 0;
            for (int bit = 0; bit < 16; ++bit) {
                order |= static_cast<unsigned int>((x >> bit) & 1) << (bit * 2);
                order |= static_cast<unsigned int>((y >> bit) & 1) << (bit * 2 + 1);
            }
            return order;
        }

    }

    FrameAccumulator::FrameAccumulator(godot::Vector2i image_size, godot::Vector2i tile_size) {
        resize(image_size, tile_size);
    }

    void FrameAccumulator::resize(godot::Vector2i new_image_size, godot::Vector2i new_tile_size) {
        image_size.x = std::max(new_image_size.x, 0);
        image_size.y = std::max(new_image_size.y, 0);
        tile_size.x = std::max(new_tile_size.x, 1);
        tile_size.y = std::max(new_tile_size.y, 1);

        rebuild_tiles();
        reset();
    }

    void FrameAccumulator::reset() {
        next_tile_index = 0;
        pass_index = 0;
    }

    void FrameAccumulator::clear() {
        // clear 表示释放当前帧调度内容；只想重新从第 0 pass 开始时使用 reset。
        resize(godot::Vector2i(0, 0), tile_size);
    }

    bool FrameAccumulator::next_tile(Tile* out_tile) {
        if (out_tile == nullptr || tile_order.empty()) {
            return false;
        }

        if (next_tile_index >= static_cast<int>(tile_order.size())) {
            // 当前 pass 的所有 tile 都发完后，进入下一轮渐进式采样。
            next_tile_index = 0;
            pass_index += 1;
        }

        *out_tile = make_tile(tile_order[next_tile_index]);
        next_tile_index += 1;
        return true;
    }

    int FrameAccumulator::get_pass_index() const {
        return pass_index;
    }

    godot::Vector2i FrameAccumulator::get_image_size() const {
        return image_size;
    }

    void FrameAccumulator::rebuild_tiles() {
        tile_count.x = ceil_div(image_size.x, tile_size.x);
        tile_count.y = ceil_div(image_size.y, tile_size.y);

        tile_order.clear();
        tile_order.reserve(tile_count.x * tile_count.y);

        for (int y = 0; y < tile_count.y; ++y) {
            for (int x = 0; x < tile_count.x; ++x) {
                tile_order.push_back(TileCoord{x, y, morton_order(x, y)});
            }
        }

        // Morton 顺序能比简单行扫描更快触达画面不同区域，适合渐进式预览。
        std::sort(tile_order.begin(), tile_order.end(), [](const TileCoord& a, const TileCoord& b) {
            if (a.order != b.order) {
                return a.order < b.order;
            }
            if (a.y != b.y) {
                return a.y < b.y;
            }
            return a.x < b.x;
        });
    }

    Tile FrameAccumulator::make_tile(const TileCoord& coord) const {
        Tile tile;
        tile.origin = godot::Vector2i(coord.x * tile_size.x, coord.y * tile_size.y);

        const int remaining_x = image_size.x - tile.origin.x;
        const int remaining_y = image_size.y - tile.origin.y;
        // 边缘 tile 可能不满一个标准 tile，需要裁剪到图像范围内。
        tile.size = godot::Vector2i(
            std::min(tile_size.x, remaining_x),
            std::min(tile_size.y, remaining_y)
        );
        tile.pass_index = pass_index;
        return tile;
    }

}
