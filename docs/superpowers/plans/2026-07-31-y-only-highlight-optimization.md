# Y-Only Highlight Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace output-sized highlight masking with allocation-free Y-plane-only border band fills and demonstrate the resulting four-source 720p performance improvement.

**Architecture:** Preserve the existing border width, half-inside/half-outside placement, clipping, validation, and Y value. Partition each border into up to four non-overlapping row-span bands and fill only those spans. Remove all highlight-specific U/V work and context mask storage; use a standalone Release benchmark target to measure the same 4x720p scenario without adding timing assertions to CTest.

**Tech Stack:** C++11, CMake 3.20+, I420 planar images, libyuv, FreeType, CTest.

---

## File Map

- `tests/performance/mix_yuv_benchmark.cc`: reproducible warm-cache four-source benchmark.
- `tests/CMakeLists.txt`: build the benchmark as a non-CTest executable.
- `tests/unit/i420_highlight_test.cc`: Y-only border geometry and no-U/V-write contract.
- `src/video/i420_highlight.h`: mask-free highlight rendering interface.
- `src/video/i420_highlight.cc`: clipped four-band Y writer.
- `tests/unit/mix_yuv_contract_test.cc`: context allocation contract without a highlight mask.
- `src/video/mix_yuv.h`: remove the testing-only mask-capacity hook.
- `src/video/mix_yuv.cc`: remove mask ownership, preallocation, and mask arguments.
- `docs/perform/perferm_test.md`: record the optimized algorithm and measured Release result.

### Task 1: Add A Reproducible Release Benchmark

**Files:**
- Create: `tests/performance/mix_yuv_benchmark.cc`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the benchmark executable**

Create `tests/performance/mix_yuv_benchmark.cc` with a four-source setup matching
the integration test. Warm up 10 times, measure 1000 individual `MixYuv` calls,
sort the durations, and print the median:

```cpp
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
    if (MixYuvContext::Create(config, &context) != MixYuvStatus::kOk) {
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

    const Rect rects[] = {
        {0, 0, 640, 360}, {640, 0, 640, 360},
        {0, 360, 640, 360}, {640, 360, 640, 360},
    };
    MixSource sources[] = {
        {red.ConstView(), rects[0], "agora-yuv-1", FillMode::kContain, true},
        {blue.ConstView(), rects[1], "agora-yuv-2", FillMode::kContain, false},
        {yellow.ConstView(), rects[2], "agora-yuv-3", FillMode::kContain, false},
        {green.ConstView(), rects[3], "agora-yuv-4", FillMode::kContain, false},
    };
    OwnedI420 output_image(1280, 720, 8);
    MixOutput output = {output_image.MutableView(), {16, 128, 128}};

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
        if (MixYuv(context.get(), sources, 4, &output) != MixYuvStatus::kOk) {
            return 1;
        }
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        elapsed_ms.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(elapsed_ms.begin(), elapsed_ms.end());
    std::printf("MixYuv median: %.3f ms\n", elapsed_ms[elapsed_ms.size() / 2]);
    return 0;
}
```

Append a non-CTest target to `tests/CMakeLists.txt`:

```cmake
add_executable(mix_yuv_benchmark
    performance/mix_yuv_benchmark.cc
    test_support/i420_test_image.cc)
target_compile_features(mix_yuv_benchmark PRIVATE cxx_std_11)
target_include_directories(mix_yuv_benchmark
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mix_yuv_benchmark
    PRIVATE YUVMIX_TEST_FONT="${YUVMIX_TEST_FONT}")
target_link_libraries(mix_yuv_benchmark PRIVATE yuvmix_video)
```

- [ ] **Step 2: Build and run the old implementation baseline**

Run:

```bash
cmake --build /tmp/yuvmix-test.ZAT6xe/yuvmix-build --target mix_yuv_benchmark --parallel
/tmp/yuvmix-test.ZAT6xe/yuvmix-build/tests/mix_yuv_benchmark
```

Expected: build succeeds and the old mask implementation reports a median near
the documented 0.97 ms baseline. Save the exact printed result for the final
performance report.

- [ ] **Step 3: Commit the benchmark harness**

```bash
git add tests/CMakeLists.txt tests/performance/mix_yuv_benchmark.cc
git commit -m "test: add four-source MixYuv benchmark"
```

### Task 2: Specify The Y-Only Border Contract

**Files:**
- Modify: `tests/unit/i420_highlight_test.cc`

- [ ] **Step 1: Replace mask and chroma assertions with the desired API**

Remove all `BlendHighlightChroma` and `EnsureHighlightMask` assertions and every
mask argument. Retain `<vector>` for snapshot comparisons. Call:

```cpp
EXPECT_EQ(DrawHighlights(&rect, 1, &output), MixYuvStatus::kOk);
```

For the 8x8 interior rectangle, retain the existing Y assertions and replace the
chroma expectations with unchanged-plane assertions:

