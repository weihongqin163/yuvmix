#include "video/i420_highlight.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace yuvmix {
namespace {

const uint8_t kHighlightY = 143;
const uint8_t kHighlightU = 113;
const uint8_t kHighlightV = 35;

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

}  // namespace

uint32_t BorderWidth(uint32_t cell_height) {
    const uint64_t scaled =
        (3u * static_cast<uint64_t>(cell_height) + 125u) / 250u;
    return static_cast<uint32_t>(std::max<uint64_t>(2u, scaled));
}

uint8_t BlendHighlightChroma(uint8_t old_value,
                             uint8_t border_value,
                             uint32_t coverage) {
    const uint32_t sum = coverage * border_value +
                         (4u - coverage) * old_value;
    return static_cast<uint8_t>((sum + 2u) / 4u);
}

MixYuvStatus EnsureHighlightMask(uint32_t width,
                                 uint32_t height,
                                 std::vector<uint8_t>* mask) {
    if (mask == NULL || width < 2 || height < 2 ||
        (width & 1u) != 0 || (height & 1u) != 0 ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() / height) {
        return MixYuvStatus::kInvalidArgument;
    }

    try {
        const size_t required = static_cast<size_t>(width) * height;
        if (mask->size() < required) {
            mask->resize(required);
        }
        return MixYuvStatus::kOk;
    } catch (const std::bad_alloc&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (const std::length_error&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (...) {
        return MixYuvStatus::kInternalError;
    }
}

MixYuvStatus DrawHighlights(const Rect* rects,
                            size_t rect_count,
                            MutableI420ImageView* output,
                            std::vector<uint8_t>* mask) {
    if (output == NULL || mask == NULL ||
        (rects == NULL && rect_count != 0) || !ValidOutput(*output)) {
        return MixYuvStatus::kInvalidArgument;
    }

    const size_t required =
        static_cast<size_t>(output->width) * output->height;
    if (mask->size() < required) {
        return MixYuvStatus::kBufferTooSmall;
    }
    for (size_t i = 0; i < rect_count; ++i) {
        if (!ValidRect(rects[i], output->width, output->height)) {
            return MixYuvStatus::kInvalidArgument;
        }
    }

    std::fill(mask->begin(), mask->begin() + required, 0);
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

        for (int64_t y = outer_top; y < outer_bottom; ++y) {
            for (int64_t x = outer_left; x < outer_right; ++x) {
                const bool inside = x >= inner_left && x < inner_right &&
                                    y >= inner_top && y < inner_bottom;
                if (!inside) {
                    (*mask)[static_cast<size_t>(y) * output->width + x] = 1;
                }
            }
        }
    }

    for (uint32_t y = 0; y < output->height; ++y) {
        for (uint32_t x = 0; x < output->width; ++x) {
            if ((*mask)[static_cast<size_t>(y) * output->width + x] != 0) {
                output->y.data[static_cast<size_t>(y) * output->y.stride + x] =
                    kHighlightY;
            }
        }
    }

    for (uint32_t y = 0; y < output->height / 2; ++y) {
        for (uint32_t x = 0; x < output->width / 2; ++x) {
            uint32_t coverage = 0;
            const uint32_t luma_x = x * 2;
            const uint32_t luma_y = y * 2;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    coverage += (*mask)[
                        static_cast<size_t>(luma_y + dy) * output->width +
                        luma_x + dx];
                }
            }
            if (coverage != 0) {
                const size_t offset =
                    static_cast<size_t>(y) * output->u.stride + x;
                output->u.data[offset] = BlendHighlightChroma(
                    output->u.data[offset], kHighlightU, coverage);
                const size_t v_offset =
                    static_cast<size_t>(y) * output->v.stride + x;
                output->v.data[v_offset] = BlendHighlightChroma(
                    output->v.data[v_offset], kHighlightV, coverage);
            }
        }
    }

    return MixYuvStatus::kOk;
}

}  // namespace yuvmix
