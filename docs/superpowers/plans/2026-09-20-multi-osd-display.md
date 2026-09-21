# Multi-OSD Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-source contain margin colors and three fixed-order, uniformly alpha-blended I420 OSD icons before the existing DisplayName.

**Architecture:** Add an internal `OsdRenderer` that owns `FreeTypeOsd`, validates and prepares fixed-capacity icon plans, clips icons to each destination, and draws icons before text. Keep contain margin fill in `mix_yuv.cc`, complete all validation and glyph preparation before output writes, and leave the standalone alpha-blend API unchanged.

**Tech Stack:** C++11, strict C11 public API, libyuv, FreeType, CMake/CTest, repository-local assertion harness.

**Design reference:** `docs/superpowers/specs/2026-09-20-multi-osd-display-design.md`

---

## File Map

- Create `src/video/i420_osd.h`: internal prepared icon/text plan and `OsdRenderer` interface.
- Create `src/video/i420_osd.cc`: icon validation, even-aligned layout, clipping, uniform I420 alpha blending, and text dispatch.
- Create `tests/unit/i420_osd_test.cc`: focused OSD validation, layout, clipping, alpha, and guard tests.
- Modify `src/video/mix_yuv.h`: append C++ config/source fields.
- Modify `src/video/mix_yuv_c.h`: append equivalent pure C fields and document the new caller contract.
- Modify `src/video/mix_yuv_c.cc`: map every new C field to C++.
- Modify `src/video/freetype_osd.h` and `.cc`: add explicit-coordinate text drawing while preserving legacy placement.
- Modify `src/video/mix_yuv.cc`: contain margin fill, OSD preflight, context ownership, and final render order.
- Modify `CMakeLists.txt` and `tests/CMakeLists.txt`: register the new module and unit target.
- Modify existing unit/integration tests and benchmarks to initialize `osd_gap`, enforce contracts, exercise the C adapter, and measure the icon path.

## Setup

- [ ] **Step 1: Configure a release build directory**

Run:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
```

Expected: configuration succeeds and writes `build-release/CMakeCache.txt`.

- [ ] **Step 2: Record the baseline test result**

Run:

```bash
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

Expected: every existing test passes before feature work begins.

### Task 1: Extend the Public C and C++ Structures

**Files:**
- Modify: `src/video/mix_yuv.h:61-93`
- Modify: `src/video/mix_yuv_c.h:62-94`
- Modify: `src/video/mix_yuv_c.cc:99-106`
- Modify: `tests/integration/mix_yuv_c_api_test.c:54-71`
- Modify: `tests/unit/mix_yuv_api_test.cc:14-27`
- Modify: `tests/unit/mix_yuv_test.cc:14-27`
- Modify: `tests/unit/freetype_osd_test.cc:39-46`
- Modify: `tests/unit/mix_yuv_contract_test.cc:48-58`
- Modify: `tests/integration/mix_yuv_four_source_test.cc:114-121`
- Modify: `tests/performance/mix_yuv_benchmark.cc:18-26`

- [ ] **Step 1: Add compile-time C API expectations that fail against the old header**

After the existing `fill_mode` assertion in `mix_yuv_c_api_test.c`, add:

```c
_Static_assert(_Generic(((yuvmix_config*)0)->osd_gap,
                        uint32_t: 1,
                        default: 0),
               "yuvmix_config.osd_gap must have uint32_t type");
_Static_assert(_Generic(((yuvmix_source*)0)->is_fill_margin_color,
                        int: 1,
                        default: 0),
               "is_fill_margin_color must have int type");
_Static_assert(_Generic(((yuvmix_source*)0)->is_network_quality,
                        int: 1,
                        default: 0),
               "is_network_quality must have int type");
_Static_assert(_Generic(((yuvmix_source*)0)->is_mic_status,
                        int: 1,
                        default: 0),
               "is_mic_status must have int type");
_Static_assert(_Generic(((yuvmix_source*)0)->is_camera_status,
                        int: 1,
                        default: 0),
               "is_camera_status must have int type");
```

- [ ] **Step 2: Build the strict C target and verify the new-field test fails**

Run:

```bash
cmake --build build-release --target mix_yuv_c_api_test -j
```

Expected: compilation fails because the new fields do not exist.

- [ ] **Step 3: Append the public fields exactly as approved**

Append to `MixSource` after `is_highlight` and to `MixYuvConfig` after
`osd_bottom`:

```cpp
// mix_yuv.h
struct MixSource {
    I420ImageView image;
    Rect destination;
    std::string display_name;
    FillMode fill_mode;
    bool is_highlight;

    uint8_t y_color;
    uint8_t u_color;
    uint8_t v_color;
    bool is_fill_margin_color;

    I420ImageView network_quality_image;
    uint8_t alpha_network_quality;
    bool is_network_quality;

    I420ImageView mic_status_image;
    uint8_t alpha_mic_status;
    bool is_mic_status;

    I420ImageView camera_status_image;
    uint8_t alpha_camera_status;
    bool is_camera_status;
};

struct MixYuvConfig {
    std::string font_path;
    uint32_t font_face_index;
    uint32_t font_size;
    uint32_t osd_left;
    uint32_t osd_bottom;
    uint32_t osd_gap;
};
```

Append these fields to the C structures:

```c
/* yuvmix_source, after is_highlight */
uint8_t y_color;
uint8_t u_color;
uint8_t v_color;
int is_fill_margin_color;

yuvmix_i420_image network_quality_image;
uint8_t alpha_network_quality;
int is_network_quality;

yuvmix_i420_image mic_status_image;
uint8_t alpha_mic_status;
int is_mic_status;

yuvmix_i420_image camera_status_image;
uint8_t alpha_camera_status;
int is_camera_status;

/* yuvmix_config, after osd_bottom */
uint32_t osd_gap;
```

Add a contract comment stating that enabled icons are borrowed, even-sized I420
images in the output color space, and source destinations must not overlap.

- [ ] **Step 4: Initialize `osd_gap` in every existing config fixture**

Add this assignment after `osd_bottom` in every C++ config listed above:

```cpp
config.osd_gap = 0;
```

For `mix_yuv_api_test.cc`, initialize `invalid.osd_gap = 0`. For the C designated
initializer, add:

```c
.osd_gap = 0,
```

