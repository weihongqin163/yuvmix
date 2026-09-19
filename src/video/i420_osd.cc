#include "video/i420_osd.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace yuvmix {
namespace {

bool RequiredPlaneSize(size_t stride, size_t rows, size_t row_bytes,
                       size_t *required) {
  if (required == NULL || rows == 0 || row_bytes == 0 || stride < row_bytes) {
    return false;
  }
  const size_t preceding_rows = rows - 1;
  if (preceding_rows != 0 &&
      stride >
          (std::numeric_limits<size_t>::max() - row_bytes) / preceding_rows) {
    return false;
  }
  *required = stride * preceding_rows + row_bytes;
  return true;
}

MixYuvStatus ValidatePlane(const ConstPlane &plane, size_t rows,
                           size_t row_bytes) {
  if (plane.data == NULL || plane.stride <= 0 ||
      static_cast<size_t>(plane.stride) < row_bytes) {
    return MixYuvStatus::kInvalidArgument;
  }
  size_t required = 0;
  if (!RequiredPlaneSize(static_cast<size_t>(plane.stride), rows, row_bytes,
                         &required)) {
    return MixYuvStatus::kInvalidArgument;
  }
  return plane.size < required ? MixYuvStatus::kBufferTooSmall
                               : MixYuvStatus::kOk;
}

MixYuvStatus ValidateIcon(const I420ImageView &image) {
  if (image.width < 2 || image.height < 2 || (image.width & 1u) != 0 ||
      (image.height & 1u) != 0 ||
      image.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      image.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return MixYuvStatus::kInvalidArgument;
  }
  MixYuvStatus status = ValidatePlane(image.y, image.height, image.width);
  if (status != MixYuvStatus::kOk) {
    return status;
  }
  status = ValidatePlane(image.u, image.height / 2, image.width / 2);
  return status == MixYuvStatus::kOk
             ? ValidatePlane(image.v, image.height / 2, image.width / 2)
             : status;
}

int64_t FloorEven(int64_t value) {
  const int64_t remainder = value % 2;
  return remainder < 0 ? value - remainder - 2 : value - remainder;
}

int64_t CeilEven(int64_t value) {
  const int64_t remainder = value % 2;
  return remainder == 0 ? value
                        : (remainder > 0 ? value + 1 : value - remainder);
}

void BlendPlaneRegion(const ConstPlane &source, uint32_t source_x,
                      uint32_t source_y, uint32_t width, uint32_t height,
                      uint8_t alpha, MutablePlane *destination,
                      uint32_t destination_x, uint32_t destination_y) {
  for (uint32_t row = 0; row < height; ++row) {
    const uint8_t *source_row =
        source.data + static_cast<size_t>(source_y + row) * source.stride +
        source_x;
    uint8_t *destination_row =
        destination->data +
        static_cast<size_t>(destination_y + row) * destination->stride +
        destination_x;
    if (alpha == 255) {
      std::memcpy(destination_row, source_row, width);
      continue;
    }
    for (uint32_t column = 0; column < width; ++column) {
      const uint32_t mixed =
          source_row[column] * alpha + destination_row[column] * (255u - alpha);
      destination_row[column] = static_cast<uint8_t>((mixed + 127u) / 255u);
    }
  }
}

} // namespace

struct OsdRenderer::Impl {
  std::unique_ptr<FreeTypeOsd> font;
  uint32_t osd_left;
  uint32_t osd_bottom;
  uint32_t osd_gap;
};

MixYuvStatus OsdRenderer::Create(const MixYuvConfig &config,
                                 std::unique_ptr<OsdRenderer> *renderer) {
  if (renderer == NULL) {
    return MixYuvStatus::kInvalidArgument;
  }
  renderer->reset();
  try {
    std::unique_ptr<Impl> impl(new Impl());
    MixYuvStatus status = FreeTypeOsd::Create(config, &impl->font);
    if (status != MixYuvStatus::kOk) {
      return status;
    }
    impl->osd_left = config.osd_left;
    impl->osd_bottom = config.osd_bottom;
    impl->osd_gap = config.osd_gap;
    renderer->reset(new OsdRenderer(std::move(impl)));
    return MixYuvStatus::kOk;
  } catch (const std::bad_alloc &) {
    return MixYuvStatus::kOutOfMemory;
  } catch (const std::length_error &) {
    return MixYuvStatus::kOutOfMemory;
  } catch (...) {
    return MixYuvStatus::kInternalError;
  }
}

OsdRenderer::OsdRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

OsdRenderer::~OsdRenderer() {}

