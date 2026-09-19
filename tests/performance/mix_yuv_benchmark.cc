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

namespace {

double Measure(const char* label,
               yuvmix::MixYuvContext* context,
               yuvmix::MixSource* sources,
               size_t source_count,
               yuvmix::MixOutput* output) {
    for (int i = 0; i < 10; ++i) {
        if (yuvmix::MixYuv(context, sources, source_count, output) !=
            yuvmix::MixYuvStatus::kOk) {
            return -1.0;
        }
    }

    std::vector<double> elapsed_ms;
    elapsed_ms.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        const std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();
        if (yuvmix::MixYuv(context, sources, source_count, output) !=
            yuvmix::MixYuvStatus::kOk) {
            return -1.0;
        }
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        elapsed_ms.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(elapsed_ms.begin(), elapsed_ms.end());
    const double median = elapsed_ms[elapsed_ms.size() / 2];
    std::printf("%s median: %.3f ms (%.1f frames/s)\n",
                label, median, 1000.0 / median);
    return median;
}

}  // namespace

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    MixYuvConfig config;
    config.font_path = YUVMIX_TEST_FONT;
    config.font_face_index = 0;
    config.font_size = 32;
    config.osd_left = 16;
    config.osd_bottom = 16;
    config.osd_gap = 4;

    std::unique_ptr<MixYuvContext> context;
    if (MixYuvContext::Create(config, &context) != MixYuvStatus::kOk ||
        !context) {
        return 1;
    }

    OwnedI420 red(1280, 720, 8);
    OwnedI420 blue(1280, 720, 8);
    OwnedI420 yellow(1280, 720, 8);
    OwnedI420 green(1280, 720, 8);
    OwnedI420 network(16, 16, 2);
    OwnedI420 mic(12, 16, 2);
    OwnedI420 camera(20, 12, 2);
    red.Fill(63, 102, 240);
    blue.Fill(32, 240, 118);
    yellow.Fill(219, 16, 138);
    green.Fill(173, 42, 26);
    network.Fill(210, 40, 220);
    mic.Fill(180, 200, 30);
    camera.Fill(70, 150, 90);

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

    if (Measure("baseline", context.get(), sources, 4, &output) < 0.0) {
        return 1;
    }

    for (size_t i = 0; i < 4; ++i) {
        sources[i].network_quality_image = network.ConstView();
        sources[i].alpha_network_quality = 255;
        sources[i].is_network_quality = true;
        sources[i].mic_status_image = mic.ConstView();
        sources[i].alpha_mic_status = 128;
        sources[i].is_mic_status = true;
        sources[i].camera_status_image = camera.ConstView();
        sources[i].alpha_camera_status = 192;
        sources[i].is_camera_status = true;
    }
    if (Measure("three-icons", context.get(), sources, 4, &output) < 0.0) {
        return 1;
    }
    return 0;
}