Map the config value immediately so the future `OsdRenderer` never observes an
uninitialized C++ field:

```cpp
cpp_config.osd_gap = config->osd_gap;
```

- [ ] **Step 5: Build and run the API tests**

Run:

```bash
cmake --build build-release --target mix_yuv_api_test mix_yuv_c_api_test -j
ctest --test-dir build-release \
  -R '^(mix_yuv_api_test|mix_yuv_c_api_test)$' --output-on-failure
```

Expected: both tests pass; existing API function signatures and status/fill-mode
numeric values remain unchanged.

- [ ] **Step 6: Commit the public shape**

```bash
git add src/video/mix_yuv.h src/video/mix_yuv_c.h src/video/mix_yuv_c.cc \
  tests/integration/mix_yuv_c_api_test.c \
  tests/unit/mix_yuv_api_test.cc tests/unit/mix_yuv_test.cc \
  tests/unit/freetype_osd_test.cc tests/unit/mix_yuv_contract_test.cc \
  tests/integration/mix_yuv_four_source_test.cc \
  tests/performance/mix_yuv_benchmark.cc
git commit -m "feat: declare multi-osd source fields"
```

### Task 2: Let FreeType Draw at Explicit Coordinates

**Files:**
- Modify: `src/video/freetype_osd.h:31-44`
- Modify: `src/video/freetype_osd.cc:277-335`
- Modify: `tests/unit/freetype_osd_test.cc:92-153`

- [ ] **Step 1: Write a failing explicit-coordinate text test**

After constructing `synthetic_run`, clear the image and add:

```cpp
image.Fill(16, 128, 128);
osd->DrawTextAt(synthetic_run, full_rect, 20, 15, &output);
EXPECT_EQ(image.Y(21, 14), 16);  // coverage 0
EXPECT_EQ(image.Y(22, 14),
          static_cast<uint8_t>((1u * 235u + 254u * 16u + 127u) / 255u));
EXPECT_EQ(image.Y(29, 13), 235);  // second glyph: 20 + 10 - 1
EXPECT_EQ(image.U(10, 7), 128);
EXPECT_EQ(image.V(10, 7), 128);
```

- [ ] **Step 2: Run the focused test and verify it fails to compile**

Run:

```bash
cmake --build build-release --target freetype_osd_test -j
```

Expected: compilation fails because `DrawTextAt` does not exist.

- [ ] **Step 3: Add the explicit-coordinate API and preserve the old wrapper**

Declare:

```cpp
void DrawTextAt(const TextRun& run,
                const Rect& clip,
                int64_t pen_x,
                int64_t baseline_y,
                MutableI420ImageView* output) const;
int descender_pixels() const;
```

Remove the `YUVMIX_TESTING` guard around `descender_pixels()`. Refactor the
current pixel loop into `DrawTextAt`. Keep `DrawText` as:

```cpp
void FreeTypeOsd::DrawText(const TextRun& run,
                           const Rect& clip,
                           MutableI420ImageView* output) const {
    const int64_t pen_x = static_cast<int64_t>(clip.x) + impl_->osd_left;
    const int64_t baseline_y =
        static_cast<int64_t>(clip.y) + clip.h - impl_->osd_bottom -
        impl_->descender_pixels;
    DrawTextAt(run, clip, pen_x, baseline_y, output);
}
```

`DrawTextAt` must retain the existing destination clip, glyph bearing, advance,
coverage formula, Y-only writes, null-output guard, and 64-bit coordinates.

- [ ] **Step 4: Run the FreeType tests**

Run:

```bash
cmake --build build-release --target freetype_osd_test -j
ctest --test-dir build-release -R '^freetype_osd_test$' --output-on-failure
```

Expected: PASS, including both legacy `DrawText` and explicit `DrawTextAt` paths.

- [ ] **Step 5: Commit the text-coordinate seam**

```bash
git add src/video/freetype_osd.h src/video/freetype_osd.cc \
  tests/unit/freetype_osd_test.cc
git commit -m "refactor: support explicit osd text coordinates"
```

### Task 3: Fill Contain Margins with Per-Source I420 Colors

**Files:**
- Modify: `src/video/mix_yuv.cc:129-220`
- Modify: `tests/unit/mix_yuv_test.cc:96-135`

- [ ] **Step 1: Add failing horizontal and vertical margin tests**

Create an 8x8 output with background `(16,128,128)`, an 8x4 wide source, a 4x8
tall source, a 4x4 `small_4x4`, and an 8x8 `full_8x8`. Fill the four sources with
distinct constant I420 colors. Exercise them separately with these fields:

```cpp
MixSource wide = {};
wide.image = wide_image.ConstView();
wide.destination = {0, 0, 8, 8};
wide.fill_mode = FillMode::kContain;
wide.y_color = 25;
wide.u_color = 75;
wide.v_color = 125;
wide.is_fill_margin_color = true;

MixSource tall = {};
tall.image = tall_image.ConstView();
tall.destination = {0, 0, 8, 8};
tall.fill_mode = FillMode::kContain;
tall.y_color = 35;
tall.u_color = 85;
tall.v_color = 135;
tall.is_fill_margin_color = true;
```

Assert for `wide` that rows 0-1 and 6-7 contain `(25,75,125)` while rows 2-5
contain the main image. Assert for `tall` that columns 0-1 and 6-7 contain
`(35,85,135)` while columns 2-5 contain the main image. Also assert guards and
padding remain intact.

Add two regression cases:

```cpp
wide.is_fill_margin_color = false;
// Uncovered rows must remain output background (16, 128, 128).

MixSource cover_small = wide;
cover_small.image = small_4x4.ConstView();
cover_small.fill_mode = FillMode::kCover;
cover_small.is_fill_margin_color = true;
// Current no-upscale cover margins must remain output background.

MixSource contain_small = cover_small;
contain_small.fill_mode = FillMode::kContain;
// All four uncovered bands must use the source margin color, while the centered
// 4x4 main rectangle keeps the source image.

MixSource contain_full = wide;
contain_full.image = full_8x8.ConstView();
contain_full.fill_mode = FillMode::kContain;
contain_full.is_fill_margin_color = true;
// A draw rectangle covering destination must contain only main-image pixels.
```

- [ ] **Step 2: Run the focused test and verify the color assertions fail**

Run:

```bash
cmake --build build-release --target mix_yuv_test -j
ctest --test-dir build-release -R '^mix_yuv_test$' --output-on-failure
```

Expected: the new margin-color assertions fail because uncovered pixels still use
the output background.

- [ ] **Step 3: Add exact I420 rectangle-fill helpers**

In the anonymous namespace in `mix_yuv.cc`, add helpers with this behavior:

```cpp
void FillPlaneRect(MutablePlane* plane,
                   uint32_t x,
                   uint32_t y,
                   uint32_t width,
                   uint32_t height,
                   uint8_t value) {
    for (uint32_t row = 0; row < height; ++row) {
        uint8_t* begin = plane->data +
            static_cast<size_t>(y + row) * plane->stride + x;
        std::fill(begin, begin + width, value);
    }
}

void FillI420Rect(MutableI420ImageView* image,
                  const Rect& rect,
                  uint8_t y,
                  uint8_t u,
                  uint8_t v) {
    if (rect.w == 0 || rect.h == 0) {
        return;
    }
    FillPlaneRect(&image->y, rect.x, rect.y, rect.w, rect.h, y);
    FillPlaneRect(&image->u, rect.x / 2, rect.y / 2,
                  rect.w / 2, rect.h / 2, u);
    FillPlaneRect(&image->v, rect.x / 2, rect.y / 2,
                  rect.w / 2, rect.h / 2, v);
}
```

Add `FillContainMargins` with four non-overlapping bands:

```cpp
void FillContainMargins(const MixSource& source,
                        const GeometryPlan& geometry,
                        MutableI420ImageView* output) {
    if (!source.is_fill_margin_color ||
        source.fill_mode != FillMode::kContain) {
        return;
    }

    const Rect& outer = source.destination;
    const uint32_t draw_x = outer.x + geometry.dest_x;
    const uint32_t draw_y = outer.y + geometry.dest_y;
    const uint32_t draw_right = draw_x + geometry.dest_w;
    const uint32_t draw_bottom = draw_y + geometry.dest_h;
    const uint8_t y = source.y_color;
    const uint8_t u = source.u_color;
    const uint8_t v = source.v_color;

    FillI420Rect(output, {outer.x, outer.y, outer.w, geometry.dest_y},
                 y, u, v);
    FillI420Rect(output,
                 {outer.x, draw_bottom, outer.w,
                  outer.y + outer.h - draw_bottom},
                 y, u, v);
    FillI420Rect(output,
                 {outer.x, draw_y, geometry.dest_x, geometry.dest_h},
                 y, u, v);
    FillI420Rect(output,
                 {draw_right, draw_y,
                  outer.x + outer.w - draw_right, geometry.dest_h},
                 y, u, v);
}
```

- [ ] **Step 4: Call margin fill immediately before drawing each main source**

At the start of `DrawSource`, before `I420Copy`/`I420Scale`, call:

```cpp
FillContainMargins(source, geometry, &output->image);
```

Do not fill the main draw rectangle itself, and do not change `BuildGeometryPlan`.

- [ ] **Step 5: Run source and geometry tests**

Run:

```bash
cmake --build build-release --target mix_yuv_test i420_geometry_test -j
ctest --test-dir build-release \
  -R '^(mix_yuv_test|i420_geometry_test)$' --output-on-failure
```

Expected: PASS for horizontal/vertical colors, disabled fill, cover no-upscale,
guards, padding, and all existing geometry cases.

- [ ] **Step 6: Commit contain margin fill**

```bash
git add src/video/mix_yuv.cc tests/unit/mix_yuv_test.cc
git commit -m "feat: fill per-source contain margins"
```

### Task 4: Build the Internal OSD Renderer

**Files:**
- Create: `src/video/i420_osd.h`
- Create: `src/video/i420_osd.cc`
- Create: `tests/unit/i420_osd_test.cc`
- Modify: `CMakeLists.txt:35-41`
- Modify: `tests/CMakeLists.txt:31-43`

- [ ] **Step 1: Register a focused test that initially fails to build**

Add to `tests/CMakeLists.txt`:

```cmake
add_yuvmix_test(i420_osd_test
    unit/i420_osd_test.cc
    test_support/i420_test_image.cc)
```

Create `i420_osd_test.cc` with this prefix:

```cpp
#include <cstdint>
#include <memory>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/i420_osd.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;
```

Then add the first case:

```cpp
MixYuvConfig config = {};
config.font_path = YUVMIX_TEST_FONT;
config.font_size = 18;
config.osd_left = 3;
config.osd_bottom = 2;
config.osd_gap = 3;
std::unique_ptr<OsdRenderer> renderer;
EXPECT_EQ(OsdRenderer::Create(config, &renderer), MixYuvStatus::kOk);

OwnedI420 network(4, 4, 2);
OwnedI420 mic(6, 6, 2);
OwnedI420 camera(4, 2, 2);
network.Fill(210, 40, 220);
mic.Fill(180, 200, 30);
camera.Fill(70, 150, 90);

MixSource source = {};
source.destination = {0, 0, 32, 24};
source.network_quality_image = network.ConstView();
source.alpha_network_quality = 255;
source.is_network_quality = true;
source.mic_status_image = mic.ConstView();
source.alpha_mic_status = 255;
source.is_mic_status = true;
source.camera_status_image = camera.ConstView();
source.alpha_camera_status = 255;
source.is_camera_status = true;

OsdPlan plan = {};
EXPECT_EQ(renderer->Prepare(source, &plan), MixYuvStatus::kOk);
```

Assert:

```cpp
EXPECT_EQ(plan.icon_count, 3u);
EXPECT_EQ(plan.icons[0].destination.x, 4u);
EXPECT_EQ(plan.icons[1].destination.x, 12u);
EXPECT_EQ(plan.icons[2].destination.x, 22u);
EXPECT_EQ(plan.text_pen_x, INT64_C(29));
EXPECT_EQ(plan.baseline_y & INT64_C(1), INT64_C(0));
```

The x values prove inward alignment and the possible extra one-pixel inter-icon
spacing. Close the initial test with:

```cpp
    return yuvmix_test::Finish();
}
```

- [ ] **Step 2: Verify the new target fails because the module is absent**

Run:

```bash
cmake --build build-release --target i420_osd_test -j
```

Expected: compilation fails because `video/i420_osd.h` does not exist.

