#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/mix_yuv.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

namespace {

bool g_count_allocations = false;
size_t g_allocation_count = 0;

}  // namespace

void* operator new(size_t size) {
    if (void* memory = std::malloc(size)) {
        if (g_count_allocations) {
            ++g_allocation_count;
        }
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

namespace {

std::unique_ptr<yuvmix::MixYuvContext> CreateContext() {
    yuvmix::MixYuvConfig config;
    config.font_path = YUVMIX_TEST_FONT;
    config.font_face_index = 0;
    config.font_size = 18;
    config.osd_left = 2;
    config.osd_bottom = 2;
    std::unique_ptr<yuvmix::MixYuvContext> context;
    EXPECT_EQ(yuvmix::MixYuvContext::Create(config, &context),
              yuvmix::MixYuvStatus::kOk);
    return context;
}

yuvmix::MixOutput OutputFor(yuvmix_test::OwnedI420* image) {
    yuvmix::MixOutput output = {};
    output.image = image->MutableView();
    output.background_color = {16, 128, 128};
    return output;
}

void ExpectSourceRejected(yuvmix::MixYuvContext* context,
                          const yuvmix::MixSource& source,
                          yuvmix::MixOutput* output,
                          yuvmix_test::OwnedI420* output_image,
                          yuvmix::MixYuvStatus expected) {
    const std::vector<uint8_t> before = output_image->Snapshot();
    EXPECT_EQ(yuvmix::MixYuv(context, &source, 1, output), expected);
    EXPECT_TRUE(output_image->Snapshot() == before);
}

void ExpectOutputRejected(yuvmix::MixYuvContext* context,
                          yuvmix::MixOutput* output,
                          yuvmix_test::OwnedI420* output_image,
                          yuvmix::MixYuvStatus expected) {
    const std::vector<uint8_t> before = output_image->Snapshot();
    EXPECT_EQ(yuvmix::MixYuv(context, NULL, 0, output), expected);
    EXPECT_TRUE(output_image->Snapshot() == before);
}

}  // namespace

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    std::unique_ptr<MixYuvContext> context = CreateContext();
    OwnedI420 source_image(8, 8, 3);
    source_image.Fill(60, 100, 150);
    MixSource source = {source_image.ConstView(), {0, 0, 8, 8}, "Mix",
                        FillMode::kContain, true};

    OwnedI420 output8_image(8, 8, 3);
    MixOutput output8 = OutputFor(&output8_image);
    EXPECT_EQ(MixYuv(context.get(), &source, 1, &output8), MixYuvStatus::kOk);
    const size_t plan_capacity = MixYuvContextPreparedCapacity(*context);
    const size_t glyph_count = MixYuvContextGlyphCount(*context);
    const size_t mask_capacity8 = MixYuvContextHighlightMaskCapacity(*context);
    EXPECT_TRUE(plan_capacity >= 1);
    EXPECT_TRUE(glyph_count >= 3);
    EXPECT_TRUE(mask_capacity8 >= 64);

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(MixYuv(context.get(), &source, 1, &output8),
                  MixYuvStatus::kOk);
    }
    EXPECT_EQ(MixYuvContextPreparedCapacity(*context), plan_capacity);
    EXPECT_EQ(MixYuvContextGlyphCount(*context), glyph_count);
    EXPECT_EQ(MixYuvContextHighlightMaskCapacity(*context), mask_capacity8);

    g_allocation_count = 0;
    g_count_allocations = true;
    MixYuvStatus stable_status = MixYuvStatus::kOk;
    for (int i = 0; i < 100; ++i) {
        stable_status = MixYuv(context.get(), &source, 1, &output8);
        if (stable_status != MixYuvStatus::kOk) {
            break;
        }
    }
    g_count_allocations = false;
    EXPECT_EQ(stable_status, MixYuvStatus::kOk);
    EXPECT_EQ(g_allocation_count, static_cast<size_t>(0));

    OwnedI420 output32_image(32, 32, 3);
    MixOutput output32 = OutputFor(&output32_image);
    EXPECT_EQ(MixYuv(context.get(), &source, 1, &output32), MixYuvStatus::kOk);
    const size_t mask_capacity32 =
        MixYuvContextHighlightMaskCapacity(*context);
    EXPECT_TRUE(mask_capacity32 >= 1024);

    OwnedI420 output16_image(16, 16, 3);
    MixOutput output16 = OutputFor(&output16_image);
    EXPECT_EQ(MixYuv(context.get(), &source, 1, &output16), MixYuvStatus::kOk);
    EXPECT_EQ(MixYuvContextHighlightMaskCapacity(*context), mask_capacity32);

    output16_image.Fill(0x37, 0x37, 0x37);
    MixSource invalid = source;
    invalid.image.y.size = 1;
    ExpectSourceRejected(context.get(), invalid, &output16, &output16_image,
                         MixYuvStatus::kBufferTooSmall);

    invalid = source;
    invalid.image.u.size = 1;
    ExpectSourceRejected(context.get(), invalid, &output16, &output16_image,
                         MixYuvStatus::kBufferTooSmall);

    invalid = source;
    invalid.image.v.size = 1;
    ExpectSourceRejected(context.get(), invalid, &output16, &output16_image,
                         MixYuvStatus::kBufferTooSmall);

    for (int plane = 0; plane < 3; ++plane) {
        invalid = source;
        if (plane == 0) {
            invalid.image.y.data = NULL;
        } else if (plane == 1) {
            invalid.image.u.data = NULL;
        } else {
            invalid.image.v.data = NULL;
        }
        ExpectSourceRejected(context.get(), invalid, &output16,
                             &output16_image,
                             MixYuvStatus::kInvalidArgument);
    }

    for (int plane = 0; plane < 3; ++plane) {
        for (int stride = -1; stride <= 0; ++stride) {
            invalid = source;
            if (plane == 0) {
                invalid.image.y.stride = stride;
            } else if (plane == 1) {
                invalid.image.u.stride = stride;
            } else {
                invalid.image.v.stride = stride;
            }
            ExpectSourceRejected(context.get(), invalid, &output16,
                                 &output16_image,
                                 MixYuvStatus::kInvalidArgument);
        }
    }

    for (int plane = 0; plane < 3; ++plane) {
        invalid = source;
        if (plane == 0) {
            invalid.image.y.stride = 7;
        } else if (plane == 1) {
            invalid.image.u.stride = 3;
        } else {
            invalid.image.v.stride = 3;
        }
        ExpectSourceRejected(context.get(), invalid, &output16,
                             &output16_image,
                             MixYuvStatus::kInvalidArgument);
    }

    const uint32_t invalid_dimensions[] = {0, 1};
    for (size_t i = 0; i < 2; ++i) {
        invalid = source;
        invalid.image.width = invalid_dimensions[i];
        ExpectSourceRejected(context.get(), invalid, &output16,
                             &output16_image,
                             MixYuvStatus::kInvalidArgument);
        invalid = source;
        invalid.image.height = invalid_dimensions[i];
        ExpectSourceRejected(context.get(), invalid, &output16,
                             &output16_image,
                             MixYuvStatus::kInvalidArgument);
    }
    invalid = source;
    invalid.image.width = 7;
    ExpectSourceRejected(context.get(), invalid, &output16, &output16_image,
                         MixYuvStatus::kInvalidArgument);
    invalid = source;
    invalid.image.height = 7;
    ExpectSourceRejected(context.get(), invalid, &output16, &output16_image,
                         MixYuvStatus::kInvalidArgument);

    invalid = source;
    invalid.destination = {10, 10, 8, 8};
    ExpectSourceRejected(context.get(), invalid, &output16, &output16_image,
                         MixYuvStatus::kInvalidArgument);

    const Rect invalid_rects[] = {
        {1, 0, 8, 8},
        {0, 1, 8, 8},
        {0, 0, 0, 8},
        {0, 0, 1, 8},
        {0, 0, 3, 8},
        {0, 0, 8, 0},
        {0, 0, 8, 1},
        {0, 0, 8, 3},
        {std::numeric_limits<uint32_t>::max(), 0, 8, 8},
        {0, std::numeric_limits<uint32_t>::max(), 8, 8},
        {0, 0, std::numeric_limits<uint32_t>::max(), 8},
        {0, 0, 8, std::numeric_limits<uint32_t>::max()},
    };
    for (size_t i = 0; i < sizeof(invalid_rects) / sizeof(invalid_rects[0]);
         ++i) {
        invalid = source;
        invalid.destination = invalid_rects[i];
        ExpectSourceRejected(context.get(), invalid, &output16,
                             &output16_image,
                             MixYuvStatus::kInvalidArgument);
    }

    invalid = source;
    invalid.image.height =
        static_cast<uint32_t>(std::numeric_limits<int>::max()) + 1u;
    invalid.image.y.size = std::numeric_limits<size_t>::max();
    invalid.image.u.size = std::numeric_limits<size_t>::max();
    invalid.image.v.size = std::numeric_limits<size_t>::max();
    ExpectSourceRejected(context.get(), invalid, &output16, &output16_image,
                         MixYuvStatus::kInvalidArgument);

    MixOutput invalid_output = output16;
    invalid_output.image.height =
        static_cast<uint32_t>(std::numeric_limits<int>::max()) + 1u;
    invalid_output.image.y.size = std::numeric_limits<size_t>::max();
    invalid_output.image.u.size = std::numeric_limits<size_t>::max();
    invalid_output.image.v.size = std::numeric_limits<size_t>::max();
    ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                         MixYuvStatus::kInvalidArgument);

    for (size_t i = 0; i < 2; ++i) {
        invalid_output = output16;
        invalid_output.image.width = invalid_dimensions[i];
        ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                             MixYuvStatus::kInvalidArgument);
        invalid_output = output16;
        invalid_output.image.height = invalid_dimensions[i];
        ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                             MixYuvStatus::kInvalidArgument);
    }
    invalid_output = output16;
    invalid_output.image.width = 15;
    ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                         MixYuvStatus::kInvalidArgument);
    invalid_output = output16;
    invalid_output.image.height = 15;
    ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                         MixYuvStatus::kInvalidArgument);

    for (int plane = 0; plane < 3; ++plane) {
        invalid_output = output16;
        if (plane == 0) {
            invalid_output.image.y.data = NULL;
        } else if (plane == 1) {
            invalid_output.image.u.data = NULL;
        } else {
            invalid_output.image.v.data = NULL;
        }
        ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                             MixYuvStatus::kInvalidArgument);
    }

    for (int plane = 0; plane < 3; ++plane) {
        for (int stride = -1; stride <= 0; ++stride) {
            invalid_output = output16;
            if (plane == 0) {
                invalid_output.image.y.stride = stride;
            } else if (plane == 1) {
                invalid_output.image.u.stride = stride;
            } else {
                invalid_output.image.v.stride = stride;
            }
            ExpectOutputRejected(context.get(), &invalid_output,
                                 &output16_image,
                                 MixYuvStatus::kInvalidArgument);
        }
    }

    for (int plane = 0; plane < 3; ++plane) {
        invalid_output = output16;
        if (plane == 0) {
            invalid_output.image.y.stride = 15;
        } else if (plane == 1) {
            invalid_output.image.u.stride = 7;
        } else {
            invalid_output.image.v.stride = 7;
        }
        ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                             MixYuvStatus::kInvalidArgument);
    }

    for (int plane = 0; plane < 3; ++plane) {
        invalid_output = output16;
        if (plane == 0) {
            invalid_output.image.y.size = 1;
        } else if (plane == 1) {
            invalid_output.image.u.size = 1;
        } else {
            invalid_output.image.v.size = 1;
        }
        ExpectOutputRejected(context.get(), &invalid_output, &output16_image,
                             MixYuvStatus::kBufferTooSmall);
    }

    MixSource minimum_source = source;
    minimum_source.image.width = 2;
    minimum_source.image.height = 2;
    minimum_source.destination = {0, 0, 2, 2};
    minimum_source.display_name.clear();
    minimum_source.is_highlight = false;
    EXPECT_EQ(MixYuv(context.get(), &minimum_source, 1, &output16),
              MixYuvStatus::kOk);

    OwnedI420 minimum_output_image(2, 2, 3);
    MixOutput minimum_output = OutputFor(&minimum_output_image);
    EXPECT_EQ(MixYuv(context.get(), NULL, 0, &minimum_output),
              MixYuvStatus::kOk);
    EXPECT_TRUE(minimum_output_image.ActivePixelsEqual(16, 128, 128));
    EXPECT_TRUE(minimum_output_image.GuardsIntact());
    EXPECT_TRUE(minimum_output_image.PaddingEquals(0xCC));

    OwnedI420 second_image(8, 8, 3);
    second_image.Fill(80, 110, 160);
    MixSource highlighted[] = {
        {source_image.ConstView(), {0, 0, 8, 8}, "",
         FillMode::kContain, true},
        {second_image.ConstView(), {8, 0, 8, 8}, "",
         FillMode::kContain, true},
    };
    EXPECT_EQ(MixYuv(context.get(), highlighted, 2, &output16),
              MixYuvStatus::kOk);
    EXPECT_EQ(output16_image.Y(0, 0), 143);
    EXPECT_EQ(output16_image.Y(7, 0), 143);
    EXPECT_EQ(output16_image.Y(8, 0), 143);
    EXPECT_EQ(output16_image.Y(15, 0), 143);
    EXPECT_TRUE(output16_image.GuardsIntact());
    EXPECT_TRUE(output16_image.PaddingEquals(0xCC));

    MixSource large_source = source;
    large_source.destination = {0, 0, 640, 360};
    OwnedI420 output720_image(1280, 720, 3);
    MixOutput output720 = OutputFor(&output720_image);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(MixYuv(context.get(), &large_source, 1, &output720),
                  MixYuvStatus::kOk);
    }
    EXPECT_TRUE(output720_image.GuardsIntact());
    EXPECT_TRUE(output720_image.PaddingEquals(0xCC));

    OwnedI420 output1080_image(1920, 1080, 3);
    MixOutput output1080 = OutputFor(&output1080_image);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(MixYuv(context.get(), &large_source, 1, &output1080),
                  MixYuvStatus::kOk);
    }
    EXPECT_TRUE(output1080_image.GuardsIntact());
    EXPECT_TRUE(output1080_image.PaddingEquals(0xCC));

    std::unique_ptr<MixYuvContext> second_context = CreateContext();
    OwnedI420 parallel_first_image(640, 360, 3);
    OwnedI420 parallel_second_image(640, 360, 3);
    MixOutput parallel_first = OutputFor(&parallel_first_image);
    MixOutput parallel_second = OutputFor(&parallel_second_image);
    MixYuvStatus first_status = MixYuvStatus::kInternalError;
    MixYuvStatus second_status = MixYuvStatus::kInternalError;
    std::thread first_thread([&]() {
        first_status = MixYuv(context.get(), &large_source, 1,
                              &parallel_first);
    });
    std::thread second_thread([&]() {
        second_status = MixYuv(second_context.get(), &large_source, 1,
                               &parallel_second);
    });
    first_thread.join();
    second_thread.join();
    EXPECT_EQ(first_status, MixYuvStatus::kOk);
    EXPECT_EQ(second_status, MixYuvStatus::kOk);
    EXPECT_TRUE(parallel_first_image.GuardsIntact());
    EXPECT_TRUE(parallel_second_image.GuardsIntact());

    return yuvmix_test::Finish();
}
