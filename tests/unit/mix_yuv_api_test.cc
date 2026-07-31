#include <memory>

#include "video/mix_yuv.h"
#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

int main() {
    using namespace yuvmix;

    MixYuvConfig invalid;
    invalid.font_path = "/path/that/does/not/exist.ttf";
    invalid.font_face_index = 0;
    invalid.font_size = 24;
    invalid.osd_left = 12;
    invalid.osd_bottom = 12;
    std::unique_ptr<MixYuvContext> context;
    EXPECT_EQ(MixYuvContext::Create(invalid, &context),
              MixYuvStatus::kFontError);
    EXPECT_TRUE(context.get() == NULL);

    MixYuvConfig valid = invalid;
    valid.font_path = YUVMIX_TEST_FONT;
    EXPECT_EQ(MixYuvContext::Create(valid, &context), MixYuvStatus::kOk);
    EXPECT_TRUE(context.get() != NULL);
    EXPECT_EQ(MixYuv(NULL, NULL, 0, NULL), MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(MixYuv(context.get(), NULL, 1, NULL),
              MixYuvStatus::kInvalidArgument);

    yuvmix_test::OwnedI420 image(8, 6, 2);
    image.Fill(0x37, 0x37, 0x37);
    MixOutput output = {};
    output.image = image.MutableView();
    output.background_color = {16, 128, 128};
    EXPECT_EQ(MixYuv(context.get(), NULL, 0, &output), MixYuvStatus::kOk);
    EXPECT_TRUE(image.ActivePixelsEqual(16, 128, 128));
    EXPECT_TRUE(image.PaddingEquals(0xCC));
    EXPECT_TRUE(image.GuardsIntact());

    const std::vector<uint8_t> before_invalid = image.Snapshot();
    MixOutput invalid_output = output;
    invalid_output.image.width = 7;
    EXPECT_EQ(MixYuv(context.get(), NULL, 0, &invalid_output),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(image.Snapshot() == before_invalid);

    invalid_output = output;
    invalid_output.image.y.stride = 7;
    EXPECT_EQ(MixYuv(context.get(), NULL, 0, &invalid_output),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(image.Snapshot() == before_invalid);

    invalid_output = output;
    invalid_output.image.u.size =
        static_cast<size_t>(invalid_output.image.u.stride) * 2 + 3;
    EXPECT_EQ(MixYuv(context.get(), NULL, 0, &invalid_output),
              MixYuvStatus::kBufferTooSmall);
    EXPECT_TRUE(image.Snapshot() == before_invalid);

    invalid_output = output;
    invalid_output.image.v.data = NULL;
    EXPECT_EQ(MixYuv(context.get(), NULL, 0, &invalid_output),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(image.Snapshot() == before_invalid);

    return yuvmix_test::Finish();
}