- [ ] **Step 3: Define the prepared-plan interface**

Create `i420_osd.h` with this internal API:

```cpp
#ifndef YUVMIX_VIDEO_I420_OSD_H_
#define YUVMIX_VIDEO_I420_OSD_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "video/freetype_osd.h"
#include "video/mix_yuv.h"

namespace yuvmix {

struct OsdIconPlan {
    I420ImageView image;
    uint32_t source_x;
    uint32_t source_y;
    Rect destination;
    uint8_t alpha;
};

struct OsdPlan {
    OsdIconPlan icons[3];
    size_t icon_count;
    TextRun text;
    Rect clip;
    int64_t text_pen_x;
    int64_t baseline_y;
};

class OsdRenderer {
public:
    static MixYuvStatus Create(const MixYuvConfig& config,
                               std::unique_ptr<OsdRenderer>* renderer);
    ~OsdRenderer();

    MixYuvStatus Prepare(const MixSource& source, OsdPlan* plan);
    void Draw(const OsdPlan& plan, MutableI420ImageView* output) const;
    size_t glyph_count() const;

    OsdRenderer(const OsdRenderer&) = delete;
    OsdRenderer& operator=(const OsdRenderer&) = delete;

private:
    struct Impl;
    explicit OsdRenderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace yuvmix

#endif  // YUVMIX_VIDEO_I420_OSD_H_
```

- [ ] **Step 4: Implement validation and fixed-order layout**

In `i420_osd.cc`, define ownership and creation explicitly:

```cpp
struct OsdRenderer::Impl {
    std::unique_ptr<FreeTypeOsd> font;
    uint32_t osd_left;
    uint32_t osd_bottom;
    uint32_t osd_gap;
};

MixYuvStatus OsdRenderer::Create(
    const MixYuvConfig& config,
    std::unique_ptr<OsdRenderer>* renderer) {
    if (renderer == NULL) {
        return MixYuvStatus::kInvalidArgument;
    }
    renderer->reset();
    try {
        std::unique_ptr<Impl> impl(new Impl());
        MixYuvStatus status = FreeTypeOsd::Create(config, &impl->font);
        if (status != MixYuvStatus::kOk) {
            return status;
        }
        impl->osd_left = config.osd_left;
        impl->osd_bottom = config.osd_bottom;
        impl->osd_gap = config.osd_gap;
        renderer->reset(new OsdRenderer(std::move(impl)));
        return MixYuvStatus::kOk;
    } catch (const std::bad_alloc&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (const std::length_error&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (...) {
        return MixYuvStatus::kInternalError;
    }
}

OsdRenderer::OsdRenderer(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

OsdRenderer::~OsdRenderer() {}

size_t OsdRenderer::glyph_count() const {
    return impl_->font->glyph_count();
}
```

`Prepare` must first validate all enabled icon views. Add the following helpers
in the `.cc` anonymous namespace (also include `<algorithm>`, `<cstring>`, and
`<limits>`, plus `<new>`, `<stdexcept>`, and `<utility>` for creation/error
mapping):

```cpp
bool RequiredPlaneSize(size_t stride,
                       size_t rows,
                       size_t row_bytes,
                       size_t* required) {
    if (required == NULL || rows == 0 || row_bytes == 0 ||
        stride < row_bytes) {
        return false;
    }
    const size_t preceding_rows = rows - 1;
    if (preceding_rows != 0 &&
        stride > (std::numeric_limits<size_t>::max() - row_bytes) /
                     preceding_rows) {
        return false;
    }
    *required = stride * preceding_rows + row_bytes;
    return true;
}

MixYuvStatus ValidatePlane(const ConstPlane& plane,
                           size_t rows,
                           size_t row_bytes) {
    if (plane.data == NULL || plane.stride <= 0 ||
        static_cast<size_t>(plane.stride) < row_bytes) {
        return MixYuvStatus::kInvalidArgument;
    }
    size_t required = 0;
    if (!RequiredPlaneSize(static_cast<size_t>(plane.stride), rows,
                           row_bytes, &required)) {
        return MixYuvStatus::kInvalidArgument;
    }
    return plane.size < required ? MixYuvStatus::kBufferTooSmall
                                 : MixYuvStatus::kOk;
}

MixYuvStatus ValidateIcon(const I420ImageView& image) {
    if (image.width < 2 || image.height < 2 ||
        (image.width & 1u) != 0 || (image.height & 1u) != 0 ||
        image.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        image.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return MixYuvStatus::kInvalidArgument;
    }
    MixYuvStatus status = ValidatePlane(image.y, image.height, image.width);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    status = ValidatePlane(image.u, image.height / 2, image.width / 2);
    return status == MixYuvStatus::kOk
        ? ValidatePlane(image.v, image.height / 2, image.width / 2)
        : status;
}
```

It must not inspect disabled views. After all enabled views validate, call
`PrepareText` on the existing `plan->text` so warmed vector capacity is retained.

Use these signed helpers and layout equations:

```cpp
int64_t FloorEven(int64_t value) {
    const int64_t remainder = value % 2;
    return remainder < 0 ? value - remainder - 2 : value - remainder;
}

int64_t CeilEven(int64_t value) {
    const int64_t remainder = value % 2;
    return remainder == 0
        ? value
        : (remainder > 0 ? value + 1 : value - remainder);
}

const int64_t raw_baseline =
    static_cast<int64_t>(source.destination.y) + source.destination.h -
    impl_->osd_bottom - impl_->font->descender_pixels();
```

If no enabled icon has alpha greater than zero, set legacy text coordinates and
`icon_count=0`. Otherwise floor the baseline to even, start the cursor at
`destination.x + osd_left`, and process Network, Mic, Camera. For each visible
icon, ceil the cursor to even, bottom-align the icon, intersect its signed full
rectangle with destination, store an `OsdIconPlan` only for a non-empty
intersection, and advance by the full width plus `osd_gap` even when fully
clipped. Set text x to the final cursor.

Use a fixed local descriptor array so validation and layout share the same order:

```cpp
struct IconInput {
    const I420ImageView* image;
    uint8_t alpha;
    bool enabled;
};

const IconInput inputs[] = {
    {&source.network_quality_image, source.alpha_network_quality,
     source.is_network_quality},
    {&source.mic_status_image, source.alpha_mic_status,
     source.is_mic_status},
    {&source.camera_status_image, source.alpha_camera_status,
     source.is_camera_status},
};
```

