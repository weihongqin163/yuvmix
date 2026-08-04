# macOS and Linux Shared Library Build Design

## Goal

Build `yuvmix_video` as a shared library on every supported macOS and Linux
target. The resulting primary artifacts are `libyuvmix_video.dylib` on macOS
and `libyuvmix_video.so` on Linux.

## Scope

- Change the `yuvmix_video` CMake target from a static library to a shared
  library.
- Export the existing C++ API declared under `src/video` by using normal
  platform visibility for this target.
- Continue linking libyuv and FreeType into `yuvmix_video` as static
  dependencies through the existing `YUVMIX_LINK_DEPS_STATIC=ON` release path.
- Support macOS arm64 and Linux x86_64 release targets. macOS x86_64 and
  32-bit x86 are not release targets.

This change does not add installation rules, package metadata, library version
or SONAME settings, CI artifact uploads, or macOS Universal Binary assembly.
It does not add a static `yuvmix_video` build option.

## CMake Design

Declare `yuvmix_video` with `add_library(... SHARED ...)`. Remove its hidden
C++ visibility setting so the existing C++ declarations remain linkable from
consumers and the test executables. Keep `CXX_EXTENSIONS OFF` and all existing
include and link relationships.

`YuvMix::Freetype` remains a private static dependency. `LibYuv::LibYuv`
remains a static dependency with its existing public link relationship. The
prebuilt dependency archives must remain position-independent; the current CI
already builds both dependencies with `CMAKE_POSITION_INDEPENDENT_CODE=ON`.

The development-only `YUVMIX_LINK_DEPS_STATIC=OFF` path is unchanged, but it is
not a release configuration. Release builds continue to require
`YUVMIX_LINK_DEPS_STATIC=ON`.

Restrict the static dependency platform mapping to `macos-arm64` for Darwin
and `linux-x86_64` for Linux. Configuration fails for unsupported operating
system and architecture combinations instead of selecting another package.

## Verification

Add a cross-platform CTest that receives the built `yuvmix_video` target path
and checks that its filename ends with `CMAKE_SHARED_LIBRARY_SUFFIX`. This test
must fail against the current static target and pass after the target becomes
shared.

Build and run the existing test suite against the shared library. Successful
links and test execution verify that the current C++ API is exported and that
the runtime loader can resolve the shared library. Inspect the produced library
on the host platform to confirm that it contains the static libyuv and FreeType
code without runtime dependencies on separate libyuv or FreeType shared
libraries.

Update the repository CI matrix and platform mapping test so macOS arm64 and
Linux x86_64 are the cross-platform release authorities.
