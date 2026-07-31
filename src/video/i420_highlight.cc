#include "video/i420_highlight.h"

#include <algorithm>
#include <cstdint>

namespace yuvmix {
namespace {

const uint8_t kHighlightY = 143;
const uint8_t kHighlightU = 113;
const uint8_t kHighlightV = 35;

struct BorderGeometry {
    int64_t outer_left;
    int64_t outer_top;
    int64_t outer_right;
    int64_t outer_bottom;
    int64_t inner_left;
    int64_t inner_top;
    int64_t inner_right;
    int64_t inner_bottom;
};

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

BorderGeometry MakeBorderGeometry(const Rect& rect,
                                  uint32_t output_width,
                                  uint32_t output_height) {
    const int64_t border_width = BorderWidth(rect.h);
    const int64_t inside_width = (border_width + 1) / 2;
    const int64_t outside_width = border_width / 2;

    BorderGeometry geometry;
    geometry.outer_left = std::max<int64_t>(
        0, static_cast<int64_t>(rect.x) - outside_width);
    geometry.outer_top = std::max<int64_t>(
        0, static_cast<int64_t>(rect.y) - outside_width);
    geometry.outer_right = std::min<int64_t>(
        output_width,
        static_cast<int64_t>(rect.x) + rect.w + outside_width);
    geometry.outer_bottom = std::min<int64_t>(
        output_height,
        static_cast<int64_t>(rect.y) + rect.h + outside_width);
    geometry.inner_left = std::max(
        geometry.outer_left,
        std::min(geometry.outer_right,
                 static_cast<int64_t>(rect.x) + inside_width));
    geometry.inner_top = std::max(
        geometry.outer_top,
        std::min(geometry.outer_bottom,
                 static_cast<int64_t>(rect.y) + inside_width));
    geometry.inner_right = std::max(
        geometry.outer_left,
        std::min(geometry.outer_right,
                 static_cast<int64_t>(rect.x) + rect.w - inside_width));
    geometry.inner_bottom = std::max(
        geometry.outer_top,
        std::min(geometry.outer_bottom,
                 static_cast<int64_t>(rect.y) + rect.h - inside_width));
    return geometry;
}

bool ContainsBorderPixel(const BorderGeometry& geometry,
                         int64_t x,
                         int64_t y) {
    if (x < geometry.outer_left || x >= geometry.outer_right ||
        y < geometry.outer_top || y >= geometry.outer_bottom) {
        return false;
    }
    return x < geometry.inner_left || x >= geometry.inner_right ||
           y < geometry.inner_top || y >= geometry.inner_bottom;
}

uint32_t ChromaCoverage(const Rect* rects,
                        size_t rect_count,
                        uint32_t output_width,
                        uint32_t output_height,
                        int64_t chroma_x,
                        int64_t chroma_y) {
    uint32_t coverage = 0;
    for (int64_t dy = 0; dy < 2; ++dy) {
        for (int64_t dx = 0; dx < 2; ++dx) {
            const int64_t luma_x = chroma_x * 2 + dx;
            const int64_t luma_y = chroma_y * 2 + dy;
            for (size_t i = 0; i < rect_count; ++i) {
                if (ContainsBorderPixel(
                        MakeBorderGeometry(
                            rects[i], output_width, output_height),
                        luma_x, luma_y)) {
                    ++coverage;
                    break;
                }
            }
        }
    }
    return coverage;
}

uint8_t BlendHighlightChroma(uint8_t old_value,
                             uint8_t highlight_value,
                             uint32_t coverage) {
    const uint32_t sum = coverage * highlight_value +
                         (4u - coverage) * old_value;
    return static_cast<uint8_t>((sum + 2u) / 4u);
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

void BlendChromaBand(const Rect* rects,
                     size_t rect_count,
                     size_t owner_index,
                     int64_t left,
                     int64_t top,
                     int64_t right,
                     int64_t bottom,
                     MutableI420ImageView* output) {
    for (int64_t y = top; y < bottom; ++y) {
        for (int64_t x = left; x < right; ++x) {
            if (ChromaCoverage(rects, owner_index,
                               output->width, output->height, x, y) != 0) {
                continue;
            }

            const uint32_t coverage =
                ChromaCoverage(rects, rect_count,
                               output->width, output->height, x, y);
            if (coverage == 0) {
                continue;
            }
            const size_t u_offset =
                static_cast<size_t>(y) * output->u.stride + x;
            const size_t v_offset =
                static_cast<size_t>(y) * output->v.stride + x;
            output->u.data[u_offset] = BlendHighlightChroma(
                output->u.data[u_offset], kHighlightU, coverage);
            output->v.data[v_offset] = BlendHighlightChroma(
                output->v.data[v_offset], kHighlightV, coverage);
        }
    }
}

void BlendChromaBorder(const Rect* rects,
                       size_t rect_count,
                       size_t owner_index,
                       MutableI420ImageView* output) {
    const BorderGeometry geometry = MakeBorderGeometry(
        rects[owner_index], output->width, output->height);
    const int64_t outer_left = geometry.outer_left / 2;
    const int64_t outer_top = geometry.outer_top / 2;
    const int64_t outer_right = (geometry.outer_right + 1) / 2;
    const int64_t outer_bottom = (geometry.outer_bottom + 1) / 2;
    const int64_t inner_left = std::max(
        outer_left,
        std::min(outer_right, (geometry.inner_left + 1) / 2));
    const int64_t inner_top = std::max(
        outer_top,
        std::min(outer_bottom, (geometry.inner_top + 1) / 2));
    const int64_t inner_right = std::max(
        outer_left,
        std::min(outer_right, geometry.inner_right / 2));
    const int64_t inner_bottom = std::max(
        outer_top,
        std::min(outer_bottom, geometry.inner_bottom / 2));

    if (inner_left >= inner_right || inner_top >= inner_bottom) {
        BlendChromaBand(rects, rect_count, owner_index,
                        outer_left, outer_top, outer_right, outer_bottom,
                        output);
        return;
    }

    BlendChromaBand(rects, rect_count, owner_index,
                    outer_left, outer_top, outer_right, inner_top, output);
    BlendChromaBand(rects, rect_count, owner_index,
                    outer_left, inner_bottom, outer_right, outer_bottom,
                    output);
    BlendChromaBand(rects, rect_count, owner_index,
                    outer_left, inner_top, inner_left, inner_bottom, output);
    BlendChromaBand(rects, rect_count, owner_index,
                    inner_right, inner_top, outer_right, inner_bottom, output);
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
        const BorderGeometry geometry = MakeBorderGeometry(
            rect, output->width, output->height);

        if (geometry.inner_left >= geometry.inner_right ||
            geometry.inner_top >= geometry.inner_bottom) {
            FillBand(&output->y,
                     geometry.outer_left, geometry.outer_top,
                     geometry.outer_right, geometry.outer_bottom);
        } else {
            FillBand(&output->y,
                     geometry.outer_left, geometry.outer_top,
                     geometry.outer_right, geometry.inner_top);
            FillBand(&output->y,
                     geometry.outer_left, geometry.inner_bottom,
                     geometry.outer_right, geometry.outer_bottom);
            FillBand(&output->y,
                     geometry.outer_left, geometry.inner_top,
                     geometry.inner_left, geometry.inner_bottom);
            FillBand(&output->y,
                     geometry.inner_right, geometry.inner_top,
                     geometry.outer_right, geometry.inner_bottom);
        }
    }

    for (size_t i = 0; i < rect_count; ++i) {
        BlendChromaBorder(rects, rect_count, i, output);
    }

    return MixYuvStatus::kOk;
}

}  // namespace yuvmix