The body of `Prepare` uses the array without allocating:

```cpp
if (plan == NULL) {
    return MixYuvStatus::kInvalidArgument;
}
bool has_visible_icon = false;
for (size_t i = 0; i < 3; ++i) {
    if (!inputs[i].enabled) {
        continue;
    }
    const MixYuvStatus status = ValidateIcon(*inputs[i].image);
    if (status != MixYuvStatus::kOk) {
        return status;
    }
    has_visible_icon = has_visible_icon || inputs[i].alpha > 0;
}
const MixYuvStatus text_status =
    impl_->font->PrepareText(source.display_name, &plan->text);
if (text_status != MixYuvStatus::kOk) {
    return text_status;
}

plan->clip = source.destination;
plan->icon_count = 0;
const int64_t raw_baseline =
    static_cast<int64_t>(source.destination.y) + source.destination.h -
    impl_->osd_bottom - impl_->font->descender_pixels();
if (!has_visible_icon) {
    plan->text_pen_x =
        static_cast<int64_t>(source.destination.x) + impl_->osd_left;
    plan->baseline_y = raw_baseline;
    return MixYuvStatus::kOk;
}

plan->baseline_y = FloorEven(raw_baseline);
int64_t cursor_x =
    static_cast<int64_t>(source.destination.x) + impl_->osd_left;
for (size_t i = 0; i < 3; ++i) {
    const IconInput& input = inputs[i];
    if (!input.enabled || input.alpha == 0) {
        continue;
    }
    const int64_t icon_x = CeilEven(cursor_x);
    const int64_t icon_y =
        plan->baseline_y - static_cast<int64_t>(input.image->height);
    const int64_t left = std::max<int64_t>(icon_x, source.destination.x);
    const int64_t top = std::max<int64_t>(icon_y, source.destination.y);
    const int64_t right = std::min<int64_t>(
        icon_x + input.image->width,
        static_cast<int64_t>(source.destination.x) + source.destination.w);
    const int64_t bottom = std::min<int64_t>(
        icon_y + input.image->height,
        static_cast<int64_t>(source.destination.y) + source.destination.h);
    if (left < right && top < bottom) {
        OsdIconPlan& icon = plan->icons[plan->icon_count++];
        icon.image = *input.image;
        icon.source_x = static_cast<uint32_t>(left - icon_x);
        icon.source_y = static_cast<uint32_t>(top - icon_y);
        icon.destination = {
            static_cast<uint32_t>(left),
            static_cast<uint32_t>(top),
            static_cast<uint32_t>(right - left),
            static_cast<uint32_t>(bottom - top),
        };
        icon.alpha = input.alpha;
    }
    cursor_x = icon_x + input.image->width + impl_->osd_gap;
}
plan->text_pen_x = cursor_x;
return MixYuvStatus::kOk;
```

- [ ] **Step 5: Add failing alpha, clipping, and validation cases**

Expand `i420_osd_test.cc` with:

```cpp
// Disabled invalid image is ignored.
source.is_network_quality = false;
source.network_quality_image = I420ImageView();
EXPECT_EQ(renderer->Prepare(source, &plan), MixYuvStatus::kOk);

// Enabled alpha-zero image is validated but does not occupy space.
source.is_network_quality = true;
source.alpha_network_quality = 0;
source.network_quality_image = valid_icon.ConstView();
EXPECT_EQ(renderer->Prepare(source, &plan), MixYuvStatus::kOk);
EXPECT_EQ(plan.icon_count, 0u);
EXPECT_EQ(plan.text_pen_x,
          static_cast<int64_t>(source.destination.x) + config.osd_left);

source.network_quality_image.y.size = 1;
EXPECT_EQ(renderer->Prepare(source, &plan), MixYuvStatus::kBufferTooSmall);
```

Reset from the valid view before each mutation and add this validation matrix:

```text
width = 3           -> kInvalidArgument
height = 1          -> kInvalidArgument
y.data = NULL       -> kInvalidArgument
u.stride = 1        -> kInvalidArgument
v.size = 1          -> kBufferTooSmall
```

Add three concrete clipping cases using one visible Network icon:

- top clipping: a 4x32 icon in the existing 32x24 destination; assert
  `source_y > 0`, destination y is 0, and every stored coordinate/dimension is
  even;
- right clipping: create another renderer with `osd_left=29`, use an 8x4 icon,
  and assert destination x is 30, width is 2, height is 4, and source x is 0;
  and
- complete clipping: create a renderer with `osd_left=UINT32_MAX`, assert
  `icon_count == 0`, and assert `text_pen_x` still equals aligned icon x plus
  full width plus gap.

Add a second complete-clipping case with `osd_bottom=UINT32_MAX`. Assert the
signed baseline is negative and even, `icon_count == 0`, and `text_pen_x`
still advances normally. These two cases prove large unsigned margins are
promoted before arithmetic and cannot wrap content back into the destination.

For alpha 0, prepare and draw into output `(16,128,128)` and assert the complete
snapshot is unchanged. For alphas 1, 128, 254, and 255, fill an icon with
`(200,40,220)`, draw, and assert each plane equals:

```cpp
static uint8_t Blend(uint8_t source, uint8_t destination, uint8_t alpha) {
    return static_cast<uint8_t>(
        (source * alpha + destination * (255u - alpha) + 127u) / 255u);
}
```

Assert destination-exterior pixels, padding, and guards remain unchanged.

- [ ] **Step 6: Implement allocation-free clipped I420 drawing**

Implement a row helper over the original plane plus prepared source offsets:

```cpp
void BlendPlaneRegion(const ConstPlane& source,
                      uint32_t source_x,
                      uint32_t source_y,
                      uint32_t width,
                      uint32_t height,
                      uint8_t alpha,
                      MutablePlane* destination,
                      uint32_t destination_x,
                      uint32_t destination_y) {
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* source_row = source.data +
            static_cast<size_t>(source_y + row) * source.stride + source_x;
        uint8_t* destination_row = destination->data +
            static_cast<size_t>(destination_y + row) * destination->stride +
            destination_x;
        if (alpha == 255) {
            std::memcpy(destination_row, source_row, width);
            continue;
        }
        for (uint32_t column = 0; column < width; ++column) {
            const uint32_t mixed =
                source_row[column] * alpha +
                destination_row[column] * (255u - alpha);
            destination_row[column] =
                static_cast<uint8_t>((mixed + 127u) / 255u);
        }
    }
}
```

