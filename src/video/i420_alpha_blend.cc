#include "video/mix_yuv.h"

#include <cstring>
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

MixYuvStatus ValidatePlane(const ConstPlane& plane,
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

MixYuvStatus ValidateSourceImage(const I420ImageView& image) {
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

MixYuvStatus ValidateSource(const I420BlendSource& source,
                            uint32_t background_width,
                            uint32_t background_height) {
    MixYuvStatus status = ValidateSourceImage(source.image);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    if ((source.x & 1u) != 0 || (source.y & 1u) != 0 ||
        source.image.width > background_width ||
        source.image.height > background_height ||
        source.x > background_width - source.image.width ||
        source.y > background_height - source.image.height) {
        return MixYuvStatus::kInvalidArgument;
    }
    return MixYuvStatus::kOk;
}

void CopyPlane(const ConstPlane& source,
               uint32_t width,
               uint32_t height,
               MutablePlane* background,
               uint32_t x,
               uint32_t y) {
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* source_row =
            source.data + static_cast<size_t>(row) * source.stride;
        uint8_t* background_row =
            background->data + static_cast<size_t>(y + row) *
                                   background->stride + x;
        std::memcpy(background_row, source_row, width);
    }
}

void BlendPlane(const ConstPlane& source,
                uint32_t width,
                uint32_t height,
                uint8_t alpha,
                MutablePlane* background,
                uint32_t x,
                uint32_t y) {
    const uint32_t source_weight = alpha;
    const uint32_t background_weight = 255u - source_weight;
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* source_row =
            source.data + static_cast<size_t>(row) * source.stride;
        uint8_t* background_row =
            background->data + static_cast<size_t>(y + row) *
                                   background->stride + x;
        for (uint32_t column = 0; column < width; ++column) {
            const uint32_t weighted =
                source_row[column] * source_weight +
                background_row[column] * background_weight;
            background_row[column] =
                static_cast<uint8_t>((weighted + 127u) / 255u);
        }
    }
}

void BlendSource(const I420BlendSource& source,
                 MutableI420ImageView* background) {
    if (source.alpha == 0) {
        return;
    }
    if (source.alpha == 255) {
        CopyPlane(source.image.y, source.image.width, source.image.height,
                  &background->y, source.x, source.y);
        CopyPlane(source.image.u, source.image.width / 2,
                  source.image.height / 2, &background->u,
                  source.x / 2, source.y / 2);
        CopyPlane(source.image.v, source.image.width / 2,
                  source.image.height / 2, &background->v,
                  source.x / 2, source.y / 2);
        return;
    }
    BlendPlane(source.image.y, source.image.width, source.image.height,
               source.alpha, &background->y, source.x, source.y);
    BlendPlane(source.image.u, source.image.width / 2,
               source.image.height / 2, source.alpha, &background->u,
               source.x / 2, source.y / 2);
    BlendPlane(source.image.v, source.image.width / 2,
               source.image.height / 2, source.alpha, &background->v,
               source.x / 2, source.y / 2);
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
    for (size_t i = 0; i < source_count; ++i) {
        const MixYuvStatus source_status =
            ValidateSource(sources[i], background->width, background->height);
        if (source_status != MixYuvStatus::kOk) {
            return source_status;
        }
    }
    for (size_t i = 0; i < source_count; ++i) {
        BlendSource(sources[i], background);
    }
    return MixYuvStatus::kOk;
}

}  // namespace yuvmix
