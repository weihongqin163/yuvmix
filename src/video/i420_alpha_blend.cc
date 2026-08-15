#include "video/mix_yuv.h"

#include <limits>

namespace yuvmix {
namespace {

bool IsValidImageSize(uint32_t width, uint32_t height) {
    return width >= 2 && height >= 2 &&
           width <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
           height <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
           (width & 1u) == 0 && (height & 1u) == 0;
}

bool RequiredPlaneSize(size_t stride,
                       size_t rows,
                       size_t row_bytes,
                       size_t* required) {
    if (required == NULL || rows == 0 || row_bytes == 0 ||
        stride < row_bytes) {
        return false;
    }
    const size_t preceding_rows = rows - 1;
    if (preceding_rows != 0 &&
        stride > (std::numeric_limits<size_t>::max() - row_bytes) /
                     preceding_rows) {
        return false;
    }
    *required = stride * preceding_rows + row_bytes;
    return true;
}

MixYuvStatus ValidatePlane(const MutablePlane& plane,
                           size_t rows,
                           size_t row_bytes) {
    if (plane.data == NULL || plane.stride <= 0 ||
        static_cast<size_t>(plane.stride) < row_bytes) {
        return MixYuvStatus::kInvalidArgument;
    }
    size_t required = 0;
    if (!RequiredPlaneSize(static_cast<size_t>(plane.stride), rows,
                           row_bytes, &required)) {
        return MixYuvStatus::kInvalidArgument;
    }
    return plane.size < required ? MixYuvStatus::kBufferTooSmall
                                 : MixYuvStatus::kOk;
}

MixYuvStatus ValidateBackground(const MutableI420ImageView& image) {
    if (!IsValidImageSize(image.width, image.height)) {
        return MixYuvStatus::kInvalidArgument;
    }
    MixYuvStatus status = ValidatePlane(image.y, image.height, image.width);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    status = ValidatePlane(image.u, image.height / 2, image.width / 2);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    return ValidatePlane(image.v, image.height / 2, image.width / 2);
}

}  // namespace

MixYuvStatus AlphaBlendI420(const I420BlendSource* sources,
                            size_t source_count,
                            MutableI420ImageView* background) {
    if (background == NULL || (sources == NULL && source_count != 0)) {
        return MixYuvStatus::kInvalidArgument;
    }
    const MixYuvStatus status = ValidateBackground(*background);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    return source_count == 0 ? MixYuvStatus::kOk
                             : MixYuvStatus::kInternalError;
}

}  // namespace yuvmix