`Draw` loops over prepared icons in order. Call the helper with full coordinates
for Y and with every coordinate/dimension divided by two for U/V:

```cpp
const OsdIconPlan& icon = plan.icons[i];
BlendPlaneRegion(icon.image.y, icon.source_x, icon.source_y,
                 icon.destination.w, icon.destination.h, icon.alpha,
                 &output->y, icon.destination.x, icon.destination.y);
BlendPlaneRegion(icon.image.u, icon.source_x / 2, icon.source_y / 2,
                 icon.destination.w / 2, icon.destination.h / 2, icon.alpha,
                 &output->u, icon.destination.x / 2, icon.destination.y / 2);
BlendPlaneRegion(icon.image.v, icon.source_x / 2, icon.source_y / 2,
                 icon.destination.w / 2, icon.destination.h / 2, icon.alpha,
                 &output->v, icon.destination.x / 2, icon.destination.y / 2);
```

After all icons, call:

```cpp
impl_->font->DrawTextAt(plan.text, plan.clip,
                        plan.text_pen_x, plan.baseline_y, output);
```

Do not allocate, revalidate, rescale, or call the public `AlphaBlendI420` from
`Draw`.

- [ ] **Step 7: Add the implementation to the shared library and run tests**

Add `src/video/i420_osd.cc` to `yuvmix_video` in the top-level `CMakeLists.txt`.
Then run:

```bash
cmake --build build-release --target i420_osd_test freetype_osd_test -j
ctest --test-dir build-release \
  -R '^(i420_osd_test|freetype_osd_test)$' --output-on-failure
```

Expected: PASS for layout, disabled/alpha-zero semantics, clipping, exact Y/U/V
alpha values, text dispatch, padding, and guards.

- [ ] **Step 8: Commit the renderer**

```bash
git add CMakeLists.txt tests/CMakeLists.txt \
  src/video/i420_osd.h src/video/i420_osd.cc \
  tests/unit/i420_osd_test.cc
git commit -m "feat: add clipped i420 osd renderer"
```

### Task 5: Integrate OSD Plans into MixYuv

**Files:**
- Modify: `src/video/mix_yuv.cc:13-16,153-157,224-355`
- Modify: `tests/unit/mix_yuv_test.cc:180-219`

- [ ] **Step 1: Add failing end-to-end layer and legacy-position tests**

Extend the existing decorated-source test with three solid even-sized icons.
Use alpha 255 and distinct Y/U/V values. Set `osd_gap=3` in its context. Assert:

1. a pixel where the first icon crosses the left highlight border has the icon's
   exact Y/U/V, proving icons draw after highlights;
2. Network, Mic, and Camera colors appear at their planned x positions; and
3. pixels outside the source destination are unchanged.

First extend the local helper without changing existing callers:

```cpp
std::unique_ptr<yuvmix::MixYuvContext> CreateContext(
    uint32_t font_size = 24,
    uint32_t osd_left = 12,
    uint32_t osd_bottom = 12,
    uint32_t osd_gap = 0) {
    yuvmix::MixYuvConfig config;
    config.font_path = YUVMIX_TEST_FONT;
    config.font_face_index = 0;
    config.font_size = font_size;
    config.osd_left = osd_left;
    config.osd_bottom = osd_bottom;
    config.osd_gap = osd_gap;
    std::unique_ptr<yuvmix::MixYuvContext> context;
    EXPECT_EQ(yuvmix::MixYuvContext::Create(config, &context),
              yuvmix::MixYuvStatus::kOk);
    return context;
}
```

Keep the existing text/highlight context unchanged. Create an additional
`icon_context = CreateContext(18, 0, 0, 3)` and render a copy named
`icon_decorated` into a fresh 32x32 output.

Use this concrete setup (the odd gap exercises per-icon inward alignment):

```cpp
OwnedI420 network(4, 8, 2);
OwnedI420 mic(4, 6, 2);
OwnedI420 camera(6, 4, 2);
network.Fill(210, 40, 220);
mic.Fill(180, 200, 30);
camera.Fill(70, 150, 90);
MixSource icon_decorated = decorated;
icon_decorated.network_quality_image = network.ConstView();
icon_decorated.alpha_network_quality = 255;
icon_decorated.is_network_quality = true;
icon_decorated.mic_status_image = mic.ConstView();
icon_decorated.alpha_mic_status = 255;
icon_decorated.is_mic_status = true;
icon_decorated.camera_status_image = camera.ConstView();
icon_decorated.alpha_camera_status = 255;
icon_decorated.is_camera_status = true;
```

Scan the destination for the three exact chroma pairs `(40,220)`, `(200,30)`,
and `(150,90)`. At least one Network pixel on the destination's left border must
equal `(210,40,220)`, replacing the highlight's `(143,113,35)` values.

Add a legacy-position regression using two otherwise identical sources:

```cpp
MixSource no_icons = decorated;
MixSource alpha_zero_icon = decorated;
alpha_zero_icon.network_quality_image = network.ConstView();
alpha_zero_icon.alpha_network_quality = 0;
alpha_zero_icon.is_network_quality = true;
```

Render each into a fresh output and assert the complete snapshots are equal.
This proves an enabled alpha-zero icon validates but does not align or advance
the text.

Update the existing invalid UTF-8 atomicity case to use one source instead of
two identical overlapping destinations, so the test honors the new caller
precondition:

```cpp
MixSource invalid_utf8 = decorated;
invalid_utf8.display_name = std::string("\xF0\x28\x8C\x28", 4);
EXPECT_EQ(MixYuv(layer_context.get(), &invalid_utf8, 1, &layer_output),
          MixYuvStatus::kInvalidArgument);
EXPECT_TRUE(layer_canvas.Snapshot() == before_utf8);
```

- [ ] **Step 2: Run the MixYuv test and verify the icon assertions fail**

Run:

```bash
cmake --build build-release --target mix_yuv_test -j
ctest --test-dir build-release -R '^mix_yuv_test$' --output-on-failure
```

