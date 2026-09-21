#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/i420_osd.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

namespace {

uint8_t Blend(uint8_t source, uint8_t destination, uint8_t alpha) {
  return static_cast<uint8_t>(
      (source * alpha + destination * (255u - alpha) + 127u) / 255u);
}

} // namespace

int main() {
  using namespace yuvmix;
  using yuvmix_test::OwnedI420;

  MixYuvConfig config = {};
  config.font_path = YUVMIX_TEST_FONT;
  config.font_size = 18;
  config.osd_left = 3;
  config.osd_bottom = 2;
  config.osd_gap = 3;
  std::unique_ptr<OsdRenderer> renderer;
  EXPECT_EQ(OsdRenderer::Create(config, &renderer), MixYuvStatus::kOk);

  OwnedI420 network(4, 4, 2);
  OwnedI420 mic(6, 6, 2);
  OwnedI420 camera(4, 2, 2);
  network.Fill(210, 40, 220);
  mic.Fill(180, 200, 30);
  camera.Fill(70, 150, 90);

  MixSource source = {};
  source.destination = {0, 0, 32, 24};
  source.network_quality_image = network.ConstView();
  source.alpha_network_quality = 255;
  source.is_network_quality = true;
  source.mic_status_image = mic.ConstView();
  source.alpha_mic_status = 255;
  source.is_mic_status = true;
  source.camera_status_image = camera.ConstView();
  source.alpha_camera_status = 255;
  source.is_camera_status = true;

  OsdPlan plan = {};
  EXPECT_EQ(renderer->Prepare(source, &plan), MixYuvStatus::kOk);
  EXPECT_EQ(plan.icon_count, 3u);
  EXPECT_EQ(plan.icons[0].destination.x, 4u);
  EXPECT_EQ(plan.icons[1].destination.x, 12u);
  EXPECT_EQ(plan.icons[2].destination.x, 22u);
  EXPECT_EQ(plan.text_pen_x, INT64_C(29));
  EXPECT_EQ(plan.baseline_y & INT64_C(1), INT64_C(0));

  OwnedI420 valid_icon(4, 4, 2);
  valid_icon.Fill(200, 40, 220);
  MixSource validation = {};
  validation.destination = {0, 0, 32, 24};
  validation.network_quality_image = I420ImageView();
  validation.is_network_quality = false;
  EXPECT_EQ(renderer->Prepare(validation, &plan), MixYuvStatus::kOk);

  validation.network_quality_image = valid_icon.ConstView();
  validation.alpha_network_quality = 0;
  validation.is_network_quality = true;
  EXPECT_EQ(renderer->Prepare(validation, &plan), MixYuvStatus::kOk);
  EXPECT_EQ(plan.icon_count, 0u);
  EXPECT_EQ(plan.text_pen_x,
            static_cast<int64_t>(validation.destination.x) + config.osd_left);

  validation.network_quality_image = valid_icon.ConstView();
  validation.network_quality_image.y.size = 1;
  EXPECT_EQ(renderer->Prepare(validation, &plan),
            MixYuvStatus::kBufferTooSmall);

  validation.network_quality_image = valid_icon.ConstView();
  validation.network_quality_image.width = 3;
  EXPECT_EQ(renderer->Prepare(validation, &plan),
            MixYuvStatus::kInvalidArgument);
  validation.network_quality_image = valid_icon.ConstView();
  validation.network_quality_image.height = 1;
  EXPECT_EQ(renderer->Prepare(validation, &plan),
            MixYuvStatus::kInvalidArgument);
  validation.network_quality_image = valid_icon.ConstView();
  validation.network_quality_image.y.data = NULL;
  EXPECT_EQ(renderer->Prepare(validation, &plan),
            MixYuvStatus::kInvalidArgument);
  validation.network_quality_image = valid_icon.ConstView();
  validation.network_quality_image.u.stride = 1;
  EXPECT_EQ(renderer->Prepare(validation, &plan),
            MixYuvStatus::kInvalidArgument);
  validation.network_quality_image = valid_icon.ConstView();
  validation.network_quality_image.v.size = 1;
  EXPECT_EQ(renderer->Prepare(validation, &plan),
            MixYuvStatus::kBufferTooSmall);

  OwnedI420 tall_icon(4, 32, 2);
  tall_icon.Fill(200, 40, 220);
  MixSource clipped = {};
  clipped.destination = {0, 0, 32, 24};
  clipped.network_quality_image = tall_icon.ConstView();
  clipped.alpha_network_quality = 255;
  clipped.is_network_quality = true;
  EXPECT_EQ(renderer->Prepare(clipped, &plan), MixYuvStatus::kOk);
  EXPECT_EQ(plan.icon_count, 1u);
  EXPECT_TRUE(plan.icons[0].source_y > 0);
  EXPECT_EQ(plan.icons[0].destination.y, 0u);
  EXPECT_EQ(plan.icons[0].source_x & 1u, 0u);
  EXPECT_EQ(plan.icons[0].source_y & 1u, 0u);
  EXPECT_EQ(plan.icons[0].destination.x & 1u, 0u);
  EXPECT_EQ(plan.icons[0].destination.y & 1u, 0u);
  EXPECT_EQ(plan.icons[0].destination.w & 1u, 0u);
  EXPECT_EQ(plan.icons[0].destination.h & 1u, 0u);

  MixYuvConfig right_config = config;
  right_config.osd_left = 29;
  std::unique_ptr<OsdRenderer> right_renderer;
  EXPECT_EQ(OsdRenderer::Create(right_config, &right_renderer),
            MixYuvStatus::kOk);
  OwnedI420 wide_icon(8, 4, 2);
  wide_icon.Fill(200, 40, 220);
  clipped.network_quality_image = wide_icon.ConstView();
  EXPECT_EQ(right_renderer->Prepare(clipped, &plan), MixYuvStatus::kOk);
  EXPECT_EQ(plan.icon_count, 1u);
  EXPECT_EQ(plan.icons[0].destination.x, 30u);
  EXPECT_EQ(plan.icons[0].destination.w, 2u);
  EXPECT_EQ(plan.icons[0].destination.h, 4u);
  EXPECT_EQ(plan.icons[0].source_x, 0u);

  MixYuvConfig far_right_config = config;
  far_right_config.osd_left = std::numeric_limits<uint32_t>::max();
  std::unique_ptr<OsdRenderer> far_right_renderer;
  EXPECT_EQ(OsdRenderer::Create(far_right_config, &far_right_renderer),
            MixYuvStatus::kOk);
  clipped.network_quality_image = valid_icon.ConstView();
  EXPECT_EQ(far_right_renderer->Prepare(clipped, &plan), MixYuvStatus::kOk);
  EXPECT_EQ(plan.icon_count, 0u);
  EXPECT_EQ(plan.text_pen_x,
            static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1 +
                valid_icon.ConstView().width + config.osd_gap);

  MixYuvConfig far_bottom_config = config;
  far_bottom_config.osd_bottom = std::numeric_limits<uint32_t>::max();
  std::unique_ptr<OsdRenderer> far_bottom_renderer;
  EXPECT_EQ(OsdRenderer::Create(far_bottom_config, &far_bottom_renderer),
            MixYuvStatus::kOk);
  EXPECT_EQ(far_bottom_renderer->Prepare(clipped, &plan), MixYuvStatus::kOk);
  EXPECT_TRUE(plan.baseline_y < 0);
  EXPECT_EQ(plan.baseline_y & INT64_C(1), INT64_C(0));
  EXPECT_EQ(plan.icon_count, 0u);
  EXPECT_EQ(plan.text_pen_x,
            INT64_C(4) + valid_icon.ConstView().width + config.osd_gap);

  OwnedI420 alpha_output_image(40, 32, 3);
  MutableI420ImageView alpha_output = alpha_output_image.MutableView();
  MixSource alpha_source = {};
  alpha_source.destination = {4, 4, 32, 24};
  alpha_source.network_quality_image = valid_icon.ConstView();
  alpha_source.is_network_quality = true;

  alpha_output_image.Fill(16, 128, 128);
  const std::vector<uint8_t> transparent_before = alpha_output_image.Snapshot();
  alpha_source.alpha_network_quality = 0;
  EXPECT_EQ(renderer->Prepare(alpha_source, &plan), MixYuvStatus::kOk);
  renderer->Draw(plan, &alpha_output);
  EXPECT_TRUE(alpha_output_image.Snapshot() == transparent_before);

  const uint8_t alphas[] = {1, 128, 254, 255};
  for (size_t i = 0; i < 4; ++i) {
    alpha_output_image.Fill(16, 128, 128);
    alpha_source.alpha_network_quality = alphas[i];
    EXPECT_EQ(renderer->Prepare(alpha_source, &plan), MixYuvStatus::kOk);
    EXPECT_EQ(plan.icon_count, 1u);
    renderer->Draw(plan, &alpha_output);
    const uint32_t x = plan.icons[0].destination.x;
    const uint32_t y = plan.icons[0].destination.y;
    EXPECT_EQ(alpha_output_image.Y(x, y), Blend(200, 16, alphas[i]));
    EXPECT_EQ(alpha_output_image.U(x / 2, y / 2), Blend(40, 128, alphas[i]));
    EXPECT_EQ(alpha_output_image.V(x / 2, y / 2), Blend(220, 128, alphas[i]));
    EXPECT_EQ(alpha_output_image.Y(0, 0), 16);
    EXPECT_EQ(alpha_output_image.U(0, 0), 128);
    EXPECT_EQ(alpha_output_image.V(0, 0), 128);
    EXPECT_TRUE(alpha_output_image.PaddingEquals(0xCC));
    EXPECT_TRUE(alpha_output_image.GuardsIntact());
  }

  return yuvmix_test::Finish();
}
