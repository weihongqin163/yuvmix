#include "video/mix_yuv_c.h"

#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include "video/mix_yuv.h"

struct yuvmix_context {
    std::unique_ptr<yuvmix::MixYuvContext> context;
};

namespace {

yuvmix_status ToCStatus(yuvmix::MixYuvStatus status) {
    switch (status) {
        case yuvmix::MixYuvStatus::kOk:
            return YUVMIX_STATUS_OK;
        case yuvmix::MixYuvStatus::kInvalidArgument:
            return YUVMIX_STATUS_INVALID_ARGUMENT;
        case yuvmix::MixYuvStatus::kBufferTooSmall:
            return YUVMIX_STATUS_BUFFER_TOO_SMALL;
        case yuvmix::MixYuvStatus::kOutOfMemory:
            return YUVMIX_STATUS_OUT_OF_MEMORY;
        case yuvmix::MixYuvStatus::kFontError:
            return YUVMIX_STATUS_FONT_ERROR;
        case yuvmix::MixYuvStatus::kLibyuvError:
            return YUVMIX_STATUS_LIBYUV_ERROR;
        case yuvmix::MixYuvStatus::kInternalError:
            return YUVMIX_STATUS_INTERNAL_ERROR;
    }
    return YUVMIX_STATUS_INTERNAL_ERROR;
}

yuvmix::ConstPlane ToCppPlane(const yuvmix_const_plane& plane) {
    yuvmix::ConstPlane cpp_plane;
    cpp_plane.data = plane.data;
    cpp_plane.stride = plane.stride;
    cpp_plane.size = plane.size;
    return cpp_plane;
}

yuvmix::MutablePlane ToCppPlane(const yuvmix_mutable_plane& plane) {
    yuvmix::MutablePlane cpp_plane;
    cpp_plane.data = plane.data;
    cpp_plane.stride = plane.stride;
    cpp_plane.size = plane.size;
    return cpp_plane;
}

yuvmix::I420ImageView ToCppImage(const yuvmix_i420_image& image) {
    yuvmix::I420ImageView cpp_image;
    cpp_image.y = ToCppPlane(image.y);
    cpp_image.u = ToCppPlane(image.u);
    cpp_image.v = ToCppPlane(image.v);
    cpp_image.width = image.width;
    cpp_image.height = image.height;
    return cpp_image;
}

yuvmix::MutableI420ImageView ToCppImage(
    const yuvmix_mutable_i420_image& image) {
    yuvmix::MutableI420ImageView cpp_image;
    cpp_image.y = ToCppPlane(image.y);
    cpp_image.u = ToCppPlane(image.u);
    cpp_image.v = ToCppPlane(image.v);
    cpp_image.width = image.width;
    cpp_image.height = image.height;
    return cpp_image;
}

bool ToCppFillMode(yuvmix_fill_mode fill_mode,
                   yuvmix::FillMode* cpp_fill_mode) {
    switch (fill_mode) {
        case YUVMIX_FILL_MODE_CONTAIN:
            *cpp_fill_mode = yuvmix::FillMode::kContain;
            return true;
        case YUVMIX_FILL_MODE_COVER:
            *cpp_fill_mode = yuvmix::FillMode::kCover;
            return true;
    }
    return false;
}

}  // namespace

extern "C" yuvmix_status yuvmix_context_create(const yuvmix_config* config,
                                                 yuvmix_context** context) {
    if (context == nullptr) {
        return YUVMIX_STATUS_INVALID_ARGUMENT;
    }
    *context = nullptr;
    if (config == nullptr || config->font_path == nullptr) {
        return YUVMIX_STATUS_INVALID_ARGUMENT;
    }

    try {
        yuvmix::MixYuvConfig cpp_config;
        cpp_config.font_path = config->font_path;
        cpp_config.font_face_index = config->font_face_index;
        cpp_config.font_size = config->font_size;
        cpp_config.osd_left = config->osd_left;
        cpp_config.osd_bottom = config->osd_bottom;

        std::unique_ptr<yuvmix::MixYuvContext> cpp_context;
        const yuvmix::MixYuvStatus status =
            yuvmix::MixYuvContext::Create(cpp_config, &cpp_context);
        if (status != yuvmix::MixYuvStatus::kOk) {
            return ToCStatus(status);
        }

        std::unique_ptr<yuvmix_context> c_context(new yuvmix_context);
        c_context->context = std::move(cpp_context);
        *context = c_context.release();
        return YUVMIX_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return YUVMIX_STATUS_OUT_OF_MEMORY;
    } catch (const std::length_error&) {
        return YUVMIX_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return YUVMIX_STATUS_INTERNAL_ERROR;
    }
}

extern "C" void yuvmix_context_destroy(yuvmix_context* context) {
    delete context;
}

extern "C" yuvmix_status yuvmix_mix(yuvmix_context* context,
                                     const yuvmix_source* sources,
                                     size_t source_count,
                                     yuvmix_output* output) {
    if (context == nullptr || context->context == nullptr ||
        output == nullptr || (sources == nullptr && source_count != 0)) {
        return YUVMIX_STATUS_INVALID_ARGUMENT;
    }

    try {
        std::vector<yuvmix::MixSource> cpp_sources(source_count);
        for (size_t i = 0; i < source_count; ++i) {
            yuvmix::MixSource& cpp_source = cpp_sources[i];
            cpp_source.image = ToCppImage(sources[i].image);
            cpp_source.destination.x = sources[i].destination.x;
            cpp_source.destination.y = sources[i].destination.y;
            cpp_source.destination.w = sources[i].destination.w;
            cpp_source.destination.h = sources[i].destination.h;
            cpp_source.display_name =
                sources[i].display_name == nullptr ? ""
                                                   : sources[i].display_name;
            if (!ToCppFillMode(sources[i].fill_mode,
                               &cpp_source.fill_mode)) {
                return YUVMIX_STATUS_INVALID_ARGUMENT;
            }
            cpp_source.is_highlight = sources[i].is_highlight != 0;
        }

        yuvmix::MixOutput cpp_output;
        cpp_output.image = ToCppImage(output->image);
        cpp_output.background_color.y = output->background_color.y;
        cpp_output.background_color.u = output->background_color.u;
        cpp_output.background_color.v = output->background_color.v;

        const yuvmix::MixSource* cpp_source_data =
            cpp_sources.empty() ? NULL : cpp_sources.data();
        return ToCStatus(yuvmix::MixYuv(context->context.get(),
                                        cpp_source_data,
                                        cpp_sources.size(),
                                        &cpp_output));
    } catch (const std::bad_alloc&) {
        return YUVMIX_STATUS_OUT_OF_MEMORY;
    } catch (const std::length_error&) {
        return YUVMIX_STATUS_OUT_OF_MEMORY;
    } catch (...) {
        return YUVMIX_STATUS_INTERNAL_ERROR;
    }
}
