#include "video/mix_yuv.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include <libyuv/planar_functions.h>
#include <libyuv/scale.h>

#include "video/freetype_osd.h"
#include "video/i420_geometry.h"
#include "video/i420_highlight.h"

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

MixYuvStatus ValidateOutputImage(const MutableI420ImageView& image) {
    if (!IsValidImageSize(image.width, image.height)) {
        return MixYuvStatus::kInvalidArgument;
    }

    MixYuvStatus status =
        ValidatePlane(image.y, image.height, image.width);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    status = ValidatePlane(image.u, image.height / 2, image.width / 2);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    return ValidatePlane(image.v, image.height / 2, image.width / 2);
}

MixYuvStatus ValidateInputImage(const I420ImageView& image) {
    if (!IsValidImageSize(image.width, image.height)) {
        return MixYuvStatus::kInvalidArgument;
    }

    MixYuvStatus status =
        ValidatePlane(image.y, image.height, image.width);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    status = ValidatePlane(image.u, image.height / 2, image.width / 2);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    return ValidatePlane(image.v, image.height / 2, image.width / 2);
}

bool IsEven(uint32_t value) {
    return (value & 1u) == 0;
}

MixYuvStatus ValidateDestination(const Rect& rect,
                                 uint32_t output_width,
                                 uint32_t output_height) {
    if (!IsEven(rect.x) || !IsEven(rect.y) ||
        rect.w < 2 || rect.h < 2 ||
        !IsEven(rect.w) || !IsEven(rect.h) ||
        rect.w > output_width || rect.h > output_height ||
        rect.x > output_width - rect.w ||
        rect.y > output_height - rect.h) {
        return MixYuvStatus::kInvalidArgument;
    }
    return MixYuvStatus::kOk;
}

void FillPlane(uint8_t* data,
               int stride,
               uint32_t rows,
               uint32_t row_bytes,
               uint8_t value) {
    for (uint32_t row = 0; row < rows; ++row) {
        std::fill(data + static_cast<size_t>(row) * stride,
                  data + static_cast<size_t>(row) * stride + row_bytes,
                  value);
    }
}

void FillOutput(MixOutput* output) {
    FillPlane(output->image.y.data, output->image.y.stride,
              output->image.height, output->image.width,
              output->background_color.y);
    FillPlane(output->image.u.data, output->image.u.stride,
              output->image.height / 2, output->image.width / 2,
              output->background_color.u);
    FillPlane(output->image.v.data, output->image.v.stride,
              output->image.height / 2, output->image.width / 2,
              output->background_color.v);
}

struct SourcePlan {
    const MixSource* source;
    GeometryPlan geometry;
    TextRun text;
};

MixYuvStatus DrawSource(const SourcePlan& plan, MixOutput* output) {
    const MixSource& source = *plan.source;
    const GeometryPlan& geometry = plan.geometry;

    const uint8_t* source_y =
        source.image.y.data +
        static_cast<size_t>(geometry.crop_y) * source.image.y.stride +
        geometry.crop_x;
    const uint8_t* source_u =
        source.image.u.data +
        static_cast<size_t>(geometry.crop_y / 2) * source.image.u.stride +
        geometry.crop_x / 2;
    const uint8_t* source_v =
        source.image.v.data +
        static_cast<size_t>(geometry.crop_y / 2) * source.image.v.stride +
        geometry.crop_x / 2;

    const uint32_t destination_x =
        source.destination.x + geometry.dest_x;
    const uint32_t destination_y =
        source.destination.y + geometry.dest_y;
    uint8_t* destination_y_plane =
        output->image.y.data +
        static_cast<size_t>(destination_y) * output->image.y.stride +
        destination_x;
    uint8_t* destination_u_plane =
        output->image.u.data +
        static_cast<size_t>(destination_y / 2) * output->image.u.stride +
        destination_x / 2;
    uint8_t* destination_v_plane =
        output->image.v.data +
        static_cast<size_t>(destination_y / 2) * output->image.v.stride +
        destination_x / 2;

    int result = 0;
    if (geometry.use_copy) {
        result = libyuv::I420Copy(
            source_y, source.image.y.stride,
            source_u, source.image.u.stride,
            source_v, source.image.v.stride,
            destination_y_plane, output->image.y.stride,
            destination_u_plane, output->image.u.stride,
            destination_v_plane, output->image.v.stride,
            static_cast<int>(geometry.dest_w),
            static_cast<int>(geometry.dest_h));
    } else {
        result = libyuv::I420Scale(
            source_y, source.image.y.stride,
            source_u, source.image.u.stride,
            source_v, source.image.v.stride,
            static_cast<int>(geometry.crop_w),
            static_cast<int>(geometry.crop_h),
            destination_y_plane, output->image.y.stride,
            destination_u_plane, output->image.u.stride,
            destination_v_plane, output->image.v.stride,
            static_cast<int>(geometry.dest_w),
            static_cast<int>(geometry.dest_h),
            libyuv::kFilterBilinear);
    }
    return result == 0 ? MixYuvStatus::kOk
                       : MixYuvStatus::kLibyuvError;
}

}  // namespace

