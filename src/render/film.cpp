#include "film.h"

#include <algorithm>

namespace godot_rt {

    namespace {

        godot::Color black() {
            return godot::Color(0.0f, 0.0f, 0.0f, 1.0f);
        }

    }

    Film::Film(godot::Vector2i size) {
        resize(size);
    }

    void Film::resize(godot::Vector2i size) {
        image_size.x = std::max(size.x, 0);
        image_size.y = std::max(size.y, 0);

        const int pixel_count = image_size.x * image_size.y;
        radiance_sum.assign(pixel_count, black());
        sample_count.assign(pixel_count, 0);
    }

    void Film::clear() {
        // 清空累计值但保留当前尺寸，方便相机或场景变化后重新开始采样。
        std::fill(radiance_sum.begin(), radiance_sum.end(), black());
        std::fill(sample_count.begin(), sample_count.end(), 0);
    }

    void Film::add_sample(godot::Vector2i pixel, godot::Color radiance) {
        if (!contains(pixel)) {
            return;
        }

        // Film 保存线性空间 radiance 的累计和，平均、色调映射留给读取或显示阶段。
        const int index = pixel_index(pixel);
        radiance_sum[index].r += radiance.r;
        radiance_sum[index].g += radiance.g;
        radiance_sum[index].b += radiance.b;
        radiance_sum[index].a = 1.0f;
        sample_count[index] += 1;
    }

    godot::Color Film::get_average(godot::Vector2i pixel) const {
        if (!contains(pixel)) {
            return black();
        }

        const int index = pixel_index(pixel);
        const int count = sample_count[index];
        if (count == 0) {
            // 无样本像素返回稳定的黑色，避免调用方额外处理除零。
            return black();
        }

        const float inv_count = 1.0f / static_cast<float>(count);
        return godot::Color(
            radiance_sum[index].r * inv_count,
            radiance_sum[index].g * inv_count,
            radiance_sum[index].b * inv_count,
            1.0f
        );
    }

    int Film::get_sample_count(godot::Vector2i pixel) const {
        if (!contains(pixel)) {
            return 0;
        }

        return sample_count[pixel_index(pixel)];
    }

    godot::Vector2i Film::get_size() const {
        return image_size;
    }

    bool Film::contains(godot::Vector2i pixel) const {
        return pixel.x >= 0 && pixel.y >= 0 &&
               pixel.x < image_size.x && pixel.y < image_size.y;
    }

    int Film::pixel_index(godot::Vector2i pixel) const {
        return pixel.y * image_size.x + pixel.x;
    }

}
