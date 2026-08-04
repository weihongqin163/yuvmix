#include "video/mix_yuv_c.h"

#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

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