Expected: new icon assertions fail because MixYuv does not yet prepare/draw OSD
icons.

- [ ] **Step 3: Replace direct FreeType ownership with `OsdRenderer`**

In `mix_yuv.cc`:

```cpp
struct SourcePlan {
    const MixSource* source;
    GeometryPlan geometry;
    OsdPlan osd;
};

struct MixYuvContext::Impl {
    std::unique_ptr<OsdRenderer> osd;
    std::vector<SourcePlan> plans;
    std::vector<Rect> highlight_rects;
};
```

Include `video/i420_osd.h` instead of `video/freetype_osd.h`. In `Create`, call
`OsdRenderer::Create(config, &impl->osd)`.

- [ ] **Step 4: Prepare all OSD plans before output starts**

Replace the direct `PrepareText` call with:

```cpp
status = context->impl_->osd->Prepare(sources[i], &plan.osd);
if (status != MixYuvStatus::kOk) {
    return status;
}
```

Keep this inside the existing all-source preflight loop before `output_started =
true`. Continue collecting highlight rectangles after successful preparation.

- [ ] **Step 5: Draw each OSD plan after all highlights**

Replace the final direct `DrawText` loop with:

```cpp
for (size_t i = 0; i < context->impl_->plans.size(); ++i) {
    context->impl_->osd->Draw(context->impl_->plans[i].osd,
                              &output->image);
}
```

Make `MixYuvContextGlyphCount` delegate to `OsdRenderer::glyph_count()` so the
existing cache test remains valid.

- [ ] **Step 6: Run MixYuv unit and contract regressions**

Run:

```bash
cmake --build build-release --target mix_yuv_test mix_yuv_contract_test -j
ctest --test-dir build-release \
  -R '^(mix_yuv_test|mix_yuv_contract_test)$' --output-on-failure
```

Expected: PASS for render order, three icon colors, alpha-zero legacy layout,
existing glyph caching, and existing no-allocation behavior.

- [ ] **Step 7: Commit pipeline integration**

```bash
git add src/video/mix_yuv.cc tests/unit/mix_yuv_test.cc
git commit -m "feat: render icons before display names"
```

### Task 6: Wire the C Adapter and Enforce Failure Atomicity

**Files:**
- Modify: `src/video/mix_yuv_c.cc:99-157`
- Modify: `tests/integration/mix_yuv_c_api_test.c:204-348`
- Modify: `tests/unit/mix_yuv_contract_test.cc:89-149`

- [ ] **Step 1: Add failing strict-C icon and nonzero-flag coverage**

In `mix_yuv_c_api_test.c`, allocate Network 16x16, Mic 12x16, and Camera 20x12
images. Fill them with `(210,40,220)`, `(180,200,30)`, and `(70,150,90)` and
configure source zero as follows:

```c
config.osd_gap = 4;
sources[0].y_color = 20;
sources[0].u_color = 90;
sources[0].v_color = 170;
sources[0].is_fill_margin_color = 7; /* any nonzero value is true */
sources[0].network_quality_image = owned_i420_const_view(&network_icon);
sources[0].alpha_network_quality = 255;
sources[0].is_network_quality = -1;
sources[0].mic_status_image = owned_i420_const_view(&mic_icon);
sources[0].alpha_mic_status = 128;
sources[0].is_mic_status = 2;
sources[0].camera_status_image = owned_i420_const_view(&camera_icon);
sources[0].alpha_camera_status = 255;
sources[0].is_camera_status = 1;
```

Add this helper:

```c
static int has_chroma(const owned_i420* image,
                      uint32_t x,
                      uint32_t y,
                      uint32_t width,
                      uint32_t height,
                      uint8_t expected_u,
                      uint8_t expected_v) {
    uint32_t row;
    for (row = y / 2; row < (y + height) / 2; ++row) {
        uint32_t column;
        for (column = x / 2; column < (x + width) / 2; ++column) {
            const size_t offset =
                (size_t)row * (image->width / 2) + column;
            if (image->u[offset] == expected_u &&
                image->v[offset] == expected_v) {
                return 1;
            }
        }
    }
    return 0;
}
```

After a successful mix, assert source zero's destination contains exact Network
chroma `(40,220)`, exact Camera chroma `(150,90)`, and Mic chroma:

```c
const uint8_t mic_u = (uint8_t)((200u * 128u + 102u * 127u + 127u) / 255u);
const uint8_t mic_v = (uint8_t)((30u * 128u + 240u * 127u + 127u) / 255u);
EXPECT_TRUE(has_chroma(&output_image, 0, 0, 640, 360, 40, 220));
EXPECT_TRUE(has_chroma(&output_image, 0, 0, 640, 360, mic_u, mic_v));
EXPECT_TRUE(has_chroma(&output_image, 0, 0, 640, 360, 150, 90));
```

This observes image, alpha, flag, gap/order, and C-to-C++ mapping without relying
on font Y pixels. Destroy all three icon allocations in `cleanup`.

- [ ] **Step 2: Run the C integration test and verify the mapping assertion fails**

Run:

```bash
cmake --build build-release --target mix_yuv_c_api_test -j
ctest --test-dir build-release -R '^mix_yuv_c_api_test$' --output-on-failure
```

Expected: the icon-presence assertion fails because the adapter leaves C++ icon
fields zero.

- [ ] **Step 3: Map every new source field in the C adapter**

Keep the `cpp_config.osd_gap = config->osd_gap` assignment introduced in Task 1.
In the source conversion loop add direct colors/alpha assignments, normalize
flags, and convert all three views:

```cpp
cpp_source.y_color = sources[i].y_color;
cpp_source.u_color = sources[i].u_color;
cpp_source.v_color = sources[i].v_color;
cpp_source.is_fill_margin_color = sources[i].is_fill_margin_color != 0;

cpp_source.network_quality_image =
    ToCppImage(sources[i].network_quality_image);
cpp_source.alpha_network_quality = sources[i].alpha_network_quality;
cpp_source.is_network_quality = sources[i].is_network_quality != 0;

cpp_source.mic_status_image = ToCppImage(sources[i].mic_status_image);
cpp_source.alpha_mic_status = sources[i].alpha_mic_status;
cpp_source.is_mic_status = sources[i].is_mic_status != 0;

cpp_source.camera_status_image =
    ToCppImage(sources[i].camera_status_image);
cpp_source.alpha_camera_status = sources[i].alpha_camera_status;
cpp_source.is_camera_status = sources[i].is_camera_status != 0;
```

