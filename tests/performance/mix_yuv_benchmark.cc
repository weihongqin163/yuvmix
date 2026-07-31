#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

#include "test_support/i420_test_image.h"
#include "video/mix_yuv.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    MixYuvConfig config;
    config.font_path = YUVMIX_TEST_FONT;
    config.font_face_index = 0;
    config.font_size = 32;
    config.osd_left = 16;
    config.osd_bottom = 16;

    std::unique_ptr<MixYuvContext> context;
    if (MixYuvContext::Create(config, &context) != MixYuvStatus::kOk ||
        !context) {
        return 1;
    }

    OwnedI420 red(1280, 720, 8);
    OwnedI420 blue(1280, 720, 8);
    OwnedI420 yellow(1280, 720, 8);
    OwnedI420 green(1280, 720, 8);
    red.Fill(63, 102, 240);
    blue.Fill(32, 240, 118);
    yellow.Fill(219, 16, 138);
    green.Fill(173, 42, 26);

    MixSource sources[] = {
        {red.ConstView(), {0, 0, 640, 360}, "agora-yuv-1",
         FillMode::kContain, true},
        {blue.ConstView(), {640, 0, 640, 360}, "agora-yuv-2",
         FillMode::kContain, false},
        {yellow.ConstView(), {0, 360, 640, 360}, "agora-yuv-3",
         FillMode::kContain, false},
        {green.ConstView(), {640, 360, 640, 360}, "agora-yuv-4",
         FillMode::kContain, false},
    };

    OwnedI420 output_image(1280, 720, 8);
    MixOutput output = {};
    output.image = output_image.MutableView();
    output.background_color = {16, 128, 128};

    for (int i = 0; i < 10; ++i) {
        if (MixYuv(context.get(), sources, 4, &output) != MixYuvStatus::kOk) {
            return 1;
        }
    }

    std::vector<double> elapsed_ms;
    elapsed_ms.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        const std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();
        const MixYuvStatus status = MixYuv(context.get(), sources, 4, &output);
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        if (status != MixYuvStatus::kOk) {
            return 1;
        }
        elapsed_ms.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(elapsed_ms.begin(), elapsed_ms.end());
    std::printf("MixYuv median: %.3f ms\n",
                elapsed_ms[elapsed_ms.size() / 2]);
    return 0;
}
