#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "test_support/i420_test_image.h"
#include "video/i420_highlight.h"

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    OwnedI420 image(1280, 720, 8);
    image.Fill(63, 102, 240);
    MutableI420ImageView output = image.MutableView();
    const Rect rect = {0, 0, 640, 360};

    for (int i = 0; i < 10; ++i) {
        if (DrawHighlights(&rect, 1, &output) != MixYuvStatus::kOk) {
            return 1;
        }
    }

    std::vector<double> samples;
    samples.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        const std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();
        const MixYuvStatus status = DrawHighlights(&rect, 1, &output);
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        if (status != MixYuvStatus::kOk) {
            return 1;
        }
        samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());
    std::printf("DrawHighlights median: %.6f ms\n",
                samples[samples.size() / 2]);
    return 0;
}
