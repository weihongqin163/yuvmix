# Prebuilt dependency packages

Release builds consume one architecture-specific package selected by CMake:

```text
third_party/prebuilt/
  macos-arm64/
  linux-x86_64/
```

Release builds produce `libyuvmix_video.dylib` on macOS arm64 and
`libyuvmix_video.so` on Linux x86_64. The libyuv and FreeType dependencies
remain static archives linked into the final shared library.

Each platform directory must contain:

```text
include/libyuv/*.h
include/freetype2/ft2build.h
include/freetype2/freetype/*.h
lib/libyuv.a
lib/libfreetype.a
MANIFEST.md
LICENSES/
```

`MANIFEST.md` records the libyuv and FreeType source revisions, compiler and
version, target architecture, minimum OS or glibc baseline, C/C++ runtime,
build flags, and every transitive static library. Linux archives must be built
with `-fPIC`.

Configure a release build with the repository package:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DYUVMIX_LINK_DEPS_STATIC=ON
```

Or point at an external package root for the current target:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DYUVMIX_LINK_DEPS_STATIC=ON \
  -DYUVMIX_DEPS_ROOT=/absolute/path/to/platform-package \
  -DYUVMIX_FREETYPE_STATIC_LINK_LIBRARIES="dependency-a;dependency-b"
```

macOS release builds require arm64. Development builds may explicitly use
installed libraries with `-DYUVMIX_LINK_DEPS_STATIC=OFF`.

The `MixYuv` GitHub Actions workflow builds pinned static dependencies and runs
the Release test suite on macOS arm64 and Linux x86_64. Published packages still
require the per-platform `MANIFEST.md` and `LICENSES/` content listed above.
