# macOS and Linux Shared Library Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce `yuvmix_video` as a shared library on macOS arm64 and Linux x86_64 while statically linking libyuv and FreeType into it.

**Architecture:** Keep the existing single `yuvmix_video` target and its static imported dependency targets, but make the primary target explicitly `SHARED` and expose its current C++ symbols. Use CMake script tests to verify the artifact type and supported platform mapping, then align the GitHub Actions matrix and dependency documentation with the two supported release targets.

**Tech Stack:** CMake 3.20+, CTest, C++11, GitHub Actions, libyuv, FreeType

---

## File Structure

- Create `tests/cmake/shared_library_artifact_test.cmake`: validates that the built target path uses the platform shared-library suffix.
- Create `tests/cmake/platform_mapping_probe.cmake`: invokes dependency platform mapping in a child CMake process so expected fatal failures can be tested.
- Modify `tests/CMakeLists.txt`: registers the shared-library artifact test.
- Modify `tests/cmake/dependency_mapping_test.cmake`: keeps macOS arm64 and Linux x86_64 success cases and adds macOS x86_64 rejection.
- Modify `CMakeLists.txt`: builds `yuvmix_video` as `SHARED` and restores default symbol visibility.
- Modify `cmake/YuvMixDependencies.cmake`: rejects macOS architectures other than arm64.
- Modify `.github/workflows/mixyuv.yml`: tests only macOS arm64 and Linux x86_64 and names the primary build as shared.
- Modify `third_party/prebuilt/README.md`: documents only the supported dependency packages and shared-library release form.

### Task 1: Verify and Build a Shared YuvMix Artifact

**Files:**
- Create: `tests/cmake/shared_library_artifact_test.cmake`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing artifact test**

Create `tests/cmake/shared_library_artifact_test.cmake`:

```cmake
if(NOT DEFINED LIBRARY_PATH OR NOT DEFINED EXPECTED_SUFFIX)
    message(FATAL_ERROR "LIBRARY_PATH and EXPECTED_SUFFIX are required")
endif()

string(LENGTH "${LIBRARY_PATH}" path_length)
string(LENGTH "${EXPECTED_SUFFIX}" suffix_length)
if(path_length LESS suffix_length)
    message(FATAL_ERROR
        "Expected a ${EXPECTED_SUFFIX} library, got ${LIBRARY_PATH}")
endif()

math(EXPR suffix_offset "${path_length} - ${suffix_length}")
string(SUBSTRING "${LIBRARY_PATH}" ${suffix_offset} ${suffix_length}
       actual_suffix)
if(NOT actual_suffix STREQUAL EXPECTED_SUFFIX)
    message(FATAL_ERROR
        "Expected a ${EXPECTED_SUFFIX} library, got ${LIBRARY_PATH}")
endif()
```

Register it in `tests/CMakeLists.txt` after `dependency_mapping_test`:

```cmake
add_test(NAME shared_library_artifact_test
    COMMAND "${CMAKE_COMMAND}"
        -DLIBRARY_PATH=$<TARGET_FILE:yuvmix_video>
        -DEXPECTED_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shared_library_artifact_test.cmake")
```

- [ ] **Step 2: Run the test and verify RED**

Configure using available development dependencies, build the current target,
and run the focused test:

```bash
cmake -S . -B build-shared-test \
  -DBUILD_TESTING=ON \
  -DYUVMIX_LINK_DEPS_STATIC=OFF \
  -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
cmake --build build-shared-test --target yuvmix_video --parallel
ctest --test-dir build-shared-test -R shared_library_artifact_test \
  --output-on-failure
```

On Linux, replace `YUVMIX_TEST_FONT` with
`/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf`. Expected: FAIL because
the current artifact ends in `.a`, not `.dylib` or `.so`.

- [ ] **Step 3: Make the target shared with exported C++ symbols**

Change the target declaration in `CMakeLists.txt`:

```cmake
add_library(yuvmix_video SHARED
    src/video/i420_geometry.cc
    src/video/i420_highlight.cc
    src/video/mix_yuv.cc
    src/video/freetype_osd.cc)
```

Keep only the extension property:

```cmake
set_target_properties(yuvmix_video PROPERTIES
    CXX_EXTENSIONS OFF)
```

Removing `CXX_VISIBILITY_PRESET hidden` gives the existing C++ API normal
platform visibility.

- [ ] **Step 4: Build and verify GREEN**

```bash
cmake --build build-shared-test --parallel
ctest --test-dir build-shared-test -R shared_library_artifact_test \
  --output-on-failure
ctest --test-dir build-shared-test --output-on-failure
```

Expected: the focused test and all existing tests PASS. On macOS the target is
`libyuvmix_video.dylib`; on Linux it is `libyuvmix_video.so`.

- [ ] **Step 5: Commit the shared target and test**

```bash
git add CMakeLists.txt tests/CMakeLists.txt \
  tests/cmake/shared_library_artifact_test.cmake
git commit -m "build: produce yuvmix shared library"
```

