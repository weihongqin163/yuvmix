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

    return yuvmix_test::Finish();
}
