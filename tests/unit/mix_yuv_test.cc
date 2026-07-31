#include <memory>
#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/mix_yuv.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

namespace {

std::unique_ptr<yuvmix::MixYuvContext> CreateContext(
    uint32_t font_size = 24,
    uint32_t osd_left = 12,
    uint32_t osd_bottom = 12) {
    yuvmix::MixYuvConfig config;
    config.font_path = YUVMIX_TEST_FONT;
    config.font_face_index = 0;
    config.font_size = font_size;
    config.osd_left = osd_left;
    config.osd_bottom = osd_bottom;
    std::unique_ptr<yuvmix::MixYuvContext> context;
    EXPECT_EQ(yuvmix::MixYuvContext::Create(config, &context),
              yuvmix::MixYuvStatus::kOk);
    return context;
}

yuvmix::MixOutput MakeOutput(yuvmix_test::OwnedI420* image) {
    yuvmix::MixOutput output = {};
    output.image = image->MutableView();
    output.background_color = {16, 128, 128};
    return output;
}

uint64_t HashPlane(const yuvmix_test::OwnedI420& image,
                   uint32_t width,
                   uint32_t height,
                   char plane) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t value = 0;
            if (plane == 'Y') {
                value = image.Y(x, y);
            } else if (plane == 'U') {
                value = image.U(x, y);
            } else {
                value = image.V(x, y);
            }
            hash ^= value;
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

}  // namespace

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    std::unique_ptr<MixYuvContext> context = CreateContext();

    OwnedI420 canvas(16, 16, 4);
    canvas.Fill(0x37, 0x37, 0x37);
    MixOutput output = MakeOutput(&canvas);

    OwnedI420 left_image(8, 8, 2);
    OwnedI420 right_image(4, 4, 2);
    left_image.Fill(40, 90, 140);
    right_image.Fill(80, 100, 150);
    MixSource sources[] = {
        {left_image.ConstView(), {0, 0, 8, 8}, "",
         FillMode::kContain, false},
        {right_image.ConstView(), {8, 0, 8, 8}, "",
         FillMode::kCover, false},
    };

    EXPECT_EQ(MixYuv(context.get(), sources, 2, &output), MixYuvStatus::kOk);
    EXPECT_EQ(canvas.Y(2, 2), 40);
    EXPECT_EQ(canvas.U(1, 1), 90);
    EXPECT_EQ(canvas.V(1, 1), 140);
    EXPECT_EQ(canvas.Y(8, 0), 16);
    EXPECT_EQ(canvas.Y(10, 2), 80);
    EXPECT_EQ(canvas.U(5, 1), 100);
    EXPECT_EQ(canvas.V(5, 1), 150);
    EXPECT_EQ(canvas.Y(2, 10), 16);
    EXPECT_TRUE(canvas.PaddingEquals(0xCC));
    EXPECT_TRUE(canvas.GuardsIntact());
    EXPECT_TRUE(left_image.PaddingEquals(0xCC));
    EXPECT_TRUE(right_image.PaddingEquals(0xCC));

    OwnedI420 wide_image(8, 4, 2);
    wide_image.Fill(60, 110, 160);
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            wide_image.SetY(x, y, static_cast<uint8_t>(20 + x * 10));
        }
    }

    MixSource contain = {wide_image.ConstView(), {0, 8, 4, 4}, "",
                         FillMode::kContain, false};
    EXPECT_EQ(MixYuv(context.get(), &contain, 1, &output), MixYuvStatus::kOk);
    const uint8_t expected_downscale[] = {25, 45, 65, 85};
    for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            EXPECT_EQ(canvas.Y(x, 8 + y), expected_downscale[x]);
        }
    }
    for (uint32_t x = 0; x < 4; ++x) {
        EXPECT_EQ(canvas.Y(x, 10), 16);
        EXPECT_EQ(canvas.Y(x, 11), 16);
    }
    EXPECT_EQ(canvas.U(0, 4), 110);
    EXPECT_EQ(canvas.V(0, 4), 160);

    MixSource cover = {wide_image.ConstView(), {4, 8, 4, 4}, "",
                       FillMode::kCover, false};
    EXPECT_EQ(MixYuv(context.get(), &cover, 1, &output), MixYuvStatus::kOk);
    const uint8_t expected_crop[] = {40, 50, 60, 70};
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            EXPECT_EQ(canvas.Y(4 + x, 8 + y), expected_crop[x]);
        }
    }
    for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t x = 0; x < 2; ++x) {
            EXPECT_EQ(canvas.U(2 + x, 4 + y), 110);
            EXPECT_EQ(canvas.V(2 + x, 4 + y), 160);
        }
    }

    OwnedI420 gradient_source(8, 8, 3);
    gradient_source.Fill(0, 0, 0);
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            gradient_source.SetY(
                x, y, static_cast<uint8_t>(10 + x * 7 + y * 11));
        }
    }
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            gradient_source.SetU(
                x, y, static_cast<uint8_t>(40 + x * 13 + y * 17));
            gradient_source.SetV(
                x, y, static_cast<uint8_t>(200 - x * 11 - y * 19));
        }
    }
    OwnedI420 gradient_output_image(4, 4, 3);
    MixOutput gradient_output = MakeOutput(&gradient_output_image);
    MixSource gradient = {gradient_source.ConstView(), {0, 0, 4, 4}, "",
                          FillMode::kContain, false};
    EXPECT_EQ(MixYuv(context.get(), &gradient, 1, &gradient_output),
              MixYuvStatus::kOk);
    EXPECT_EQ(HashPlane(gradient_output_image, 4, 4, 'Y'),
              UINT64_C(16325007039982934165));
    EXPECT_EQ(HashPlane(gradient_output_image, 2, 2, 'U'),
              UINT64_C(1667169561829713701));
    EXPECT_EQ(HashPlane(gradient_output_image, 2, 2, 'V'),
              UINT64_C(3219689328877945565));
    EXPECT_TRUE(gradient_output_image.GuardsIntact());
    EXPECT_TRUE(gradient_output_image.PaddingEquals(0xCC));

    canvas.Fill(0x37, 0x37, 0x37);
    const std::vector<uint8_t> before_invalid = canvas.Snapshot();
    MixSource invalid_sources[] = {sources[0], sources[1]};
    invalid_sources[1].destination.x = 9;
    EXPECT_EQ(MixYuv(context.get(), invalid_sources, 2, &output),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(canvas.Snapshot() == before_invalid);

    invalid_sources[1] = sources[1];
    invalid_sources[1].fill_mode = static_cast<FillMode>(99);
    EXPECT_EQ(MixYuv(context.get(), invalid_sources, 2, &output),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(canvas.Snapshot() == before_invalid);

    OwnedI420 layer_canvas(32, 32, 4);
    OwnedI420 layer_source(32, 32, 2);
    layer_canvas.Fill(16, 128, 128);
    layer_source.Fill(50, 100, 150);
    MixOutput layer_output = MakeOutput(&layer_canvas);
    std::unique_ptr<MixYuvContext> layer_context = CreateContext(18, 29, 2);
    MixSource decorated = {layer_source.ConstView(), {0, 0, 32, 32}, "M",
                           FillMode::kContain, true};
    EXPECT_EQ(MixYuv(layer_context.get(), &decorated, 1, &layer_output),
              MixYuvStatus::kOk);
    EXPECT_EQ(layer_canvas.Y(0, 0), 143);
    EXPECT_EQ(layer_canvas.Y(16, 16), 50);
    EXPECT_EQ(layer_canvas.U(0, 0), 110);
    EXPECT_EQ(layer_canvas.V(0, 0), 64);
    bool osd_visible = false;
    bool osd_over_border = false;
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const uint8_t value = layer_canvas.Y(x, y);
            osd_visible = osd_visible || (value != 50 && value != 143);
            const bool border_pixel = x == 0 || x == 31 || y == 0 || y == 31;
            osd_over_border = osd_over_border ||
                              (border_pixel && value > 143);
        }
    }
    EXPECT_TRUE(osd_visible);
    EXPECT_TRUE(osd_over_border);
    EXPECT_TRUE(layer_canvas.PaddingEquals(0xCC));
    EXPECT_TRUE(layer_canvas.GuardsIntact());

    layer_canvas.Fill(0x37, 0x37, 0x37);
    const std::vector<uint8_t> before_utf8 = layer_canvas.Snapshot();
    MixSource invalid_utf8[] = {decorated, decorated};
    invalid_utf8[1].display_name = std::string("\xF0\x28\x8C\x28", 4);
    EXPECT_EQ(MixYuv(layer_context.get(), invalid_utf8, 2, &layer_output),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(layer_canvas.Snapshot() == before_utf8);

    return yuvmix_test::Finish();
}