- [ ] **Step 4: Add late-invalid-icon atomicity and warmed allocation tests**

In `mix_yuv_contract_test.cc`, create valid 4x4 Network/Mic/Camera images and set
them on the warmed source. Run once before allocation counting so glyph and plan
capacities are stable, then retain the existing 100-call allocation loop and
expect zero allocations.

Add a two-source failure case where only the second source has an enabled icon
with `v.size = 1`:

```cpp
MixSource late_invalid[] = {source, source};
late_invalid[0].destination = {0, 0, 8, 8};
late_invalid[1].destination = {8, 0, 8, 8};
late_invalid[1].network_quality_image = network.ConstView();
late_invalid[1].alpha_network_quality = 255;
late_invalid[1].is_network_quality = true;
late_invalid[1].network_quality_image.v.size = 1;
ExpectSourcesRejected(context.get(), late_invalid, 2, &output16,
                      &output16_image, MixYuvStatus::kBufferTooSmall);
```

Add this multi-source helper next to `ExpectSourceRejected`:

```cpp
void ExpectSourcesRejected(yuvmix::MixYuvContext* context,
                           const yuvmix::MixSource* sources,
                           size_t source_count,
                           yuvmix::MixOutput* output,
                           yuvmix_test::OwnedI420* output_image,
                           yuvmix::MixYuvStatus expected) {
    const std::vector<uint8_t> before = output_image->Snapshot();
    EXPECT_EQ(yuvmix::MixYuv(context, sources, source_count, output), expected);
    EXPECT_TRUE(output_image->Snapshot() == before);
}
```

Also test disabled and alpha-zero validation explicitly:

```cpp
MixSource ignored_invalid = source;
ignored_invalid.network_quality_image.y.data = NULL;
ignored_invalid.is_network_quality = false;
EXPECT_EQ(MixYuv(context.get(), &ignored_invalid, 1, &output16),
          MixYuvStatus::kOk);

MixSource transparent_invalid = ignored_invalid;
transparent_invalid.is_network_quality = true;
transparent_invalid.alpha_network_quality = 0;
ExpectSourceRejected(context.get(), transparent_invalid, &output16,
                     &output16_image, MixYuvStatus::kInvalidArgument);
```

- [ ] **Step 5: Run C and contract tests**

Run:

```bash
cmake --build build-release --target \
  mix_yuv_c_api_test mix_yuv_contract_test -j
ctest --test-dir build-release \
  -R '^(mix_yuv_c_api_test|mix_yuv_contract_test)$' --output-on-failure
```

Expected: PASS for strict C11 compilation, nonzero flag normalization, visible
icons, late validation atomicity, alpha-zero validation, guards, and zero warmed
allocations.

- [ ] **Step 6: Commit adapter and contract coverage**

```bash
git add src/video/mix_yuv_c.cc \
  tests/integration/mix_yuv_c_api_test.c \
  tests/unit/mix_yuv_contract_test.cc
git commit -m "feat: expose multi-osd rendering through c api"
```

### Task 7: Add the Icon Benchmark and Run Release Gates

**Files:**
- Modify: `tests/performance/mix_yuv_benchmark.cc:14-80`

- [ ] **Step 1: Extend the benchmark with a three-icon-per-source case**

Create reusable solid icons, set `config.osd_gap = 4`, and add a measurement
helper that reports two labeled medians:

```cpp
double Measure(const char* label,
               MixYuvContext* context,
               MixSource* sources,
               size_t source_count,
               MixOutput* output) {
    std::vector<double> elapsed_ms;
    elapsed_ms.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        const std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();
        if (MixYuv(context, sources, source_count, output) !=
            MixYuvStatus::kOk) {
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
```

Measure once with all icon flags false, then enable Network/Mic/Camera on all
four sources with alphas 255/128/192 and measure again. Reuse the same read-only
icon views across sources. Treat a negative result as benchmark failure; do not
add a timing threshold.

- [ ] **Step 2: Build and run the benchmark**

Run:

```bash
cmake --build build-release --target mix_yuv_benchmark -j
./build-release/tests/mix_yuv_benchmark
```

Expected: exits zero and prints median milliseconds and frames/second for both
`baseline` and `three-icons`.

- [ ] **Step 3: Run the full release test suite**

Run:

```bash
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
git diff --check
```

Expected: all targets build, every test passes, and `git diff --check` prints no
errors.

- [ ] **Step 4: Run ASan and UBSan coverage**

Run:

```bash
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DYUVMIX_ENABLE_ASAN=ON \
  -DYUVMIX_ENABLE_UBSAN=ON \
  -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
```

Expected: all sanitizer tests pass without memory or undefined-behavior reports.

- [ ] **Step 5: Review the final public and behavioral diff**

Run:

```bash
git diff 84f77e0 -- src/video/mix_yuv.h src/video/mix_yuv_c.h
git diff 84f77e0 --stat
git status --short
```

Expected: public fields exactly match the approved design, the standalone alpha
blend API is unchanged, and only intended source/test/build files plus the
untracked visual-companion directories are present.

- [ ] **Step 6: Commit benchmark and final integration adjustments**

```bash
git add tests/performance/mix_yuv_benchmark.cc
git commit -m "perf: benchmark multi-osd rendering"
```

## Completion Criteria

- Every enabled icon is validated before output writes, even at alpha zero.
- Disabled and alpha-zero icons do not occupy layout space.
- Visible icons render Network, Mic, Camera, DisplayName in fixed order.
- Icon alpha blends Y/U/V exactly; text remains Y-only.
- Icons keep original even dimensions, align inward to even coordinates, clip to
  destination, and advance layout by full width plus gap.
- No visible icons preserve old DisplayName placement pixel-for-pixel.
- Per-source contain colors fill only uncovered contain bands; cover behavior is
  unchanged.
- Main images, highlights, icons, and text use the approved layer order.
- C and C++ callers receive matching fields; nonzero C flags normalize to true.
- Preflight failures preserve output, warmed repeated calls allocate nothing,
  guards and padding remain intact, all release/sanitizer tests pass, and the
  benchmark reports both baseline and icon-path medians.
