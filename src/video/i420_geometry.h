#ifndef YUVMIX_VIDEO_I420_GEOMETRY_H_
#define YUVMIX_VIDEO_I420_GEOMETRY_H_

#include <cstdint>

#include "video/mix_yuv.h"

namespace yuvmix {

struct GeometryPlan {
    uint32_t crop_x;
    uint32_t crop_y;
    uint32_t crop_w;
    uint32_t crop_h;
    uint32_t dest_x;
    uint32_t dest_y;
    uint32_t dest_w;
    uint32_t dest_h;
    bool use_copy;
};

MixYuvStatus BuildGeometryPlan(uint32_t source_width,
                               uint32_t source_height,
                               uint32_t target_width,
                               uint32_t target_height,
                               FillMode mode,
                               GeometryPlan* plan);

}  // namespace yuvmix

#endif  // YUVMIX_VIDEO_I420_GEOMETRY_H_