```cpp
EXPECT_EQ(image.U(0, 0), 128);
EXPECT_EQ(image.V(0, 0), 128);
EXPECT_EQ(image.U(1, 1), 128);
EXPECT_EQ(image.V(1, 1), 128);
```

For adjacent rectangles, assert Y coverage and unchanged chroma:

```cpp
EXPECT_EQ(DrawHighlights(adjacent_rects, 2, &union_output),
          MixYuvStatus::kOk);
EXPECT_EQ(union_image.Y(3, 3), 143);
EXPECT_EQ(union_image.Y(4, 3), 143);
EXPECT_EQ(union_image.U(1, 1), 128);
EXPECT_EQ(union_image.V(1, 1), 128);
```

Add a collapsed-inner case that forces a full clipped outer rectangle:

```cpp
OwnedI420 narrow_image(8, 8, 3);
narrow_image.Fill(16, 128, 128);
MutableI420ImageView narrow_output = narrow_image.MutableView();
const Rect narrow_rect = {2, 2, 2, 2};
EXPECT_EQ(DrawHighlights(&narrow_rect, 1, &narrow_output),
          MixYuvStatus::kOk);
EXPECT_EQ(narrow_image.Y(1, 1), 143);
EXPECT_EQ(narrow_image.Y(4, 4), 143);
EXPECT_EQ(narrow_image.Y(5, 5), 16);
EXPECT_EQ(narrow_image.U(1, 1), 128);
EXPECT_EQ(narrow_image.V(1, 1), 128);
EXPECT_TRUE(narrow_image.PaddingEquals(0xCC));
EXPECT_TRUE(narrow_image.GuardsIntact());
```

Replace the short-mask validation block with invalid-rectangle no-write coverage:

```cpp
const std::vector<uint8_t> before_invalid = edge_image.Snapshot();
const Rect invalid_rect = {1, 0, 4, 4};
EXPECT_EQ(DrawHighlights(&invalid_rect, 1, &edge_output),
          MixYuvStatus::kInvalidArgument);
EXPECT_TRUE(edge_image.Snapshot() == before_invalid);
EXPECT_EQ(DrawHighlights(NULL, 1, &edge_output),
          MixYuvStatus::kInvalidArgument);
```

- [ ] **Step 2: Run the focused target and verify RED**

Run:

```bash
cmake --build /tmp/yuvmix-test.ZAT6xe/yuvmix-build --target i420_highlight_test --parallel
```

Expected: compilation fails because `DrawHighlights` still requires a mask and
the current implementation still exposes/uses mask and chroma helpers.

### Task 3: Implement Allocation-Free Four-Band Y Rendering

**Files:**
- Modify: `src/video/i420_highlight.h`
- Modify: `src/video/i420_highlight.cc`
- Modify: `src/video/mix_yuv.h`
- Modify: `src/video/mix_yuv.cc`

- [ ] **Step 1: Reduce the highlight header to the mask-free API**

Use this public surface in `src/video/i420_highlight.h`:

```cpp
uint32_t BorderWidth(uint32_t cell_height);

MixYuvStatus DrawHighlights(const Rect* rects,
                            size_t rect_count,
                            MutableI420ImageView* output);
```

Remove `<vector>`, `BlendHighlightChroma`, and `EnsureHighlightMask`.

- [ ] **Step 2: Implement clipped row-span fills**

In `src/video/i420_highlight.cc`, retain `ValidOutput`, `ValidRect`, and
`BorderWidth`; remove mask allocation, chroma constants, and U/V loops. Add:

```cpp
void FillBand(MutablePlane* y_plane,
              int64_t left,
              int64_t top,
              int64_t right,
              int64_t bottom) {
    if (left >= right || top >= bottom) {
        return;
    }
    const size_t width = static_cast<size_t>(right - left);
    for (int64_t y = top; y < bottom; ++y) {
        uint8_t* row = y_plane->data +
                       static_cast<size_t>(y) * y_plane->stride + left;
        std::fill_n(row, width, kHighlightY);
    }
}
```

After validating the complete rectangle array, calculate the existing outer and
inner bounds for each rectangle. If `inner_left >= inner_right` or
`inner_top >= inner_bottom`, fill the full outer rectangle. Otherwise fill:

```cpp
FillBand(&output->y, outer_left, outer_top, outer_right, inner_top);
FillBand(&output->y, outer_left, inner_bottom, outer_right, outer_bottom);
FillBand(&output->y, outer_left, inner_top, inner_left, inner_bottom);
FillBand(&output->y, inner_right, inner_top, outer_right, inner_bottom);
```

Clamp the inner coordinates to the outer rectangle before passing them to
`FillBand`; this preserves output-edge clipping and keeps every pointer within
the validated Y plane.

- [ ] **Step 3: Remove mask storage and preflight allocation from MixYuv**

Delete `highlight_mask` from `MixYuvContext::Impl`, remove the
`EnsureHighlightMask` preflight block, and call:

