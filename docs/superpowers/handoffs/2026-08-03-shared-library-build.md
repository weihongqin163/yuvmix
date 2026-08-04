# Shared Library Build Handoff

## Resume Point

- Worktree: `/Users/weihognqin/Documents/work/yuvmix/.worktrees/shared-library-build`
- Branch: `feature/shared-library-build`
- Implementation head before this handoff: `fea10d73bb7d773ff417c9d00d10cf0d6df86b26`
- Base branch: `main` at `99c3972fa7a808bed9d65ab44ce140da82a29168`
- The feature branch has not been merged or pushed.

## Completed Work

- `yuvmix_video` is an explicit shared-library target.
- The target explicitly uses default C++ symbol visibility, even when a parent
  build sets `CMAKE_CXX_VISIBILITY_PRESET=hidden`.
- macOS release support is restricted to arm64.
- Linux release support is restricted to x86_64/amd64.
- libyuv and FreeType remain PIC static archives linked into the YuvMix shared
  library.
- Linux shared linking uses `-z defs` to reject unresolved symbols.
- CTest verifies that the real YuvMix artifact exists and has the platform
  shared-library suffix.
- Platform mapping tests cover supported aliases, rejected macOS x86_64, and
  rejected multi-architecture macOS builds with exact diagnostics.
- CI covers macOS arm64 and Linux x86_64, runs the full tests, checks binary
  architecture, and rejects runtime libyuv/FreeType shared dependencies.
- Dependency package documentation matches the supported targets and outputs.

## Verification Evidence

- Fixed-version PIC static dependencies are currently staged at
  `/private/tmp/yuvmix-shared-deps`.
- The latest fresh macOS arm64 Release build used
  `CMAKE_CXX_VISIBILITY_PRESET=hidden` globally and passed 9/9 CTest tests.
- `file` identified `libyuvmix_video.dylib` as an arm64 Mach-O dynamically
  linked shared library.
- `otool -L` listed only the dylib itself, libc++, and libSystem. It did not
  list libyuv or FreeType dynamic libraries.
- `nm -gU` confirmed the existing YuvMix C++ APIs are exported.
- Workflow YAML and all seven Bash run blocks passed local syntax checks.
- The feature worktree was clean before this handoff file was added.

## Pending Work

1. Resume the interrupted final reviewer and review the range
   `99c3972..HEAD`. Confirm that explicit default visibility and Linux
   `LINKER:-z,defs` close the last review findings.
2. Run a final fresh verification under the verification-before-completion
   workflow.
3. Use the finishing-a-development-branch workflow and ask whether to merge,
   push a PR, keep the branch, or discard it.
4. A hosted `ubuntu-24.04` CI run is still required for direct Linux x86_64
   `.so`, CTest, `-z defs`, and `ldd` evidence.

## Known Minor Follow-ups

- The workflow still passes `CMAKE_OSX_ARCHITECTURES=x86_64` on Linux. CMake
  ignores it on non-Apple platforms, but a future cleanup can make the argument
  macOS-only.
- FreeType is cloned by the `VER-2-13-3` tag rather than an immutable commit.
  Pinning its resolved commit would improve dependency reproducibility.
