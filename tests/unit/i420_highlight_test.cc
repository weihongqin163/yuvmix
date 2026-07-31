#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/i420_highlight.h"

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    EXPECT_EQ(BorderWidth(162), 2u);
    EXPECT_EQ(BorderWidth(242), 3u);
    EXPECT_EQ(BorderWidth(558), 7u);
    EXPECT_EQ(BorderWidth(720), 9u);
    EXPECT_EQ(BorderWidth(838), 10u);
    EXPECT_EQ(BorderWidth(1080), 13u);

    EXPECT_EQ(BlendHighlightChroma(128, 113, 0), 128);
    EXPECT_EQ(BlendHighlightChroma(128, 113, 1), 124);
    EXPECT_EQ(BlendHighlightChroma(128, 113, 2), 121);
    EXPECT_EQ(BlendHighlightChroma(128, 113, 3), 117);
    EXPECT_EQ(BlendHighlightChroma(128, 113, 4), 113);
    EXPECT_EQ(BlendHighlightChroma(128, 35, 1), 105);
    EXPECT_EQ(BlendHighlightChroma(128, 35, 3), 58);

    OwnedI420 image(8, 8, 3);
    image.Fill(16, 128, 128);
    MutableI420ImageView output = image.MutableView();
    std::vector<uint8_t> mask;
    EXPECT_EQ(EnsureHighlightMask(8, 8, &mask), MixYuvStatus::kOk);
    EXPECT_TRUE(mask.size() >= 64);

    const Rect rect = {2, 2, 4, 4};
    EXPECT_EQ(DrawHighlights(&rect, 1, &output, &mask), MixYuvStatus::kOk);
    EXPECT_EQ(image.Y(0, 0), 16);
    EXPECT_EQ(image.Y(1, 1), 143);
    EXPECT_EQ(image.Y(2, 2), 143);
    EXPECT_EQ(image.Y(3, 3), 16);
    EXPECT_EQ(image.Y(4, 4), 16);
    EXPECT_EQ(image.Y(6, 6), 143);
    EXPECT_EQ(image.Y(7, 7), 16);
    EXPECT_EQ(image.U(0, 0), 124);
    EXPECT_EQ(image.V(0, 0), 105);
    EXPECT_EQ(image.U(1, 1), 117);
    EXPECT_EQ(image.V(1, 1), 58);
    EXPECT_TRUE(image.PaddingEquals(0xCC));
    EXPECT_TRUE(image.GuardsIntact());

    OwnedI420 edge_image(8, 8, 3);
    edge_image.Fill(16, 128, 128);
    MutableI420ImageView edge_output = edge_image.MutableView();
    const Rect edge_rect = {0, 0, 4, 4};
    EXPECT_EQ(DrawHighlights(&edge_rect, 1, &edge_output, &mask),
              MixYuvStatus::kOk);
    EXPECT_EQ(edge_image.Y(0, 0), 143);
    EXPECT_EQ(edge_image.Y(1, 1), 16);
    EXPECT_EQ(edge_image.Y(4, 4), 143);
    EXPECT_EQ(edge_image.Y(5, 5), 16);
    EXPECT_TRUE(edge_image.GuardsIntact());

    OwnedI420 union_image(8, 8, 3);
    union_image.Fill(16, 128, 128);
    MutableI420ImageView union_output = union_image.MutableView();
    const Rect adjacent_rects[] = {
        {0, 0, 4, 4},
        {4, 0, 4, 4},
    };
    EXPECT_EQ(DrawHighlights(adjacent_rects, 2, &union_output, &mask),
              MixYuvStatus::kOk);
    EXPECT_EQ(union_image.U(1, 1), BlendHighlightChroma(128, 113, 3));
    EXPECT_EQ(union_image.V(1, 1), BlendHighlightChroma(128, 35, 3));
    EXPECT_NE(union_image.U(1, 1),
              BlendHighlightChroma(
                  BlendHighlightChroma(128, 113, 3), 113, 3));
    EXPECT_NE(union_image.V(1, 1),
              BlendHighlightChroma(
                  BlendHighlightChroma(128, 35, 3), 35, 3));
    EXPECT_TRUE(union_image.GuardsIntact());

    std::vector<uint8_t> short_mask(63, 0);
    const std::vector<uint8_t> before_short = edge_image.Snapshot();
    EXPECT_EQ(DrawHighlights(&edge_rect, 1, &edge_output, &short_mask),
              MixYuvStatus::kBufferTooSmall);
    EXPECT_TRUE(edge_image.Snapshot() == before_short);

    EXPECT_EQ(EnsureHighlightMask(8, 8, NULL),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(DrawHighlights(NULL, 1, &edge_output, &mask),
              MixYuvStatus::kInvalidArgument);

    return yuvmix_test::Finish();
}