```cpp
status = DrawHighlights(context->impl_->highlight_rects.data(),
                        context->impl_->highlight_rects.size(),
                        &output->image);
```

Remove `MixYuvContextHighlightMaskCapacity` and its friend declaration from
`src/video/mix_yuv.cc` and `src/video/mix_yuv.h`.

- [ ] **Step 4: Build and run the focused test to verify GREEN**

Run:

```bash
cmake --build /tmp/yuvmix-test.ZAT6xe/yuvmix-build --target i420_highlight_test --parallel
/tmp/yuvmix-test.ZAT6xe/yuvmix-build/tests/i420_highlight_test
```

Expected: build succeeds and the focused test exits 0 with no failures.

- [ ] **Step 5: Commit the renderer change**

```bash
git add src/video/i420_highlight.h src/video/i420_highlight.cc \
  src/video/mix_yuv.h src/video/mix_yuv.cc tests/unit/i420_highlight_test.cc
git commit -m "perf: draw highlights as Y-only border bands"
```

### Task 4: Update The Context Contract

**Files:**
- Modify: `tests/unit/mix_yuv_contract_test.cc`

- [ ] **Step 1: Verify the removed mask hook breaks the old contract test**

Run:

```bash
cmake --build /tmp/yuvmix-test.ZAT6xe/yuvmix-build --target mix_yuv_contract_test --parallel
```

Expected: compilation fails because the test still references the removed
`MixYuvContextHighlightMaskCapacity` hook.

- [ ] **Step 2: Remove mask-capacity assertions**

Delete `mask_capacity8`, `mask_capacity32`, and all
`MixYuvContextHighlightMaskCapacity` calls. Preserve the repeated-call checks for
plan capacity, glyph count, and zero allocations. Keep the 32x32 then 16x16 calls
as output-size regression coverage without mask assertions.

- [ ] **Step 3: Build and run the contract test to verify GREEN**

Run:

```bash
cmake --build /tmp/yuvmix-test.ZAT6xe/yuvmix-build --target mix_yuv_contract_test --parallel
/tmp/yuvmix-test.ZAT6xe/yuvmix-build/tests/mix_yuv_contract_test
```

Expected: build succeeds and the test exits 0 with stable-state allocation count
equal to zero.

- [ ] **Step 4: Commit the contract update**

```bash
git add tests/unit/mix_yuv_contract_test.cc
git commit -m "test: update context contract for mask-free highlights"
```

### Task 5: Measure And Document The Improvement

**Files:**
- Modify: `docs/perform/perferm_test.md`

- [ ] **Step 1: Run the optimized Release benchmark**

Run:

```bash
cmake --build /tmp/yuvmix-test.ZAT6xe/yuvmix-build --target mix_yuv_benchmark --parallel
/tmp/yuvmix-test.ZAT6xe/yuvmix-build/tests/mix_yuv_benchmark
```

Expected: exit 0 and a median materially lower than the old implementation, with
the target result below 0.30 ms on the documented arm64 machine.

- [ ] **Step 2: Update the performance report with measured evidence**

In `docs/perform/perferm_test.md`:

- retain the original 0.97 ms result as the pre-optimization baseline;
- add the exact old and new benchmark medians printed by Task 1 and Task 5;
- replace the old hotspot description with four Y-band row fills;
- state that U/V are intentionally unchanged and the border is luminance-only;
- state that highlight work is proportional to border area and allocates no mask;
- do not claim results for architectures that were not measured.

- [ ] **Step 3: Commit the measured report**

```bash
git add docs/perform/perferm_test.md
git commit -m "docs: record Y-only highlight performance"
```

### Task 6: Full Verification

**Files:**
- Verify only; no planned modifications.

- [ ] **Step 1: Reconfigure the Release build**

Run:

```bash
cmake -S . -B /tmp/yuvmix-test.ZAT6xe/yuvmix-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_TESTING=ON \
  -DYUVMIX_LINK_DEPS_STATIC=ON \
  -DYUVMIX_DEPS_ROOT=/tmp/yuvmix-test.ZAT6xe/deps \
  -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
```

Expected: configure and generate succeed using the pinned static dependencies.

- [ ] **Step 2: Build every target**

Run:

```bash
cmake --build /tmp/yuvmix-test.ZAT6xe/yuvmix-build --parallel
```

Expected: all library, test, integration, and benchmark targets build.

- [ ] **Step 3: Run the complete test suite**

Run:

```bash
ctest --test-dir /tmp/yuvmix-test.ZAT6xe/yuvmix-build --output-on-failure
```

Expected: all 8 registered tests pass with 0 failures.

- [ ] **Step 4: Check source and repository hygiene**

Run:

```bash
git diff --check
git status --short --branch
```

Expected: `git diff --check` exits 0, the worktree is clean, and `main` is ahead
of `origin/main` only by the intentional local commits.
