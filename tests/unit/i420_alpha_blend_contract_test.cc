#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/mix_yuv.h"

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

void ExpectRejected(const yuvmix::I420BlendSource* sources,
                    size_t source_count,
                    yuvmix::MutableI420ImageView* background,
                    yuvmix_test::OwnedI420* image,
                    yuvmix::MixYuvStatus expected) {
    const std::vector<uint8_t> before = image->Snapshot();
    EXPECT_EQ(yuvmix::AlphaBlendI420(sources, source_count, background),
              expected);
    EXPECT_TRUE(image->Snapshot() == before);
}

}  // namespace

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    OwnedI420 source_image(4, 4, 3);
    source_image.Fill(200, 40, 220);
    OwnedI420 background_image(8, 8, 5);
    background_image.Fill(20, 180, 60);
    MutableI420ImageView background = background_image.MutableView();
    I420BlendSource valid = {source_image.ConstView(), 2, 2, 128};

    I420BlendSource invalid = valid;
    invalid.x = 1;
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);
    invalid = valid;
    invalid.y = 1;
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);
    invalid = valid;
    invalid.x = 6;
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);
    invalid = valid;
    invalid.y = std::numeric_limits<uint32_t>::max() - 1u;
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);

    for (int plane = 0; plane < 3; ++plane) {
        invalid = valid;
        if (plane == 0) {
            invalid.image.y.data = NULL;
        } else if (plane == 1) {
            invalid.image.u.data = NULL;
        } else {
            invalid.image.v.data = NULL;
        }
        ExpectRejected(&invalid, 1, &background, &background_image,
                       MixYuvStatus::kInvalidArgument);

        invalid = valid;
        if (plane == 0) {
            invalid.image.y.size = 1;
        } else if (plane == 1) {
            invalid.image.u.size = 1;
        } else {
            invalid.image.v.size = 1;
        }
        ExpectRejected(&invalid, 1, &background, &background_image,
                       MixYuvStatus::kBufferTooSmall);

        invalid = valid;
        if (plane == 0) {
            invalid.image.y.stride = 3;
        } else if (plane == 1) {
            invalid.image.u.stride = 1;
        } else {
            invalid.image.v.stride = 1;
        }
        ExpectRejected(&invalid, 1, &background, &background_image,
                       MixYuvStatus::kInvalidArgument);
    }

    invalid = valid;
    invalid.alpha = 0;
    invalid.image.y.data = NULL;
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);
    invalid = valid;
    invalid.image.width = 3;
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);
    invalid = valid;
    invalid.image.height = 3;
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);
    invalid = valid;
    invalid.image.width =
        static_cast<uint32_t>(std::numeric_limits<int>::max()) + 1u;
    invalid.image.y.size = std::numeric_limits<size_t>::max();
    invalid.image.u.size = std::numeric_limits<size_t>::max();
    invalid.image.v.size = std::numeric_limits<size_t>::max();
    ExpectRejected(&invalid, 1, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);

    for (int plane = 0; plane < 3; ++plane) {
        MutableI420ImageView invalid_background = background;
        if (plane == 0) {
            invalid_background.y.data = NULL;
        } else if (plane == 1) {
            invalid_background.u.data = NULL;
        } else {
            invalid_background.v.data = NULL;
        }
        ExpectRejected(NULL, 0, &invalid_background, &background_image,
                       MixYuvStatus::kInvalidArgument);

        invalid_background = background;
        if (plane == 0) {
            invalid_background.y.size = 1;
        } else if (plane == 1) {
            invalid_background.u.size = 1;
        } else {
            invalid_background.v.size = 1;
        }
        ExpectRejected(NULL, 0, &invalid_background, &background_image,
                       MixYuvStatus::kBufferTooSmall);
    }

    MutableI420ImageView invalid_background = background;
    invalid_background.y.stride = 7;
    ExpectRejected(NULL, 0, &invalid_background, &background_image,
                   MixYuvStatus::kInvalidArgument);
    invalid_background = background;
    invalid_background.width = 7;
    ExpectRejected(NULL, 0, &invalid_background, &background_image,
                   MixYuvStatus::kInvalidArgument);

    const I420BlendSource late_invalid[] = {
        valid,
        {source_image.ConstView(), 6, 6, 255},
    };
    ExpectRejected(late_invalid, 2, &background, &background_image,
                   MixYuvStatus::kInvalidArgument);

    g_allocation_count = 0;
    g_count_allocations = true;
    const MixYuvStatus allocation_status =
        AlphaBlendI420(&valid, 1, &background);
    g_count_allocations = false;
    EXPECT_EQ(allocation_status, MixYuvStatus::kOk);
    EXPECT_EQ(g_allocation_count, static_cast<size_t>(0));

    OwnedI420 background_a(8, 8, 2);
    OwnedI420 background_b(8, 8, 2);
    background_a.Fill(20, 180, 60);
    background_b.Fill(20, 180, 60);
    MutableI420ImageView view_a = background_a.MutableView();
    MutableI420ImageView view_b = background_b.MutableView();
    MixYuvStatus status_a = MixYuvStatus::kInternalError;
    MixYuvStatus status_b = MixYuvStatus::kInternalError;
    std::thread thread_a([&]() {
        status_a = AlphaBlendI420(&valid, 1, &view_a);
    });
    std::thread thread_b([&]() {
        status_b = AlphaBlendI420(&valid, 1, &view_b);
    });
    thread_a.join();
    thread_b.join();
    EXPECT_EQ(status_a, MixYuvStatus::kOk);
    EXPECT_EQ(status_b, MixYuvStatus::kOk);
    EXPECT_TRUE(background_a.Snapshot() == background_b.Snapshot());
    EXPECT_TRUE(background_a.GuardsIntact());
    EXPECT_TRUE(background_b.GuardsIntact());

    return yuvmix_test::Finish();
}