### Task 2: Restrict Static Dependency Packages to Supported Architectures

**Files:**
- Create: `tests/cmake/platform_mapping_probe.cmake`
- Modify: `tests/cmake/dependency_mapping_test.cmake`
- Modify: `cmake/YuvMixDependencies.cmake`

- [ ] **Step 1: Write the child-process platform probe**

Create `tests/cmake/platform_mapping_probe.cmake`:

```cmake
include("${YUVMIX_SOURCE_DIR}/cmake/YuvMixDependencies.cmake")

set(CMAKE_SYSTEM_NAME "${PROBE_SYSTEM}")
set(CMAKE_SYSTEM_PROCESSOR "${PROBE_PROCESSOR}")
set(CMAKE_OSX_ARCHITECTURES "${PROBE_OSX_ARCHITECTURES}")
yuvmix_platform_dependency_name(platform_name)
```

- [ ] **Step 2: Write a failing rejection test**

Replace the Darwin success cases in
`tests/cmake/dependency_mapping_test.cmake` with:

```cmake
expect_platform("Darwin" "arm64" "" "macos-arm64")
expect_platform("Darwin" "x86_64" "arm64" "macos-arm64")
expect_platform("Linux" "x86_64" "" "linux-x86_64")
```

Then add:

```cmake
function(expect_platform_rejected system processor osx_architectures)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DYUVMIX_SOURCE_DIR=${YUVMIX_SOURCE_DIR}
            -DPROBE_SYSTEM=${system}
            -DPROBE_PROCESSOR=${processor}
            -DPROBE_OSX_ARCHITECTURES=${osx_architectures}
            -P "${CMAKE_CURRENT_LIST_DIR}/platform_mapping_probe.cmake"
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_QUIET)
    if(result EQUAL 0)
        message(FATAL_ERROR
            "Expected ${system}/${processor}/${osx_architectures} to be rejected")
    endif()
endfunction()

expect_platform_rejected("Darwin" "x86_64" "")
expect_platform_rejected("Darwin" "arm64" "x86_64")
```

- [ ] **Step 3: Run the mapping test and verify RED**

```bash
ctest --test-dir build-shared-test -R dependency_mapping_test \
  --output-on-failure
```

Expected: FAIL because the current mapper still accepts macOS x86_64.

- [ ] **Step 4: Reject unsupported macOS architectures**

Replace the Darwin processor branch in
`cmake/YuvMixDependencies.cmake` with:

```cmake
string(TOLOWER "${target_processor}" processor)
if(processor STREQUAL "arm64" OR processor STREQUAL "aarch64")
    set(platform_name "macos-arm64")
else()
    message(FATAL_ERROR
        "Unsupported macOS architecture: ${target_processor}")
endif()
```

Keep the existing Linux x86_64 branch unchanged.

- [ ] **Step 5: Run the focused and full tests and verify GREEN**

```bash
ctest --test-dir build-shared-test -R dependency_mapping_test \
  --output-on-failure
ctest --test-dir build-shared-test --output-on-failure
```

Expected: both macOS arm64 aliases map correctly, both macOS x86_64 probes are
rejected, and all tests PASS.

- [ ] **Step 6: Commit the supported platform mapping**

```bash
git add cmake/YuvMixDependencies.cmake \
  tests/cmake/dependency_mapping_test.cmake \
  tests/cmake/platform_mapping_probe.cmake
git commit -m "build: restrict release architectures"
```

### Task 3: Align CI and Dependency Documentation

**Files:**
- Modify: `.github/workflows/mixyuv.yml`
- Modify: `third_party/prebuilt/README.md`

- [ ] **Step 1: Update the CI matrix and shared-build labels**

In `.github/workflows/mixyuv.yml`:

- Rename job `static-platform` to `shared-platform`.
- Change the job display suffix from `static` to `shared`.
- Delete the `macOS x86_64` / `macos-15-intel` matrix entry.
- Keep the macOS arm64 and Linux x86_64 entries.
- Rename `Configure static MixYuv` to `Configure shared MixYuv`.
- Keep `YUVMIX_LINK_DEPS_STATIC=ON`, `BUILD_SHARED_LIBS=OFF` for FreeType, and
  `CMAKE_POSITION_INDEPENDENT_CODE=ON` for both dependency builds.

- [ ] **Step 2: Update dependency package documentation**

In `third_party/prebuilt/README.md`:

- Remove `macos-x86_64/` from the package layout.
- State that release output is `libyuvmix_video.dylib` on macOS arm64 and
  `libyuvmix_video.so` on Linux x86_64.
- State that libyuv and FreeType remain static archives linked into the final
  shared library.
- Replace the multi-architecture macOS/Universal Binary paragraph with a
  statement that macOS release builds require arm64.
- Update the CI target list to macOS arm64 and Linux x86_64.

- [ ] **Step 3: Validate configuration text and formatting**