struct MixYuvContext::Impl {
    std::unique_ptr<FreeTypeOsd> osd;
    std::vector<SourcePlan> plans;
    std::vector<Rect> highlight_rects;
    std::vector<uint8_t> highlight_mask;
};

MixYuvContext::MixYuvContext(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

MixYuvContext::~MixYuvContext() {}

MixYuvStatus MixYuvContext::Create(
    const MixYuvConfig& config,
    std::unique_ptr<MixYuvContext>* context) {
    if (context == NULL) {
        return MixYuvStatus::kInvalidArgument;
    }
    context->reset();

    try {
        std::unique_ptr<Impl> impl(new Impl());
        const MixYuvStatus status = FreeTypeOsd::Create(config, &impl->osd);
        if (status != MixYuvStatus::kOk) {
            return status;
        }
        context->reset(new MixYuvContext(std::move(impl)));
        return MixYuvStatus::kOk;
    } catch (const std::bad_alloc&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (const std::length_error&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (...) {
        return MixYuvStatus::kInternalError;
    }
}

MixYuvStatus MixYuv(MixYuvContext* context,
                    const MixSource* sources,
                    size_t source_count,
                    MixOutput* output) {
    if (context == NULL || output == NULL ||
        (sources == NULL && source_count != 0)) {
        return MixYuvStatus::kInvalidArgument;
    }

    bool output_started = false;
    try {
        MixYuvStatus status = ValidateOutputImage(output->image);
        if (status != MixYuvStatus::kOk) {
            return status;
        }

        context->impl_->plans.resize(source_count);
        context->impl_->highlight_rects.clear();
        context->impl_->highlight_rects.reserve(source_count);
        for (size_t i = 0; i < source_count; ++i) {
            status = ValidateInputImage(sources[i].image);
            if (status != MixYuvStatus::kOk) {
                return status;
            }
            status = ValidateDestination(sources[i].destination,
                                         output->image.width,
                                         output->image.height);
            if (status != MixYuvStatus::kOk) {
                return status;
            }

            SourcePlan& plan = context->impl_->plans[i];
            plan.source = &sources[i];
            status = BuildGeometryPlan(
                sources[i].image.width, sources[i].image.height,
                sources[i].destination.w, sources[i].destination.h,
                sources[i].fill_mode, &plan.geometry);
            if (status != MixYuvStatus::kOk) {
                return status;
            }
            status = context->impl_->osd->PrepareText(
                sources[i].display_name, &plan.text);
            if (status != MixYuvStatus::kOk) {
                return status;
            }
            if (sources[i].is_highlight) {
                context->impl_->highlight_rects.push_back(
                    sources[i].destination);
            }
        }

        if (!context->impl_->highlight_rects.empty()) {
            status = EnsureHighlightMask(output->image.width,
                                         output->image.height,
                                         &context->impl_->highlight_mask);
            if (status != MixYuvStatus::kOk) {
                return status;
            }
        }

        output_started = true;
        FillOutput(output);
        for (size_t i = 0; i < context->impl_->plans.size(); ++i) {
            status = DrawSource(context->impl_->plans[i], output);
            if (status != MixYuvStatus::kOk) {
                return status;
            }
        }
        if (!context->impl_->highlight_rects.empty()) {
            status = DrawHighlights(
                context->impl_->highlight_rects.data(),
                context->impl_->highlight_rects.size(),
                &output->image, &context->impl_->highlight_mask);
            if (status != MixYuvStatus::kOk) {
                return status;
            }
        }
        for (size_t i = 0; i < context->impl_->plans.size(); ++i) {
            context->impl_->osd->DrawText(
                context->impl_->plans[i].text,
                context->impl_->plans[i].source->destination,
                &output->image);
        }
        return MixYuvStatus::kOk;
    } catch (const std::bad_alloc&) {
        return output_started ? MixYuvStatus::kInternalError
                              : MixYuvStatus::kOutOfMemory;
    } catch (const std::length_error&) {
        return output_started ? MixYuvStatus::kInternalError
                              : MixYuvStatus::kOutOfMemory;
    } catch (...) {
        return MixYuvStatus::kInternalError;
    }
}

#if defined(YUVMIX_TESTING)
size_t MixYuvContextPreparedCapacity(const MixYuvContext& context) {
    return context.impl_->plans.capacity();
}

size_t MixYuvContextGlyphCount(const MixYuvContext& context) {
    return context.impl_->osd->glyph_count();
}

size_t MixYuvContextHighlightMaskCapacity(const MixYuvContext& context) {
    return context.impl_->highlight_mask.capacity();
}
#endif

}  // namespace yuvmix
