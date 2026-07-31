# MixYuv I420 Compositor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved internal C++11 `MixYuv` API that composites caller-owned I420 sources with libyuv, then draws unioned highlight borders and FreeType nickname OSD into a caller-owned I420 output.

**Architecture:** A preflight phase validates every buffer and builds immutable geometry/text plans before output is touched. A layered render phase fills the background, writes every source directly into its destination with libyuv, draws the union of all highlight borders, and finally blends OSD into the Y plane. `MixYuvContext` owns the fixed FreeType configuration, glyph cache, render-plan capacity, and reusable highlight mask; one context is used serially.

**Tech Stack:** C++11, CMake 3.20+, libyuv, FreeType 2, CTest, AddressSanitizer, UndefinedBehaviorSanitizer

---

## Execution Status (2026-07-31)

- Tasks 1-7 are implemented and covered by focused tests.
- Task 8 verification passes locally in Debug, Release with static dependencies,
  strict `-Wall -Wextra -Wpedantic -Werror`, and combined ASan/UBSan builds.
- The contract suite covers preflight atomicity, per-plane validation, Rect boundaries,
  720p/1080p repetition, independent-context concurrency, guard bytes, and 100 stable
  calls with zero C++ heap allocations.
- Static platform selection is covered by a CMake mapping test. The GitHub Actions
  matrix defines static builds for macOS x86_64, macOS arm64, and Linux x86_64; its
  first remote run remains pending because this workspace is not a Git checkout.
- Commit checkpoint steps are intentionally not executed because the workspace has no
  Git metadata. All other checkboxes below describe the original task sequence; this
  section is the authoritative execution record.

## Execution Preconditions

- The approved specification is `docs/superpowers/specs/2026-07-31-mixyuv-design.md`.
- The current directory is not a Git repository. Do not run `git init` without user approval. Each task includes the intended commit checkpoint; execute those commands only after the workspace is provided as a Git checkout.
- CMake 4.3.2 and a system FreeType installation are currently discoverable.
- Production builds consume platform-specific static libyuv/FreeType packages for
  macOS x86_64, macOS arm64, and Linux x86_64. Local development may explicitly use
  system packages while the prebuilt package is being produced.
- Local verification built libyuv revision
  `b56492e2dfc064f65ef27fed9c45d9bbfc2e2ad2` and FreeType 2.13.3 as arm64 static
  archives. Release static tests consume them through `YUVMIX_DEPS_ROOT`.
- Local OSD tests use `/System/Library/Fonts/SFNSMono.ttf`. CI must provision one
  pinned font artifact with a fixed checksum and pass its path as `YUVMIX_TEST_FONT`
  on macOS and Linux so glyph metrics do not vary by runner image.

## Goal Breakdown

1. **G1 Buildable API:** Create the CMake target, internal API, status model, test harness, and FreeType-backed context lifecycle.
2. **G2 Deterministic Geometry:** Implement overflow-safe, integer-only Contain/Cover planning with FloorEven.
3. **G3 Safe I420 Buffers:** Validate three-plane views and fill only active output pixels without touching padding.
4. **G4 libyuv Composition:** Copy or bilinear-scale every source directly into its planned output region.
5. **G5 Correct Highlighting:** Render all border masks as a union and apply exact Y and coverage-blended U/V values.
6. **G6 FreeType OSD:** Strictly decode UTF-8, cache grayscale glyphs, place text from face metrics, and blend Y only.
7. **G7 Layered Integration:** Enforce preflight-before-write and the background, media, highlight, OSD render order.
8. **G8 Release Evidence:** Pass unit, integration, sanitizer, guard-byte, and allocation-reuse verification.

## Target File Map

```text
CMakeLists.txt                         Project, dependency lookup, library target
cmake/FindLibYuv.cmake                Imported LibYuv::LibYuv target
cmake/YuvMixDependencies.cmake        Platform mapping and static imported targets
src/video/mix_yuv.h                   Internal API and caller-owned view types
src/video/mix_yuv.cc                  Validation, preflight, layered orchestration
src/video/i420_geometry.h             GeometryPlan and integer geometry API
src/video/i420_geometry.cc            Contain/Cover and FloorEven implementation
src/video/i420_highlight.h            Highlight rendering API
src/video/i420_highlight.cc           Union mask and I420 coverage blend
src/video/freetype_osd.h              TextRun and FreeTypeOsd internal API
src/video/freetype_osd.cc             UTF-8, face/glyph cache, placement and Y blend
tests/CMakeLists.txt                   CTest executables and test font configuration
tests/test_support/test_assert.h       Dependency-free assertion helpers
tests/test_support/i420_test_image.h   Owned guarded I420 fixture
tests/test_support/i420_test_image.cc  Fixture allocation and pixel helpers
tests/unit/mix_yuv_api_test.cc         API/context/empty-output tests
tests/unit/i420_geometry_test.cc       Pure geometry tests
tests/unit/i420_highlight_test.cc      Exact border/chroma tests
tests/unit/freetype_osd_test.cc        UTF-8/glyph/placement/blend tests
tests/unit/mix_yuv_test.cc             Composition and layer integration tests
tests/unit/mix_yuv_contract_test.cc    Overflow, padding, guard and failure tests
```