```bash
rg -n "macos-15-intel|macos-x86_64|macOS x86_64" \
  .github/workflows/mixyuv.yml third_party/prebuilt/README.md
rg -n "YUVMIX_LINK_DEPS_STATIC=ON|BUILD_SHARED_LIBS=OFF|CMAKE_POSITION_INDEPENDENT_CODE=ON" \
  .github/workflows/mixyuv.yml
git diff --check
```

Expected: the first command returns no matches; the second confirms static/PIC
dependency settings remain present; `git diff --check` returns no errors.

- [ ] **Step 4: Commit CI and documentation changes**

```bash
git add .github/workflows/mixyuv.yml third_party/prebuilt/README.md
git commit -m "ci: test supported shared library targets"
```

### Task 4: Verify Static Dependency Inclusion and Final State

**Files:**
- Verify only; no planned file changes.

- [ ] **Step 1: Build pinned PIC static dependencies on macOS arm64**

```bash
YUVMIX_VERIFY_ROOT="$(mktemp -d /private/tmp/yuvmix-shared.XXXXXX)"
git clone https://github.com/lemenkov/libyuv.git \
  "$YUVMIX_VERIFY_ROOT/libyuv"
git -C "$YUVMIX_VERIFY_ROOT/libyuv" checkout \
  b56492e2dfc064f65ef27fed9c45d9bbfc2e2ad2
git clone --depth 1 --branch VER-2-13-3 \
  https://github.com/freetype/freetype.git \
  "$YUVMIX_VERIFY_ROOT/freetype"
cmake -S "$YUVMIX_VERIFY_ROOT/libyuv" \
  -B "$YUVMIX_VERIFY_ROOT/libyuv-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build "$YUVMIX_VERIFY_ROOT/libyuv-build" --parallel
mkdir -p "$YUVMIX_VERIFY_ROOT/deps/include" \
  "$YUVMIX_VERIFY_ROOT/deps/lib"
cp -R "$YUVMIX_VERIFY_ROOT/libyuv/include/"* \
  "$YUVMIX_VERIFY_ROOT/deps/include/"
cp "$YUVMIX_VERIFY_ROOT/libyuv-build/libyuv.a" \
  "$YUVMIX_VERIFY_ROOT/deps/lib/"
cmake -S "$YUVMIX_VERIFY_ROOT/freetype" \
  -B "$YUVMIX_VERIFY_ROOT/freetype-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$YUVMIX_VERIFY_ROOT/deps" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_SHARED_LIBS=OFF \
  -DFT_DISABLE_ZLIB=ON \
  -DFT_DISABLE_BZIP2=ON \
  -DFT_DISABLE_PNG=ON \
  -DFT_DISABLE_HARFBUZZ=ON \
  -DFT_DISABLE_BROTLI=ON
cmake --build "$YUVMIX_VERIFY_ROOT/freetype-build" --parallel
cmake --install "$YUVMIX_VERIFY_ROOT/freetype-build"
```

Expected: the dependency directory contains PIC `libyuv.a` and
`libfreetype.a`. The workflow runs the equivalent commands without
`CMAKE_OSX_ARCHITECTURES` on Linux x86_64.

- [ ] **Step 2: Configure and test the static-dependency release build**

```bash
cmake -S . -B "$YUVMIX_VERIFY_ROOT/yuvmix-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_TESTING=ON \
  -DYUVMIX_LINK_DEPS_STATIC=ON \
  -DYUVMIX_DEPS_ROOT="$YUVMIX_VERIFY_ROOT/deps" \
  -DYUVMIX_TEST_FONT=/System/Library/Fonts/SFNSMono.ttf
cmake --build "$YUVMIX_VERIFY_ROOT/yuvmix-build" --parallel
ctest --test-dir "$YUVMIX_VERIFY_ROOT/yuvmix-build" --output-on-failure
```

Expected: configuration, shared-library link, and all tests PASS.

- [ ] **Step 3: Inspect runtime dependencies**

On macOS arm64:

```bash
file "$YUVMIX_VERIFY_ROOT/yuvmix-build/libyuvmix_video.dylib"
otool -L "$YUVMIX_VERIFY_ROOT/yuvmix-build/libyuvmix_video.dylib"
```

Expected: `file` reports an arm64 Mach-O dynamically linked shared library;
`otool -L` does not list separate libyuv or FreeType dynamic libraries.

On Linux x86_64:

```bash
file build-release/libyuvmix_video.so
ldd build-release/libyuvmix_video.so
```

Expected: `file` reports an x86-64 ELF shared object; `ldd` does not list
separate libyuv or FreeType shared libraries.

- [ ] **Step 4: Run final repository checks**

```bash
ctest --test-dir "$YUVMIX_VERIFY_ROOT/yuvmix-build" --output-on-failure
git diff --check
git status --short
```

Expected: all tests PASS, no whitespace errors, and only intentional plan or
implementation changes remain.
