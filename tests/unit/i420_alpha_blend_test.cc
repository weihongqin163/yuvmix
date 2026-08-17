#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/mix_yuv.h"

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    OwnedI420 background_image(4, 4, 3);
    background_image.Fill(20, 180, 60);
    MutableI420ImageView background = background_image.MutableView();
    const std::vector<uint8_t> before = background_image.Snapshot();

    EXPECT_EQ(AlphaBlendI420(NULL, 0, &background), MixYuvStatus::kOk);
    EXPECT_TRUE(background_image.Snapshot() == before);
    EXPECT_EQ(AlphaBlendI420(NULL, 1, &background),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(AlphaBlendI420(NULL, 0, NULL),
              MixYuvStatus::kInvalidArgument);

    OwnedI420 source_image(2, 2, 5);
    source_image.Fill(200, 40, 220);
    const std::vector<uint8_t> source_before = source_image.Snapshot();
    I420BlendSource source = {source_image.ConstView(), 2, 2, 0};

    struct BlendCase {
        uint8_t alpha;
        uint8_t y;
        uint8_t u;
        uint8_t v;
    };
    const BlendCase cases[] = {
        {0, 20, 180, 60},
        {1, 21, 179, 61},
        {128, 110, 110, 140},
        {254, 199, 41, 219},
        {255, 200, 40, 220},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        background_image.Fill(20, 180, 60);
        source.alpha = cases[i].alpha;
        EXPECT_EQ(AlphaBlendI420(&source, 1, &background),
                  MixYuvStatus::kOk);
        EXPECT_EQ(background_image.Y(2, 2), cases[i].y);
        EXPECT_EQ(background_image.Y(3, 3), cases[i].y);
        EXPECT_EQ(background_image.U(1, 1), cases[i].u);
        EXPECT_EQ(background_image.V(1, 1), cases[i].v);
        EXPECT_EQ(background_image.Y(0, 0), 20);
        EXPECT_EQ(background_image.U(0, 0), 180);
        EXPECT_EQ(background_image.V(0, 0), 60);
        EXPECT_TRUE(background_image.PaddingEquals(0xCC));
        EXPECT_TRUE(background_image.GuardsIntact());
        EXPECT_TRUE(source_image.Snapshot() == source_before);
    }

    OwnedI420 quadrant_background(8, 8, 7);
    quadrant_background.Fill(16, 128, 128);
    MutableI420ImageView quadrant_view = quadrant_background.MutableView();
    OwnedI420 source_a(4, 4, 1);
    OwnedI420 source_b(4, 4, 2);
    OwnedI420 source_c(4, 4, 3);
    OwnedI420 source_d(4, 4, 4);
    source_a.Fill(32, 64, 96);
    source_b.Fill(64, 96, 128);
    source_c.Fill(96, 128, 160);
    source_d.Fill(128, 160, 192);
    const I420BlendSource quadrants[] = {
        {source_a.ConstView(), 0, 0, 255},
        {source_b.ConstView(), 4, 0, 255},
        {source_c.ConstView(), 0, 4, 255},
        {source_d.ConstView(), 4, 4, 255},
    };
    EXPECT_EQ(AlphaBlendI420(quadrants, 4, &quadrant_view),
              MixYuvStatus::kOk);
    EXPECT_EQ(quadrant_background.Y(1, 1), 32);
    EXPECT_EQ(quadrant_background.Y(5, 1), 64);
    EXPECT_EQ(quadrant_background.Y(1, 5), 96);
    EXPECT_EQ(quadrant_background.Y(5, 5), 128);
    EXPECT_EQ(quadrant_background.U(3, 3), 160);
    EXPECT_EQ(quadrant_background.V(3, 3), 192);
    EXPECT_TRUE(quadrant_background.PaddingEquals(0xCC));
    EXPECT_TRUE(quadrant_background.GuardsIntact());
    EXPECT_TRUE(source_a.PaddingEquals(0xCC));
    EXPECT_TRUE(source_a.GuardsIntact());

    return yuvmix_test::Finish();
}
