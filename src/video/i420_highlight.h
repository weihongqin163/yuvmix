#ifndef YUVMIX_VIDEO_I420_HIGHLIGHT_H_
#define YUVMIX_VIDEO_I420_HIGHLIGHT_H_

#include <cstddef>
#include <cstdint>

#include "video/mix_yuv.h"

namespace yuvmix {

uint32_t BorderWidth(uint32_t cell_height);

MixYuvStatus DrawHighlights(const Rect* rects,
                            size_t rect_count,
                            MutableI420ImageView* output);

}  // namespace yuvmix

#endif  // YUVMIX_VIDEO_I420_HIGHLIGHT_H_