MixYuvStatus OsdRenderer::Prepare(const MixSource &source, OsdPlan *plan) {
  if (plan == NULL) {
    return MixYuvStatus::kInvalidArgument;
  }

  struct IconInput {
    const I420ImageView *image;
    uint8_t alpha;
    bool enabled;
  };
  const IconInput inputs[] = {
      {&source.network_quality_image, source.alpha_network_quality,
       source.is_network_quality},
      {&source.mic_status_image, source.alpha_mic_status, source.is_mic_status},
      {&source.camera_status_image, source.alpha_camera_status,
       source.is_camera_status},
  };

  bool has_visible_icon = false;
  for (size_t i = 0; i < 3; ++i) {
    if (!inputs[i].enabled) {
      continue;
    }
    const MixYuvStatus status = ValidateIcon(*inputs[i].image);
    if (status != MixYuvStatus::kOk) {
      return status;
    }
    has_visible_icon = has_visible_icon || inputs[i].alpha > 0;
  }

  const MixYuvStatus text_status =
      impl_->font->PrepareText(source.display_name, &plan->text);
  if (text_status != MixYuvStatus::kOk) {
    return text_status;
  }

  plan->clip = source.destination;
  plan->icon_count = 0;
  const int64_t raw_baseline = static_cast<int64_t>(source.destination.y) +
                               source.destination.h - impl_->osd_bottom -
                               impl_->font->descender_pixels();
  if (!has_visible_icon) {
    plan->text_pen_x =
        static_cast<int64_t>(source.destination.x) + impl_->osd_left;
    plan->baseline_y = raw_baseline;
    return MixYuvStatus::kOk;
  }

  plan->baseline_y = FloorEven(raw_baseline);
  int64_t cursor_x =
      static_cast<int64_t>(source.destination.x) + impl_->osd_left;
  for (size_t i = 0; i < 3; ++i) {
    const IconInput &input = inputs[i];
    if (!input.enabled || input.alpha == 0) {
      continue;
    }
    const int64_t icon_x = CeilEven(cursor_x);
    const int64_t icon_y =
        plan->baseline_y - static_cast<int64_t>(input.image->height);
    const int64_t left = std::max<int64_t>(icon_x, source.destination.x);
    const int64_t top = std::max<int64_t>(icon_y, source.destination.y);
    const int64_t right = std::min<int64_t>(
        icon_x + input.image->width,
        static_cast<int64_t>(source.destination.x) + source.destination.w);
    const int64_t bottom = std::min<int64_t>(
        icon_y + input.image->height,
        static_cast<int64_t>(source.destination.y) + source.destination.h);
    if (left < right && top < bottom) {
      OsdIconPlan &icon = plan->icons[plan->icon_count++];
      icon.image = *input.image;
      icon.source_x = static_cast<uint32_t>(left - icon_x);
      icon.source_y = static_cast<uint32_t>(top - icon_y);
      icon.destination = {
          static_cast<uint32_t>(left),
          static_cast<uint32_t>(top),
          static_cast<uint32_t>(right - left),
          static_cast<uint32_t>(bottom - top),
      };
      icon.alpha = input.alpha;
    }
    cursor_x = icon_x + input.image->width + impl_->osd_gap;
  }
  plan->text_pen_x = cursor_x;
  return MixYuvStatus::kOk;
}

void OsdRenderer::Draw(const OsdPlan &plan,
                       MutableI420ImageView *output) const {
  for (size_t i = 0; i < plan.icon_count; ++i) {
    const OsdIconPlan &icon = plan.icons[i];
    BlendPlaneRegion(icon.image.y, icon.source_x, icon.source_y,
                     icon.destination.w, icon.destination.h, icon.alpha,
                     &output->y, icon.destination.x, icon.destination.y);
    BlendPlaneRegion(icon.image.u, icon.source_x / 2, icon.source_y / 2,
                     icon.destination.w / 2, icon.destination.h / 2, icon.alpha,
                     &output->u, icon.destination.x / 2,
                     icon.destination.y / 2);
    BlendPlaneRegion(icon.image.v, icon.source_x / 2, icon.source_y / 2,
                     icon.destination.w / 2, icon.destination.h / 2, icon.alpha,
                     &output->v, icon.destination.x / 2,
                     icon.destination.y / 2);
  }
  impl_->font->DrawTextAt(plan.text, plan.clip, plan.text_pen_x,
                          plan.baseline_y, output);
}

size_t OsdRenderer::glyph_count() const { return impl_->font->glyph_count(); }

} // namespace yuvmix