### Task 1: Build Scaffold, Internal API, and Context Lifecycle

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/FindLibYuv.cmake`
- Create: `cmake/YuvMixDependencies.cmake`
- Create: `src/video/mix_yuv.h`
- Create: `src/video/mix_yuv.cc`
- Create: `src/video/freetype_osd.h`
- Create: `src/video/freetype_osd.cc`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_support/test_assert.h`
- Create: `tests/unit/mix_yuv_api_test.cc`

- [ ] **Step 1: Add the dependency-free test assertions**

Create `tests/test_support/test_assert.h` with failure counting that does not abort after the first assertion:

```cpp
#ifndef YUVMIX_TEST_SUPPORT_TEST_ASSERT_H_
#define YUVMIX_TEST_SUPPORT_TEST_ASSERT_H_

#include <iostream>

namespace yuvmix_test {

inline int& FailureCount() {
    static int failures = 0;
    return failures;
}

inline void Expect(bool condition, const char* expression,
                   const char* file, int line) {
    if (!condition) {
        ++FailureCount();
        std::cerr << file << ':' << line << ": expectation failed: "
                  << expression << '\n';
    }
}

inline int Finish() {
    return FailureCount() == 0 ? 0 : 1;
}

}  // namespace yuvmix_test

#define EXPECT_TRUE(expression) \
    ::yuvmix_test::Expect((expression), #expression, __FILE__, __LINE__)
#define EXPECT_FALSE(expression) EXPECT_TRUE(!(expression))
#define EXPECT_EQ(actual, expected) EXPECT_TRUE((actual) == (expected))
#define EXPECT_NE(actual, expected) EXPECT_TRUE((actual) != (expected))

#endif  // YUVMIX_TEST_SUPPORT_TEST_ASSERT_H_
```

- [ ] **Step 2: Write the API/context failing test**

Create `tests/unit/mix_yuv_api_test.cc` with cases for invalid context creation, a valid configured font, and null call arguments:

```cpp
#include <memory>

#include "video/mix_yuv.h"
#include "test_support/test_assert.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

int main() {
    using namespace yuvmix;

    MixYuvConfig invalid;
    invalid.font_path = "/path/that/does/not/exist.ttf";
    invalid.font_face_index = 0;
    invalid.font_size = 24;
    invalid.osd_left = 12;
    invalid.osd_bottom = 12;
    std::unique_ptr<MixYuvContext> context;
    EXPECT_EQ(MixYuvContext::Create(invalid, &context),
              MixYuvStatus::kFontError);
    EXPECT_TRUE(context.get() == NULL);

    MixYuvConfig valid = invalid;
    valid.font_path = YUVMIX_TEST_FONT;
    EXPECT_EQ(MixYuvContext::Create(valid, &context), MixYuvStatus::kOk);
    EXPECT_TRUE(context.get() != NULL);
    EXPECT_EQ(MixYuv(NULL, NULL, 0, NULL), MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(MixYuv(context.get(), NULL, 1, NULL),
              MixYuvStatus::kInvalidArgument);

    return yuvmix_test::Finish();
}
```

- [ ] **Step 3: Add the exact API declarations**

Create `src/video/mix_yuv.h` from section 2 of the approved spec. Use `= delete` for copy operations, store implementation behind `std::unique_ptr<Impl>`, and declare `MixYuv` as a friend so no FreeType types leak through the header:

```cpp
#ifndef YUVMIX_VIDEO_MIX_YUV_H_
#define YUVMIX_VIDEO_MIX_YUV_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace yuvmix {

enum class FillMode { kContain, kCover };
enum class MixYuvStatus {
    kOk = 0,
    kInvalidArgument,
    kBufferTooSmall,
    kOutOfMemory,
    kFontError,
    kLibyuvError,
    kInternalError,
};

struct ConstPlane { const uint8_t* data; int stride; size_t size; };
struct MutablePlane { uint8_t* data; int stride; size_t size; };
struct I420ImageView {
    ConstPlane y; ConstPlane u; ConstPlane v;
    uint32_t width; uint32_t height;
};
struct MutableI420ImageView {
    MutablePlane y; MutablePlane u; MutablePlane v;
    uint32_t width; uint32_t height;
};
struct Rect { uint32_t x; uint32_t y; uint32_t w; uint32_t h; };
struct MixSource {
    I420ImageView image;
    Rect destination;
    std::string display_name;
    FillMode fill_mode;
    bool is_highlight;
};
struct I420Color { uint8_t y; uint8_t u; uint8_t v; };
struct MixOutput { MutableI420ImageView image; I420Color background_color; };
struct MixYuvConfig {
    std::string font_path;
    uint32_t font_face_index;
    uint32_t font_size;
    uint32_t osd_left;
    uint32_t osd_bottom;
};

class MixYuvContext {
public:
    static MixYuvStatus Create(const MixYuvConfig& config,
                               std::unique_ptr<MixYuvContext>* context);
    ~MixYuvContext();
    MixYuvContext(const MixYuvContext&) = delete;
    MixYuvContext& operator=(const MixYuvContext&) = delete;

private:
    struct Impl;
    explicit MixYuvContext(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend MixYuvStatus MixYuv(MixYuvContext*, const MixSource*, size_t,
                               MixOutput*);
};

MixYuvStatus MixYuv(MixYuvContext* context, const MixSource* sources,
                    size_t source_count, MixOutput* output);

}  // namespace yuvmix

#endif  // YUVMIX_VIDEO_MIX_YUV_H_
```

