# MixYuv Four-Source 720p Artifact Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a C++11 integration test that sends four synthetic BT.709 720p I420 sources through `MixYuv` and produces `tests/mix-4-720.yuv`.

**Architecture:** A standalone test executable owns four padded source images and one padded output image, invokes the existing compositor once, validates layout/highlight/OSD behavior, then writes only active Y/U/V rows as a tightly-packed I420 frame. CTest writes its routine artifact into the build tree; a final explicit invocation writes the requested source-tree artifact.

**Tech Stack:** C++11, CMake/CTest, existing `MixYuv`, libyuv, FreeType, `OwnedI420`

---

## Preconditions

- Approved specification:
  `docs/superpowers/specs/2026-07-31-mixyuv-four-source-test-design.md`.
- The workspace is not a Git repository. Do not initialize Git. The checkpoint step
  records files instead of committing them.
- Local tests use `/System/Library/Fonts/SFNSMono.ttf` through
  `YUVMIX_TEST_FONT`.

## File Map

```text
tests/CMakeLists.txt
  Register the integration executable and pass a build-tree output path to CTest.

tests/integration/mix_yuv_four_source_test.cc
  Construct sources, call MixYuv, validate the frame, and serialize tightly-packed I420.

tests/mix-4-720.yuv
  Generated 1280x720 I420 artifact; exactly 1,382,400 bytes.
```

### Task 1: Add The Failing Integration Test

**Files:**
- Create: `tests/integration/mix_yuv_four_source_test.cc`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Register the integration test executable**

Append to `tests/CMakeLists.txt`:

```cmake
add_yuvmix_test(mix_yuv_four_source_test
    integration/mix_yuv_four_source_test.cc
    test_support/i420_test_image.cc)
set_tests_properties(mix_yuv_four_source_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
```

The executable itself supplies the default filename `mix-4-720.yuv`, so the CTest
working directory keeps routine output out of the source tree.

- [x] **Step 2: Write a deliberate failing test shell**

Create `tests/integration/mix_yuv_four_source_test.cc`:

```cpp
#include "test_support/test_assert.h"

int main() {
    EXPECT_TRUE(false);
    return yuvmix_test::Finish();
}
```

- [x] **Step 3: Build and observe RED**

Run:

```bash
cmake --build /private/tmp/yuvcomposite-build \
  --target mix_yuv_four_source_test --parallel 4
ctest --test-dir /private/tmp/yuvcomposite-build \
  -R '^mix_yuv_four_source_test$' --output-on-failure
```

Expected: build succeeds and CTest fails because `EXPECT_TRUE(false)` reports one
expectation failure. This proves the new CTest entry executes the new binary.

### Task 2: Implement The Four-Source Mix And Writer

**Files:**
- Modify: `tests/integration/mix_yuv_four_source_test.cc`

- [x] **Step 1: Replace the failing shell with the complete integration test**

Use this implementation:

```cpp
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/mix_yuv.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

namespace {

const uint32_t kOutputWidth = 1280;
const uint32_t kOutputHeight = 720;
const size_t kOutputSize =
    static_cast<size_t>(kOutputWidth) * kOutputHeight * 3 / 2;

bool WritePlane(std::ofstream* stream,
                const yuvmix::MutablePlane& plane,
                uint32_t row_bytes,
                uint32_t rows) {
    for (uint32_t row = 0; row < rows; ++row) {
        stream->write(
            reinterpret_cast<const char*>(
                plane.data + static_cast<size_t>(row) * plane.stride),
            static_cast<std::streamsize>(row_bytes));
        if (!*stream) {
            return false;
        }
    }
    return true;
}

bool WriteI420(const std::string& path,
               const yuvmix::MutableI420ImageView& image) {
    std::ofstream stream(path.c_str(),
                         std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream) {
        return false;
    }

    const bool written =
        WritePlane(&stream, image.y, image.width, image.height) &&
        WritePlane(&stream, image.u, image.width / 2, image.height / 2) &&
        WritePlane(&stream, image.v, image.width / 2, image.height / 2);
    stream.flush();
    const bool flushed = static_cast<bool>(stream);
    stream.close();
    const bool closed = !stream.fail();
    if (!written || !flushed || !closed) {
        std::remove(path.c_str());
        return false;
    }
    return true;
}

size_t FileSize(const std::string& path) {
    std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
    if (!stream) {
        return 0;
    }
    const std::ifstream::pos_type end = stream.tellg();
    if (end < 0) {
        return 0;
    }
    return static_cast<size_t>(end);
}

bool HasVisibleName(const yuvmix_test::OwnedI420& image,
                    const yuvmix::Rect& rect,
                    uint8_t media_y) {
    const uint32_t left = rect.x + 16;
    const uint32_t right = rect.x + 320;
    const uint32_t top = rect.y + rect.h - 64;
    const uint32_t bottom = rect.y + rect.h - 8;
    for (uint32_t y = top; y < bottom; ++y) {
        for (uint32_t x = left; x < right; ++x) {
            if (image.Y(x, y) != media_y) {
                return true;
            }
        }
    }
    return false;
}

void ExpectInteriorColor(const yuvmix_test::OwnedI420& image,
                         uint32_t x,
                         uint32_t y,
                         uint8_t expected_y,
                         uint8_t expected_u,
                         uint8_t expected_v) {
    EXPECT_EQ(image.Y(x, y), expected_y);
    EXPECT_EQ(image.U(x / 2, y / 2), expected_u);
    EXPECT_EQ(image.V(x / 2, y / 2), expected_v);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    EXPECT_TRUE(argc == 1 || argc == 2);
    if (argc != 1 && argc != 2) {
        return yuvmix_test::Finish();
    }
    const std::string output_path =
        argc == 2 ? argv[1] : "mix-4-720.yuv";

    MixYuvConfig config;
    config.font_path = YUVMIX_TEST_FONT;
    config.font_face_index = 0;
    config.font_size = 32;
    config.osd_left = 16;
    config.osd_bottom = 16;
    std::unique_ptr<MixYuvContext> context;
    EXPECT_EQ(MixYuvContext::Create(config, &context), MixYuvStatus::kOk);
    if (!context) {
        return yuvmix_test::Finish();
    }

    OwnedI420 red(1280, 720, 8);
    OwnedI420 blue(1280, 720, 8);
    OwnedI420 yellow(1280, 720, 8);
    OwnedI420 green(1280, 720, 8);
    red.Fill(63, 102, 240);
    blue.Fill(32, 240, 118);
    yellow.Fill(219, 16, 138);
    green.Fill(173, 42, 26);

    const Rect rects[] = {
        {0, 0, 640, 360},
        {640, 0, 640, 360},
        {0, 360, 640, 360},
        {640, 360, 640, 360},
    };
    MixSource sources[] = {
        {red.ConstView(), rects[0], "agora-yuv-1", FillMode::kContain, true},
        {blue.ConstView(), rects[1], "agora-yuv-2", FillMode::kContain, false},
        {yellow.ConstView(), rects[2], "agora-yuv-3", FillMode::kContain, false},
        {green.ConstView(), rects[3], "agora-yuv-4", FillMode::kContain, false},
    };

    OwnedI420 output_image(kOutputWidth, kOutputHeight, 8);
    MixOutput output = {};
    output.image = output_image.MutableView();
    output.background_color = {16, 128, 128};
    EXPECT_EQ(MixYuv(context.get(), sources, 4, &output), MixYuvStatus::kOk);

    ExpectInteriorColor(output_image, 320, 180, 63, 102, 240);
    ExpectInteriorColor(output_image, 960, 180, 32, 240, 118);
    ExpectInteriorColor(output_image, 320, 540, 219, 16, 138);
    ExpectInteriorColor(output_image, 960, 540, 173, 42, 26);
    EXPECT_EQ(output_image.Y(0, 0), 143);
    EXPECT_TRUE(HasVisibleName(output_image, rects[0], 63));
    EXPECT_TRUE(HasVisibleName(output_image, rects[1], 32));
    EXPECT_TRUE(HasVisibleName(output_image, rects[2], 219));
    EXPECT_TRUE(HasVisibleName(output_image, rects[3], 173));
    EXPECT_TRUE(output_image.GuardsIntact());
    EXPECT_TRUE(output_image.PaddingEquals(0xCC));

    if (yuvmix_test::FailureCount() != 0) {
        return yuvmix_test::Finish();
    }

    EXPECT_TRUE(WriteI420(output_path, output.image));
    EXPECT_EQ(FileSize(output_path), kOutputSize);
    if (FileSize(output_path) != kOutputSize) {
        std::remove(output_path.c_str());
    }
    return yuvmix_test::Finish();
}
```

- [x] **Step 2: Build and observe GREEN**

Run:

```bash
cmake --build /private/tmp/yuvcomposite-build \
  --target mix_yuv_four_source_test --parallel 4
ctest --test-dir /private/tmp/yuvcomposite-build \
  -R '^mix_yuv_four_source_test$' --output-on-failure
```

Expected: the test passes and
`/private/tmp/yuvcomposite-build/tests/mix-4-720.yuv` is exactly 1,382,400 bytes.

- [x] **Step 3: Run the complete test suite**

Run:

```bash
ctest --test-dir /private/tmp/yuvcomposite-build --output-on-failure
```

Expected: all eight tests pass.

### Task 3: Generate And Verify The Requested Artifact

**Files:**
- Create: `tests/mix-4-720.yuv`

- [x] **Step 1: Generate the source-tree artifact with the tested executable**

Run from the repository root:

```bash
/private/tmp/yuvcomposite-build/tests/mix_yuv_four_source_test \
  tests/mix-4-720.yuv
```

Expected: exit status 0.

- [x] **Step 2: Verify artifact size and checksum**

Run:

```bash
wc -c tests/mix-4-720.yuv
shasum -a 256 tests/mix-4-720.yuv
```

Expected size: `1382400`. Record the generated SHA-256 in the completion report; the
hash is environment-specific because the configured font rasterization is part of
the output.

- [x] **Step 3: Re-run sanitizer verification**

Run:

```bash
cmake --build /private/tmp/yuvcomposite-sanitize --parallel 4
ctest --test-dir /private/tmp/yuvcomposite-sanitize --output-on-failure
```

Expected: all eight tests pass with no AddressSanitizer or
UndefinedBehaviorSanitizer findings.

- [x] **Step 4: Record the checkpoint**

Because this workspace has no `.git` metadata, do not run `git add` or `git commit`.
Confirm the final changed/generated paths:

```text
tests/CMakeLists.txt
tests/integration/mix_yuv_four_source_test.cc
tests/mix-4-720.yuv
docs/superpowers/specs/2026-07-31-mixyuv-four-source-test-design.md
docs/superpowers/plans/2026-07-31-mixyuv-four-source-test.md
```
