#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "test_support/i420_test_image.h"
#include "video/mix_yuv.h"

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    OwnedI420 source_a(640, 480, 8);
    OwnedI420 source_b(640, 480, 8);
    OwnedI420 source_c(640, 480, 8);
    OwnedI420 source_d(640, 480, 8);
    source_a.Fill(63, 102, 240);
    source_b.Fill(32, 240, 118);
    source_c.Fill(219, 16, 138);
    source_d.Fill(173, 42, 26);

    const I420BlendSource sources[] = {
        {source_a.ConstView(), 0, 0, 64},
        {source_b.ConstView(), 640, 0, 128},
        {source_c.ConstView(), 0, 480, 192},
        {source_d.ConstView(), 640, 480, 224},
    };
    OwnedI420 background_image(1920, 1080, 8);
    background_image.Fill(16, 128, 128);
    MutableI420ImageView background = background_image.MutableView();

    for (int i = 0; i < 20; ++i) {
        if (AlphaBlendI420(sources, 4, &background) != MixYuvStatus::kOk) {
            return 1;
        }
    }

    std::vector<double> samples;
    samples.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        const std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();
        const MixYuvStatus status = AlphaBlendI420(sources, 4, &background);
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        if (status != MixYuvStatus::kOk) {
            return 1;
        }
        samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());
    const double median_ms = samples[samples.size() / 2];
    const double frame_budget_ms = 1000.0 / 60.0;
    std::printf("AlphaBlendI420 median: %.3f ms; 60 fps budget: %.3f ms; "
                "remaining: %.3f ms\n",
                median_ms, frame_budget_ms, frame_budget_ms - median_ms);
    return 0;
}
