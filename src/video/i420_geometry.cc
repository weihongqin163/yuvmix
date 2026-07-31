#include "video/i420_geometry.h"

#include <cstdint>

namespace yuvmix {
namespace {

bool IsValidI420Dimension(uint32_t value) {
    return value >= 2 && (value & 1u) == 0;
}

uint32_t FloorEven(uint64_t value) {
    return static_cast<uint32_t>(value & ~uint64_t(1));
}

uint32_t CenterOffset(uint32_t outer, uint32_t inner) {
    return FloorEven((outer - inner) / 2u);
}

}  // namespace

MixYuvStatus BuildGeometryPlan(uint32_t source_width,
                               uint32_t source_height,
                               uint32_t target_width,
                               uint32_t target_height,
                               FillMode mode,
                               GeometryPlan* plan) {
    if (plan == NULL ||
        !IsValidI420Dimension(source_width) ||
        !IsValidI420Dimension(source_height) ||
        !IsValidI420Dimension(target_width) ||
        !IsValidI420Dimension(target_height) ||
        (mode != FillMode::kContain && mode != FillMode::kCover)) {
        return MixYuvStatus::kInvalidArgument;
    }

    GeometryPlan result = {};
    result.crop_w = source_width;
    result.crop_h = source_height;

    if (source_width < target_width && source_height < target_height) {
        result.dest_x = CenterOffset(target_width, source_width);
        result.dest_y = CenterOffset(target_height, source_height);
        result.dest_w = source_width;
        result.dest_h = source_height;
        result.use_copy = true;
        *plan = result;
        return MixYuvStatus::kOk;
    }

    const uint64_t width_limited =
        static_cast<uint64_t>(target_width) * source_height;
    const uint64_t height_limited =
        static_cast<uint64_t>(target_height) * source_width;

    if (mode == FillMode::kContain) {
        if (width_limited <= height_limited) {
            result.dest_w = target_width;
            result.dest_h = FloorEven(
                static_cast<uint64_t>(source_height) * target_width /
                source_width);
        } else {
            result.dest_w = FloorEven(
                static_cast<uint64_t>(source_width) * target_height /
                source_height);
            result.dest_h = target_height;
        }

        if (!IsValidI420Dimension(result.dest_w) ||
            !IsValidI420Dimension(result.dest_h)) {
            return MixYuvStatus::kInvalidArgument;
        }

        result.dest_x = CenterOffset(target_width, result.dest_w);
        result.dest_y = CenterOffset(target_height, result.dest_h);
    } else {
        result.dest_w = target_width;
        result.dest_h = target_height;

        if (width_limited < height_limited) {
            result.crop_w = FloorEven(
                static_cast<uint64_t>(source_height) * target_width /
                target_height);
            result.crop_x = CenterOffset(source_width, result.crop_w);
        } else if (width_limited > height_limited) {
            result.crop_h = FloorEven(
                static_cast<uint64_t>(source_width) * target_height /
                target_width);
            result.crop_y = CenterOffset(source_height, result.crop_h);
        }

        if (!IsValidI420Dimension(result.crop_w) ||
            !IsValidI420Dimension(result.crop_h)) {
            return MixYuvStatus::kInvalidArgument;
        }
    }

    result.use_copy = result.crop_x == 0 && result.crop_y == 0 &&
                      result.crop_w == source_width &&
                      result.crop_h == source_height &&
                      result.dest_w == source_width &&
                      result.dest_h == source_height;
    *plan = result;
    return MixYuvStatus::kOk;
}

}  // namespace yuvmix
