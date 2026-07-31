#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/i420_highlight.h"

namespace {

void ExpectChromaEquals(const yuvmix_test::OwnedI420& image,
                        uint8_t expected_u,
                        uint8_t expected_v) {
    const yuvmix::I420ImageView view = image.ConstView();
    for (uint32_t y = 0; y < view.height / 2; ++y) {
        for (uint32_t x = 0; x < view.width / 2; ++x) {
            EXPECT_EQ(image.U(x, y), expected_u);
            EXPECT_EQ(image.V(x, y), expected_v);
        }
    }
}

}  // namespace

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    EXPECT_EQ(BorderWidth(162), 2u);
    EXPECT_EQ(BorderWidth(242), 3u);
    EXPECT_EQ(BorderWidth(558), 7u);
    EXPECT_EQ(BorderWidth(720), 9u);
    EXPECT_EQ(BorderWidth(838), 10u);
    EXPECT_EQ(BorderWidth(1080), 13u);

    OwnedI420 image(8, 8, 3);
    image.Fill(16, 128, 128);
    MutableI420ImageView output = image.MutableView();

    const Rect rect = {2, 2, 4, 4};
    EXPECT_EQ(DrawHighlights(&rect, 1, &output), MixYuvStatus::kOk);
    EXPECT_EQ(image.Y(0, 0), 16);
    EXPECT_EQ(image.Y(1, 1), 143);
    EXPECT_EQ(image.Y(2, 2), 143);
    EXPECT_EQ(image.Y(3, 3), 16);
    EXPECT_EQ(image.Y(4, 4), 16);
    EXPECT_EQ(image.Y(6, 6), 143);
    EXPECT_EQ(image.Y(7, 7), 16);
    ExpectChromaEquals(image, 128, 128);
    EXPECT_TRUE(image.PaddingEquals(0xCC));
    EXPECT_TRUE(image.GuardsIntact());

    OwnedI420 edge_image(8, 8, 3);
    edge_image.Fill(16, 128, 128);
    MutableI420ImageView edge_output = edge_image.MutableView();
    const Rect edge_rect = {0, 0, 4, 4};
    EXPECT_EQ(DrawHighlights(&edge_rect, 1, &edge_output), MixYuvStatus::kOk);
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
    EXPECT_EQ(DrawHighlights(adjacent_rects, 2, &union_output),
              MixYuvStatus::kOk);
    EXPECT_EQ(union_image.Y(3, 3), 143);
    EXPECT_EQ(union_image.Y(4, 3), 143);
    ExpectChromaEquals(union_image, 128, 128);
    EXPECT_TRUE(union_image.GuardsIntact());

    OwnedI420 narrow_image(8, 8, 3);
    narrow_image.Fill(16, 128, 128);
    MutableI420ImageView narrow_output = narrow_image.MutableView();
    const Rect narrow_rect = {2, 2, 2, 2};
    EXPECT_EQ(DrawHighlights(&narrow_rect, 1, &narrow_output),
              MixYuvStatus::kOk);
    EXPECT_EQ(narrow_image.Y(1, 1), 143);
    EXPECT_EQ(narrow_image.Y(4, 4), 143);
    EXPECT_EQ(narrow_image.Y(5, 5), 16);
    ExpectChromaEquals(narrow_image, 128, 128);
    EXPECT_TRUE(narrow_image.PaddingEquals(0xCC));
    EXPECT_TRUE(narrow_image.GuardsIntact());

    const std::vector<uint8_t> before_invalid = edge_image.Snapshot();
    const Rect invalid_rect = {1, 0, 4, 4};
    EXPECT_EQ(DrawHighlights(&invalid_rect, 1, &edge_output),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(edge_image.Snapshot() == before_invalid);
    EXPECT_EQ(DrawHighlights(NULL, 1, &edge_output),
              MixYuvStatus::kInvalidArgument);

    return yuvmix_test::Finish();
}