- [ ] **Step 4: Add CMake discovery and run the failing build**

Create `cmake/FindLibYuv.cmake`:

```cmake
find_path(LibYuv_INCLUDE_DIR
  NAMES libyuv/scale.h libyuv/planar_functions.h
  HINTS ${LibYuv_ROOT}
  PATH_SUFFIXES include)
find_library(LibYuv_LIBRARY
  NAMES yuv libyuv
  HINTS ${LibYuv_ROOT}
  PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibYuv
  REQUIRED_VARS LibYuv_INCLUDE_DIR LibYuv_LIBRARY)

if(LibYuv_FOUND AND NOT TARGET LibYuv::LibYuv)
  add_library(LibYuv::LibYuv UNKNOWN IMPORTED)
  set_target_properties(LibYuv::LibYuv PROPERTIES
    IMPORTED_LOCATION "${LibYuv_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LibYuv_INCLUDE_DIR}")
endif()

mark_as_advanced(LibYuv_INCLUDE_DIR LibYuv_LIBRARY)
```

Create the root `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(yuvmix LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
option(YUVMIX_LINK_DEPS_STATIC "Use platform prebuilt static dependencies" ON)
set(YUVMIX_DEPS_ROOT "" CACHE PATH "Platform dependency package root")
include(YuvMixDependencies)
yuvmix_configure_dependencies()

add_library(yuvmix_video STATIC
  src/video/mix_yuv.cc
  src/video/freetype_osd.cc)
target_compile_features(yuvmix_video PUBLIC cxx_std_11)
set_target_properties(yuvmix_video PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_VISIBILITY_PRESET hidden)
target_include_directories(yuvmix_video
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_link_libraries(yuvmix_video
  PRIVATE YuvMix::Freetype
  PUBLIC LibYuv::LibYuv)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
```

Create `tests/CMakeLists.txt`:

```cmake
set(YUVMIX_TEST_FONT "/System/Library/Fonts/SFNSMono.ttf"
  CACHE FILEPATH "Deterministic font used by MixYuv OSD tests")
if(NOT EXISTS "${YUVMIX_TEST_FONT}")
  message(FATAL_ERROR
    "YUVMIX_TEST_FONT must point to a readable TrueType/OpenType font")
endif()

function(add_yuvmix_test name)
  add_executable(${name} ${ARGN})
  target_compile_features(${name} PRIVATE cxx_std_11)
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_definitions(${name}
    PRIVATE YUVMIX_TEST_FONT="${YUVMIX_TEST_FONT}")
  target_link_libraries(${name} PRIVATE yuvmix_video)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_yuvmix_test(mix_yuv_api_test unit/mix_yuv_api_test.cc)
```

