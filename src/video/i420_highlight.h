#ifndef YUVMIX_VIDEO_I420_HIGHLIGHT_H_
#define YUVMIX_VIDEO_I420_HIGHLIGHT_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "video/mix_yuv.h"

namespace yuvmix {

uint32_t BorderWidth(uint32_t cell_height);

uint8_t BlendHighlightChroma(uint8_t old_value,
                             uint8_t border_value,
                             uint32_t coverage);

MixYuvStatus EnsureHighlightMask(uint32_t width,
                                 uint32_t height,
                                 std::vector<uint8_t>* mask);

MixYuvStatus DrawHighlights(const Rect* rects,
                            size_t rect_count,
                            MutableI420ImageView* output,
                            std::vector<uint8_t>* mask);

}  // namespace yuvmix

#endif  // YUVMIX_VIDEO_I420_HIGHLIGHT_H_
