#include "video/i420_highlight.h"

#include <algorithm>
#include <cstdint>

namespace yuvmix {
namespace {

const uint8_t kHighlightY = 143;

bool ValidOutput(const MutableI420ImageView& output) {
    if (output.width < 2 || output.height < 2 ||
        (output.width & 1u) != 0 || (output.height & 1u) != 0 ||
        output.y.data == NULL || output.u.data == NULL ||
        output.v.data == NULL || output.y.stride <= 0 ||
        output.u.stride <= 0 || output.v.stride <= 0 ||
        static_cast<uint32_t>(output.y.stride) < output.width ||
        static_cast<uint32_t>(output.u.stride) < output.width / 2 ||
        static_cast<uint32_t>(output.v.stride) < output.width / 2) {
        return false;
    }

    const size_t y_required =
        static_cast<size_t>(output.y.stride) * (output.height - 1) +
        output.width;
    const size_t chroma_rows = output.height / 2;
    const size_t u_required =
        static_cast<size_t>(output.u.stride) * (chroma_rows - 1) +
        output.width / 2;
    const size_t v_required =
        static_cast<size_t>(output.v.stride) * (chroma_rows - 1) +
        output.width / 2;
    return output.y.size >= y_required && output.u.size >= u_required &&
           output.v.size >= v_required;
}

bool ValidRect(const Rect& rect,
               uint32_t output_width,
               uint32_t output_height) {
    return (rect.x & 1u) == 0 && (rect.y & 1u) == 0 &&
           rect.w >= 2 && rect.h >= 2 &&
           (rect.w & 1u) == 0 && (rect.h & 1u) == 0 &&
           rect.w <= output_width && rect.h <= output_height &&
           rect.x <= output_width - rect.w &&
           rect.y <= output_height - rect.h;
}

void FillBand(MutablePlane* y_plane,
              int64_t left,
              int64_t top,
              int64_t right,
              int64_t bottom) {
    if (left >= right || top >= bottom) {
        return;
    }

    const size_t width = static_cast<size_t>(right - left);
    for (int64_t y = top; y < bottom; ++y) {
        uint8_t* row =
            y_plane->data +
            static_cast<size_t>(y) * static_cast<size_t>(y_plane->stride) +
            static_cast<size_t>(left);
        std::fill_n(row, width, kHighlightY);
    }
}

}  // namespace

uint32_t BorderWidth(uint32_t cell_height) {
    const uint64_t scaled =
        (3u * static_cast<uint64_t>(cell_height) + 125u) / 250u;
    return static_cast<uint32_t>(std::max<uint64_t>(2u, scaled));
}

MixYuvStatus DrawHighlights(const Rect* rects,
                            size_t rect_count,
                            MutableI420ImageView* output) {
    if (output == NULL || (rects == NULL && rect_count != 0) ||
        !ValidOutput(*output)) {
        return MixYuvStatus::kInvalidArgument;
    }

    for (size_t i = 0; i < rect_count; ++i) {
        if (!ValidRect(rects[i], output->width, output->height)) {
            return MixYuvStatus::kInvalidArgument;
        }
    }

    for (size_t i = 0; i < rect_count; ++i) {
        const Rect& rect = rects[i];
        const int64_t border_width = BorderWidth(rect.h);
        const int64_t inside_width = (border_width + 1) / 2;
        const int64_t outside_width = border_width / 2;

        const int64_t outer_left = std::max<int64_t>(
            0, static_cast<int64_t>(rect.x) - outside_width);
        const int64_t outer_top = std::max<int64_t>(
            0, static_cast<int64_t>(rect.y) - outside_width);
        const int64_t outer_right = std::min<int64_t>(
            output->width,
            static_cast<int64_t>(rect.x) + rect.w + outside_width);
        const int64_t outer_bottom = std::min<int64_t>(
            output->height,
            static_cast<int64_t>(rect.y) + rect.h + outside_width);
        const int64_t inner_left =
            static_cast<int64_t>(rect.x) + inside_width;
        const int64_t inner_top =
            static_cast<int64_t>(rect.y) + inside_width;
        const int64_t inner_right =
            static_cast<int64_t>(rect.x) + rect.w - inside_width;
        const int64_t inner_bottom =
            static_cast<int64_t>(rect.y) + rect.h - inside_width;

        const int64_t clipped_inner_left =
            std::max(outer_left, std::min(outer_right, inner_left));
        const int64_t clipped_inner_top =
            std::max(outer_top, std::min(outer_bottom, inner_top));
        const int64_t clipped_inner_right =
            std::max(outer_left, std::min(outer_right, inner_right));
        const int64_t clipped_inner_bottom =
            std::max(outer_top, std::min(outer_bottom, inner_bottom));

        if (clipped_inner_left >= clipped_inner_right ||
            clipped_inner_top >= clipped_inner_bottom) {
            FillBand(&output->y, outer_left, outer_top,
                     outer_right, outer_bottom);
        } else {
            FillBand(&output->y, outer_left, outer_top,
                     outer_right, clipped_inner_top);
            FillBand(&output->y, outer_left, clipped_inner_bottom,
                     outer_right, outer_bottom);
            FillBand(&output->y, outer_left, clipped_inner_top,
                     clipped_inner_left, clipped_inner_bottom);
            FillBand(&output->y, clipped_inner_right, clipped_inner_top,
                     outer_right, clipped_inner_bottom);
        }
    }

    return MixYuvStatus::kOk;
}

}  // namespace yuvmix