Run:

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
cmake --build build --target mix_yuv_api_test
```

Expected after libyuv is discoverable: link failure for undefined `MixYuvContext` and `MixYuv`, proving the API test is active.

`cmake/YuvMixDependencies.cmake` must map `Darwin/x86_64`, `Darwin/arm64`, and
`Linux/x86_64` to `macos-x86_64`, `macos-arm64`, and `linux-x86_64`. With static mode
enabled, create imported `LibYuv::LibYuv` and `YuvMix::Freetype` targets from the
selected package and reject missing headers or archives. With static mode disabled,
use `find_package(Freetype REQUIRED)` plus `FindLibYuv.cmake`, then provide
`YuvMix::Freetype` as an alias of `Freetype::Freetype`.

- [ ] **Step 5: Implement FreeType context creation and minimal argument rejection**

Define the initial `FreeTypeOsd` lifecycle API in `freetype_osd.h`:

```cpp
class FreeTypeOsd {
public:
    static MixYuvStatus Create(const MixYuvConfig& config,
                               std::unique_ptr<FreeTypeOsd>* osd);
    ~FreeTypeOsd();
    FreeTypeOsd(const FreeTypeOsd&) = delete;
    FreeTypeOsd& operator=(const FreeTypeOsd&) = delete;

private:
    struct Impl;
    explicit FreeTypeOsd(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
```

Its factory must validate font size `1..512`, call `FT_Init_FreeType`, `FT_New_Face`,
`FT_Select_Charmap(FT_ENCODING_UNICODE)`, and `FT_Set_Pixel_Sizes`. Store it in
`MixYuvContext::Impl`. Implement `MixYuv` only far enough to reject null context/output
and `sources == NULL && source_count != 0`:

```cpp
MixYuvStatus MixYuv(MixYuvContext* context, const MixSource* sources,
                    size_t source_count, MixOutput* output) {
    if (context == NULL || output == NULL ||
        (sources == NULL && source_count != 0)) {
        return MixYuvStatus::kInvalidArgument;
    }
    return MixYuvStatus::kInternalError;
}
```

Catch `std::bad_alloc` and `std::length_error` in `Create`, reset the output unique pointer on every failure, and release `FT_Face` before `FT_Library` in the destructor.

- [ ] **Step 6: Build and run the API test**

Run:

```bash
cmake --build build --target mix_yuv_api_test
ctest --test-dir build -R '^mix_yuv_api_test$' --output-on-failure
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 7: Record the checkpoint**

In a Git checkout, run:

```bash
git add CMakeLists.txt cmake/FindLibYuv.cmake cmake/YuvMixDependencies.cmake src/video/mix_yuv.h src/video/mix_yuv.cc src/video/freetype_osd.h src/video/freetype_osd.cc tests/CMakeLists.txt tests/test_support/test_assert.h tests/unit/mix_yuv_api_test.cc
git commit -m "build: scaffold mixyuv video compositor"
```

### Task 2: Integer Contain/Cover Geometry

**Files:**
- Create: `src/video/i420_geometry.h`
- Create: `src/video/i420_geometry.cc`
- Create: `tests/unit/i420_geometry_test.cc`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write table-driven failing geometry tests**

Define cases that assert every field of `GeometryPlan`:

```cpp
struct GeometryCase {
    uint32_t sw, sh, dw, dh;
    yuvmix::FillMode mode;
    yuvmix::GeometryPlan expected;
};

const GeometryCase cases[] = {
    {640, 360, 1280, 720, yuvmix::FillMode::kContain,
     {0, 0, 640, 360, 320, 180, 640, 360, true}},
    {1920, 1080, 640, 480, yuvmix::FillMode::kContain,
     {0, 0, 1920, 1080, 0, 60, 640, 360, false}},
    {1920, 1080, 640, 480, yuvmix::FillMode::kCover,
     {240, 0, 1440, 1080, 0, 0, 640, 480, false}},
    {640, 1080, 1280, 720, yuvmix::FillMode::kCover,
     {0, 360, 640, 360, 0, 0, 1280, 720, false}},
};
```

Also assert rejection of odd dimensions, zero dimensions, unknown FillMode values, multiplication overflow candidates, and derived dimensions below 2.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake --build build --target i420_geometry_test
```

Expected: compilation failure because `i420_geometry.h` and `BuildGeometryPlan` do not exist.

- [ ] **Step 3: Implement the geometry API and minimal integer algorithm**

Use this exact plan shape:

```cpp
struct GeometryPlan {
    uint32_t crop_x, crop_y, crop_w, crop_h;
    uint32_t dest_x, dest_y, dest_w, dest_h;
    bool use_copy;
};

MixYuvStatus BuildGeometryPlan(uint32_t source_width,
                               uint32_t source_height,
                               uint32_t target_width,
                               uint32_t target_height,
                               FillMode mode,
                               GeometryPlan* plan);
```

Implement `FloorEven(value) = value & ~uint64_t(1)`. For Contain, compare
`target_width * source_height` with `target_height * source_width` in checked
`uint64_t`; for Cover, use the same cross products to choose the cropped axis.
Set `use_copy` only when crop and destination dimensions equal the source dimensions.
Return `kInvalidArgument` when any input or derived I420 dimension is odd, zero, or
below 2.

Register the implementation and test:

```cmake
target_sources(yuvmix_video PRIVATE src/video/i420_geometry.cc)
add_yuvmix_test(i420_geometry_test unit/i420_geometry_test.cc)
```

- [ ] **Step 4: Run the focused geometry test**

Run:

```bash
cmake --build build --target i420_geometry_test
ctest --test-dir build -R '^i420_geometry_test$' --output-on-failure
```

Expected: all geometry cases pass.

- [ ] **Step 5: Record the checkpoint**

```bash
git add CMakeLists.txt src/video/i420_geometry.h src/video/i420_geometry.cc tests/CMakeLists.txt tests/unit/i420_geometry_test.cc
git commit -m "feat: add integer i420 geometry planning"
```

### Task 3: Plane Validation and Background Fill

**Files:**
- Create: `tests/test_support/i420_test_image.h`
- Create: `tests/test_support/i420_test_image.cc`
- Modify: `src/video/mix_yuv.cc`
- Modify: `tests/unit/mix_yuv_api_test.cc`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add a guarded caller-owned I420 fixture**

Implement `OwnedI420` with configurable stride padding, 16-byte guards before and after every plane, and view constructors:

```cpp
class OwnedI420 {
public:
    OwnedI420(uint32_t width, uint32_t height, size_t padding);
    yuvmix::I420ImageView ConstView() const;
    yuvmix::MutableI420ImageView MutableView();
    void Fill(uint8_t y, uint8_t u, uint8_t v);
    bool ActivePixelsEqual(uint8_t y, uint8_t u, uint8_t v) const;
    bool GuardsIntact() const;
    bool PaddingEquals(uint8_t value) const;
    uint8_t Y(uint32_t x, uint32_t y) const;
    uint8_t U(uint32_t x, uint32_t y) const;
    uint8_t V(uint32_t x, uint32_t y) const;
private:
    uint32_t width_;
    uint32_t height_;
    size_t padding_;
    std::vector<uint8_t> y_storage_;
    std::vector<uint8_t> u_storage_;
    std::vector<uint8_t> v_storage_;
};
```

Initialize guards to `0xA5`, padding to `0xCC`, and active pixels separately.

- [ ] **Step 2: Write failing empty-output and validation tests**

Create a valid context, build an 8x6 output with stride padding, and assert:

```cpp
yuvmix::MixOutput output;
output.image = image.MutableView();
output.background_color = {16, 128, 128};
EXPECT_EQ(yuvmix::MixYuv(context.get(), NULL, 0, &output),
          yuvmix::MixYuvStatus::kOk);
EXPECT_TRUE(image.ActivePixelsEqual(16, 128, 128));
EXPECT_TRUE(image.PaddingEquals(0xCC));
EXPECT_TRUE(image.GuardsIntact());
```

Add cases for null planes, odd dimensions, stride smaller than row width, and each
plane one byte shorter than `stride * (rows - 1) + row_bytes`. Pre-fill output with
`0x37` and assert it remains byte-for-byte unchanged on every rejected call.

- [ ] **Step 3: Run the test to verify the current stub fails**

Run:

```bash
cmake --build build --target mix_yuv_api_test
ctest --test-dir build -R '^mix_yuv_api_test$' --output-on-failure
```

Expected: failure because valid empty composition returns `kInternalError`.

- [ ] **Step 4: Implement validation and active-row background fill**

Add checked helpers in the anonymous namespace of `mix_yuv.cc`:

```cpp
bool RequiredPlaneSize(size_t stride, size_t rows, size_t row_bytes,
                       size_t* required);
MixYuvStatus ValidateInputImage(const I420ImageView& image);
MixYuvStatus ValidateOutputImage(const MutableI420ImageView& image);
void FillPlane(uint8_t* data, int stride, uint32_t rows,
               uint32_t row_bytes, uint8_t value);
void FillOutput(MixOutput* output);
```

Reject dimensions below 2 or odd, non-positive stride, short row width, null data,
and checked multiply/add overflow. Fill exactly `row_bytes` for every active row;
never `memset` the complete stride.

Compile the fixture into the API test:

```cmake
target_sources(mix_yuv_api_test PRIVATE test_support/i420_test_image.cc)
```

- [ ] **Step 5: Run API and guard tests**

Run:

```bash
cmake --build build --target mix_yuv_api_test
ctest --test-dir build -R '^mix_yuv_api_test$' --output-on-failure
```

Expected: all API, empty-output, padding, and short-plane cases pass.

- [ ] **Step 6: Record the checkpoint**

```bash
git add src/video/mix_yuv.cc tests/test_support/i420_test_image.h tests/test_support/i420_test_image.cc tests/unit/mix_yuv_api_test.cc tests/CMakeLists.txt
git commit -m "feat: validate and initialize i420 output buffers"
```

### Task 4: libyuv Source Composition

**Files:**
- Modify: `src/video/mix_yuv.cc`
- Create: `tests/unit/mix_yuv_test.cc`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing constant-color composition tests**

Use explicitly owned constant I420 sources so bilinear scaling has exact expected values
and every view remains valid for the complete call. Cover these cases:

```cpp
OwnedI420 left_image(8, 8, 2);
OwnedI420 right_image(4, 4, 2);
left_image.Fill(40, 90, 140);
right_image.Fill(80, 100, 150);
MixSource left = {left_image.ConstView(), {0, 0, 8, 8}, "",
                  FillMode::kContain, false};
MixSource right = {right_image.ConstView(), {8, 0, 8, 8}, "",
                   FillMode::kCover, false};
const MixSource sources[] = {left, right};
EXPECT_EQ(MixYuv(context.get(), sources, 2, &output), MixYuvStatus::kOk);
EXPECT_EQ(canvas.Y(2, 2), 40);
EXPECT_EQ(canvas.Y(10, 2), 80);
EXPECT_EQ(canvas.Y(2, 10), output.background_color.y);
```

Add tests for no-upscale centering, Contain letterbox, Cover crop, non-tight input
stride, destination offsets, and a second invalid source proving the first source is
not written before the whole array passes preflight.

- [ ] **Step 2: Run the focused test to verify it fails**

Run:

```bash
cmake --build build --target mix_yuv_test
ctest --test-dir build -R '^mix_yuv_test$' --output-on-failure
```

Expected: pixel assertions fail because only the background is rendered.

- [ ] **Step 3: Add immutable per-source preflight plans**

Inside `MixYuvContext::Impl`, retain a `std::vector<SourcePlan>` with this exact data:

```cpp
struct SourcePlan {
    const MixSource* source;
    GeometryPlan geometry;
};
```

Before background fill, reserve `source_count`, validate every input image and Rect,
check `x <= output.width - w` and `y <= output.height - h`, validate FillMode, and call
`BuildGeometryPlan`. Do not test Rect intersection or plane aliasing.

- [ ] **Step 4: Implement direct libyuv copy/scale**

For each plan, compute even crop pointers in the source and even destination pointers
in the output. Use `libyuv::I420Copy` when `geometry.use_copy` is true and otherwise use:

```cpp
const int result = libyuv::I420Scale(
    source_y, source_y_stride,
    source_u, source_u_stride,
    source_v, source_v_stride,
    static_cast<int>(geometry.crop_w),
    static_cast<int>(geometry.crop_h),
    destination_y, output_y_stride,
    destination_u, output_u_stride,
    destination_v, output_v_stride,
    static_cast<int>(geometry.dest_w),
    static_cast<int>(geometry.dest_h),
    libyuv::kFilterBilinear);
```

Return `kLibyuvError` immediately on nonzero result. Do not allocate a full source or
destination temporary frame.

Register the composition test with its owned-image fixture:

```cmake
add_yuvmix_test(mix_yuv_test
  unit/mix_yuv_test.cc
  test_support/i420_test_image.cc)
```

- [ ] **Step 5: Run geometry, API, and composition tests**

Run:

```bash
cmake --build build
ctest --test-dir build -R '^(i420_geometry|mix_yuv_api|mix_yuv)_test$' --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Record the checkpoint**

```bash
git add src/video/mix_yuv.cc tests/unit/mix_yuv_test.cc tests/CMakeLists.txt
git commit -m "feat: composite i420 sources with libyuv"
```

### Task 5: Unioned I420 Highlight Rendering

**Files:**
- Create: `src/video/i420_highlight.h`
- Create: `src/video/i420_highlight.cc`
- Create: `tests/unit/i420_highlight_test.cc`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/video/mix_yuv.cc`

- [ ] **Step 1: Write exact failing border tests**

Test the required height mapping and geometry:

```cpp
EXPECT_EQ(BorderWidth(162), 2u);
EXPECT_EQ(BorderWidth(242), 3u);
EXPECT_EQ(BorderWidth(558), 7u);
EXPECT_EQ(BorderWidth(720), 9u);
EXPECT_EQ(BorderWidth(838), 10u);
EXPECT_EQ(BorderWidth(1080), 13u);
```

For a small synthetic output, verify Y pixels in outer-minus-inner, canvas-edge
clipping, odd inside/outside allocation, and U/V expected values for `n=0..4` using:

```cpp
uint8_t BlendChroma(uint8_t old_value, uint8_t border_value, uint32_t n) {
    return static_cast<uint8_t>(
        (n * border_value + (4 - n) * old_value + 2) / 4);
}
```

Add two nearby highlighted Rects whose borders touch the same 2x2 block and assert the
chroma is blended once from the union coverage.

- [ ] **Step 2: Run the highlight test to verify it fails**

Run:

```bash
cmake --build build --target i420_highlight_test
```

Expected: compilation failure because `i420_highlight.h` does not exist.

- [ ] **Step 3: Implement the focused highlight API**

Use an output-sized byte mask owned by the caller/context:

```cpp
uint32_t BorderWidth(uint32_t cell_height);

MixYuvStatus EnsureHighlightMask(uint32_t width,
                                 uint32_t height,
                                 std::vector<uint8_t>* mask);

MixYuvStatus DrawHighlights(const Rect* rects,
                            size_t rect_count,
                            MutableI420ImageView* output,
                            std::vector<uint8_t>* mask);
```

Call `EnsureHighlightMask` during preflight, before output fill. It performs checked
`width * height` calculation and grows the vector when required. `DrawHighlights`
must reject insufficient capacity without resizing; during a valid draw it clears exactly
`output.width * output.height`, marks every clipped outer-minus-inner border with 1,
then scans Y pixels and 2x2 chroma blocks. Use signed 64-bit outer coordinates so
`x - outside_width` cannot underflow.

Register the implementation and focused test:

```cmake
target_sources(yuvmix_video PRIVATE src/video/i420_highlight.cc)
add_yuvmix_test(i420_highlight_test
  unit/i420_highlight_test.cc
  test_support/i420_test_image.cc)
```

- [ ] **Step 4: Integrate highlights after every media source**

Extend `SourcePlan` with `Rect destination` and `bool is_highlight`. Before output fill,
collect highlighted Rects and ensure mask capacity. After the complete libyuv source
loop succeeds, call `DrawHighlights`. With no highlighted sources, skip mask clearing
and scanning.

- [ ] **Step 5: Run focused and integrated tests**

Run:

```bash
cmake --build build
ctest --test-dir build -R '^(i420_highlight|mix_yuv)_test$' --output-on-failure
```

Expected: exact border, union, chroma, and layer tests pass.

- [ ] **Step 6: Record the checkpoint**

```bash
git add CMakeLists.txt src/video/i420_highlight.h src/video/i420_highlight.cc src/video/mix_yuv.cc tests/CMakeLists.txt tests/unit/i420_highlight_test.cc tests/unit/mix_yuv_test.cc
git commit -m "feat: render unioned active speaker highlights"
```

### Task 6: Strict UTF-8 and FreeType OSD

**Files:**
- Modify: `src/video/freetype_osd.h`
- Modify: `src/video/freetype_osd.cc`
- Create: `tests/unit/freetype_osd_test.cc`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing UTF-8 and OSD tests**

Test valid ASCII, a three-byte code point, a four-byte code point, empty input, truncated
sequences, invalid continuations, overlong forms, surrogates, and values above U+10FFFF.
Then create a text run with the configured test font and assert:

```cpp
TextRun run;
EXPECT_EQ(osd->PrepareText("Mix", &run), MixYuvStatus::kOk);
EXPECT_EQ(run.glyphs.size(), 3u);
EXPECT_TRUE(run.advance_x > 0);
EXPECT_EQ(osd->PrepareText("\xF0\x28\x8C\x28", &run),
          MixYuvStatus::kInvalidArgument);
```

Draw onto a padded I420 image and assert at least one Y pixel changes toward 235, U/V
and padding stay unchanged, clipping stays inside the Rect, and a glyph overlapping a
143-valued border produces the documented alpha blend rather than leaving 143.

- [ ] **Step 2: Run the OSD test to verify it fails**

Run:

```bash
cmake --build build --target freetype_osd_test
ctest --test-dir build -R '^freetype_osd_test$' --output-on-failure
```

Expected: compilation or assertion failure because text preparation and drawing are absent.

- [ ] **Step 3: Implement strict UTF-8 decoding**

Expose an internal helper for direct unit testing:

```cpp
MixYuvStatus DecodeUtf8(const std::string& text,
                        std::vector<uint32_t>* code_points);
```

Decode one to four bytes, reject overlong encodings, continuation bytes in leading
position, surrogate range `0xD800..0xDFFF`, and code points above `0x10FFFF`. Decode
into a temporary vector and swap only on success so a rejected string leaves the
caller result unchanged.

- [ ] **Step 4: Implement glyph caching and immutable text runs**

Use fixed-font cached glyphs and pointer-stable ownership:

```cpp
struct GlyphBitmap {
    int width;
    int rows;
    int bitmap_left;
    int bitmap_top;
    int advance_x;
    std::vector<uint8_t> coverage;
};

struct TextRun {
    std::vector<const GlyphBitmap*> glyphs;
    int64_t advance_x;
};
```

Extend `FreeTypeOsd` with the exact operations consumed by `MixYuv` and tests:

```cpp
MixYuvStatus PrepareText(const std::string& text, TextRun* run);
void DrawText(const TextRun& run, const Rect& clip,
              MutableI420ImageView* output) const;
size_t glyph_count() const;
```

Store `std::unique_ptr<GlyphBitmap>` by glyph index so pointers in a prepared TextRun
remain valid when the map rehashes. Load with `FT_Load_Glyph`, render with
`FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL)`, convert `FT_PIXEL_MODE_GRAY` to 0..255,
and convert `FT_PIXEL_MODE_MONO` bits to 0 or 255. Missing characters use glyph index 0.
Do not apply kerning.

- [ ] **Step 5: Implement baseline placement and Y-only blending**

Compute:

```text
pen_x = rect.x + osd_left
baseline_y = rect.y + rect.h - osd_bottom - abs(descender_px)
glyph_x = pen_x + bitmap_left
glyph_y = baseline_y - bitmap_top
```

Use signed 64-bit coordinates, clip every glyph to the source Rect, and blend each
coverage byte with:

```cpp
const uint32_t mixed = coverage * 235u + (255u - coverage) * old_y;
new_y = static_cast<uint8_t>((mixed + 127u) / 255u);
```

Never write U/V or output padding.

Register the focused test; `freetype_osd.cc` is already part of `yuvmix_video`:

```cmake
add_yuvmix_test(freetype_osd_test
  unit/freetype_osd_test.cc
  test_support/i420_test_image.cc)
```

- [ ] **Step 6: Run OSD and sanitizer-friendly unit tests**

Run:

```bash
cmake --build build --target freetype_osd_test
ctest --test-dir build -R '^freetype_osd_test$' --output-on-failure
```

Expected: UTF-8, missing-glyph, placement, clipping, blend, and plane-isolation tests pass.

- [ ] **Step 7: Record the checkpoint**

```bash
git add src/video/freetype_osd.h src/video/freetype_osd.cc tests/unit/freetype_osd_test.cc tests/CMakeLists.txt
git commit -m "feat: add freetype nickname osd renderer"
```

### Task 7: Complete Preflight and Layered MixYuv Integration

**Files:**
- Modify: `src/video/mix_yuv.cc`
- Modify: `tests/unit/mix_yuv_test.cc`
- Create: `tests/unit/mix_yuv_contract_test.cc`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing full-pipeline tests**

Add cases that prove these observable states in order:

1. Contain padding begins as configured background.
2. Media overwrites only its planned destination pixels.
3. Half-outside highlight overwrites media and can enter the layout gap.
4. OSD coverage overwrites the highlight Y value but not U/V.

For failure atomicity, create two sources where the first is valid and the second has
an invalid UTF-8 DisplayName. Snapshot the complete output storage before the call and
assert `kInvalidArgument` plus byte-for-byte equality afterward.

- [ ] **Step 2: Run the integration test to verify it fails**

Run:

```bash
cmake --build build --target mix_yuv_test mix_yuv_contract_test
ctest --test-dir build -R '^mix_yuv(_contract)?_test$' --output-on-failure
```

Expected: OSD and preflight-atomicity cases fail before integration.

- [ ] **Step 3: Extend SourcePlan with prepared text**

Use this plan, prepared completely before `output_started` becomes true:

```cpp
struct SourcePlan {
    const MixSource* source;
    GeometryPlan geometry;
    Rect destination;
    bool is_highlight;
    TextRun text;
};
```

For every source, validate the image and Rect, build geometry, strictly decode and
prepare text, and append the completed plan. Ensure render-plan, glyph, and highlight
mask capacity before filling the background.

- [ ] **Step 4: Implement the final exception boundary and render order**

Use a local `bool output_started = false`. The final call structure must be:

```cpp
try {
    MixYuvStatus status = Preflight(context, sources, source_count, output);
    if (status != MixYuvStatus::kOk) return status;
    output_started = true;
    FillOutput(output);
    status = DrawAllSources(context, output);
    if (status != MixYuvStatus::kOk) return status;
    status = DrawAllHighlights(context, output);
    if (status != MixYuvStatus::kOk) return status;
    DrawAllText(context, output);
    return MixYuvStatus::kOk;
} catch (const std::bad_alloc&) {
    return output_started ? MixYuvStatus::kInternalError
                          : MixYuvStatus::kOutOfMemory;
} catch (const std::length_error&) {
    return output_started ? MixYuvStatus::kInternalError
                          : MixYuvStatus::kOutOfMemory;
} catch (...) {
    return MixYuvStatus::kInternalError;
}
```

Rendering methods must not allocate. Clear per-call vectors with `clear()` rather than
releasing capacity. OSD is drawn for every non-empty TextRun in source array order.

Register the contract test; `mix_yuv_test` was registered in Task 4:

```cmake
add_yuvmix_test(mix_yuv_contract_test
  unit/mix_yuv_contract_test.cc
  test_support/i420_test_image.cc)
```

- [ ] **Step 5: Run all unit and integration tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 6: Record the checkpoint**

```bash
git add src/video/mix_yuv.cc tests/unit/mix_yuv_test.cc tests/unit/mix_yuv_contract_test.cc tests/CMakeLists.txt
git commit -m "feat: complete layered mixyuv pipeline"
```

### Task 8: Contract Matrix, Sanitizers, and Final Verification

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/unit/mix_yuv_contract_test.cc`
- Modify: `docs/superpowers/plans/2026-07-31-mixyuv-implementation.md`

- [ ] **Step 1: Complete the contract matrix tests**

Add explicit cases for:

- source/output widths and heights `0`, `1`, odd, `2`, 720p, and 1080p;
- negative, zero, short, padded, and overflow-adjacent strides/capacities;
- Rect zero, odd, edge-aligned, out-of-bounds, and unsigned maximum fields;
- empty source array and multiple nonintersecting sources;
- output size growth, shrink, and regrowth on one context;
- input and output guard bytes after success and all checked failures;
- repeated stable calls with the same source count and glyph set.

Implement allocation-reuse instrumentation behind a test-only compile definition:

```cpp
#if defined(YUVMIX_TESTING)
size_t MixYuvContextPreparedCapacity(const MixYuvContext& context);
size_t MixYuvContextGlyphCount(const MixYuvContext& context);
#endif
```

Declare both functions as friends of `MixYuvContext`. Under `if(BUILD_TESTING)`, add
`target_compile_definitions(yuvmix_video PUBLIC YUVMIX_TESTING)` so the library and
test translation units see the same declarations. Do not define the accessors in
non-test builds.

Assert both values remain constant across 100 stable calls after one warm-up call.

- [ ] **Step 2: Add sanitizer CMake options**

Add mutually compatible options:

```cmake
option(YUVMIX_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(YUVMIX_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(YUVMIX_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()
if(YUVMIX_ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()
```

Apply only for Clang/GCC and fail configuration with a clear message for unsupported
compilers rather than silently ignoring the request.

- [ ] **Step 3: Run the normal Debug verification**

Run:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
```

Expected: all tests pass with zero failures.

- [ ] **Step 4: Run ASan and UBSan verification**

Run:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DYUVMIX_ENABLE_ASAN=ON -DYUVMIX_ENABLE_UBSAN=ON -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

Expected: all tests pass; sanitizer output contains no AddressSanitizer or
UndefinedBehaviorSanitizer findings.

- [ ] **Step 5: Run Release verification**

Run:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

Expected: all tests pass with zero failures.

- [ ] **Step 6: Check the implementation against the approved specification**

Run searches that must show implementation and tests for every critical contract:

```bash
rg -n "kFilterBilinear|I420Copy|border_width|chroma_out|FT_RENDER_MODE_NORMAL|output_started|kBufferTooSmall" src tests
rg -n "TO[D]O|TB[D]|FIX[M]E" src tests CMakeLists.txt cmake
```

Expected: the first search finds code and tests for all named contracts; the second
search produces no matches.

- [ ] **Step 7: Record the final checkpoint**

```bash
git add CMakeLists.txt cmake src tests docs/superpowers/plans/2026-07-31-mixyuv-implementation.md
git commit -m "test: verify mixyuv contracts and sanitizers"
```

## Completion Criteria

The implementation goal is complete only when:

- all eight goal groups G1 through G8 are implemented;
- Debug, Release, ASan, and UBSan commands have fresh successful output;
- every preflight failure test proves the output is unchanged;
- guard and padding checks prove no writes outside active output pixels;
- source media, highlight, and OSD pixel tests prove the required layer order;
- no source or test file contains unfinished-work markers;
- the final code remains C++11 and the internal header is not installed as public ABI;
- libyuv and the deterministic test font prerequisites are documented in the build output.
