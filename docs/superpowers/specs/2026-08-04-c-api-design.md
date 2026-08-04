# YuvMix Pure C API Design

## Goal

Add a directly linkable pure C API to the existing `yuvmix_video` shared
library. C callers must be able to create a reusable mixing context, mix an
arbitrary number of I420 sources, and destroy the context without depending on
C++ types or name-mangled symbols.

The existing C++ public API and implementation remain unchanged. The C API is
a thin adapter over that implementation.

## Scope

The C API exposes the existing mixing capabilities:

- reusable context creation and destruction;
- arbitrary source counts;
- caller-owned I420 planes with explicit strides and buffer sizes;
- contain and cover fill modes;
- destination rectangles;
- display-name OSD;
- source highlighting;
- caller-owned output buffers and background color; and
- status-based error reporting.

This change does not replace or modify the existing C++ API. It does not add
installation rules, runtime `dlopen` helpers, a function table, ABI versioning,
Windows-specific support, or ownership of caller image buffers.

## Public Header

Create `src/video/mix_yuv_c.h`. It includes only C standard headers and is
valid when compiled as C. An `extern "C"` guard gives the declarations C
linkage when included from C++.

The header defines:

- `yuvmix_status` with explicit values corresponding to every existing
  `MixYuvStatus` result;
- `yuvmix_fill_mode` for contain and cover;
- const and mutable plane structures containing data, stride, and size;
- const and mutable I420 image views;
- a rectangle, I420 color, source, output, and context configuration; and
- the opaque `yuvmix_context` type.

The source structure uses `const char* display_name` and an integer highlight
flag. No public structure contains a C++ type.

The exported functions are:

```c
yuvmix_status yuvmix_context_create(
    const yuvmix_config* config,
    yuvmix_context** context);

void yuvmix_context_destroy(yuvmix_context* context);

yuvmix_status yuvmix_mix(
    yuvmix_context* context,
    const yuvmix_source* sources,
    size_t source_count,
    yuvmix_output* output);
```

The first release intentionally has no `struct_size`, API version function, or
versioned symbol names. Its public structure layouts may change in a future
release.

## Adapter Implementation

Create `src/video/mix_yuv_c.cc` and compile it into the existing
`yuvmix_video` shared target. Do not edit `src/video/mix_yuv.h` or
`src/video/mix_yuv.cc`.

The opaque C context owns one existing `yuvmix::MixYuvContext`. Context
creation converts the C configuration to `yuvmix::MixYuvConfig` and calls the
existing factory. Mixing converts the C source and output structures to the
corresponding C++ value types, then calls the existing `yuvmix::MixYuv`.

The adapter maps every C++ status explicitly to its C equivalent. It does not
depend on the two enums retaining identical layouts or implicit conversions.

All exceptions are caught inside the adapter. Allocation failures return
`YUVMIX_STATUS_OUT_OF_MEMORY`; all other exceptions return
`YUVMIX_STATUS_INTERNAL_ERROR`. No exception may cross the C ABI.

## Ownership And Validation

On entry, `yuvmix_context_create` clears the caller's output context. A null
configuration, null context output pointer, or null font path returns
`YUVMIX_STATUS_INVALID_ARGUMENT`. On success, the caller owns the context and
must release it with `yuvmix_context_destroy`. Destroying a null context is a
no-op.

The adapter copies the font path during context creation. Input and output
image buffers remain caller-owned. Source display names remain caller-owned
and need to be valid only for the duration of `yuvmix_mix`; a null display name
is treated as an empty string.

Invalid C enum values are rejected with `YUVMIX_STATUS_INVALID_ARGUMENT`.
Image dimensions, strides, plane sizes, destinations, and other behavioral
validation continue to use the existing C++ implementation.

A single context is not safe for concurrent calls. Independent contexts may
be used by different threads.

## Build Integration

Enable the C language in the top-level CMake project so a real C consumer can
be compiled. Add `mix_yuv_c.cc` to the existing `yuvmix_video` target. The C
API does not create a second library and uses the target's existing default
symbol visibility.

The current C++ four-source integration test remains unchanged.

## Testing

Add `tests/integration/mix_yuv_c_api_test.c` and compile it as C. The test
includes only `video/mix_yuv_c.h` and C standard headers, links the existing
shared library target, and exercises the exported C ABI.

The test creates four I420 sources, a reusable context, and an output image. It
calls `yuvmix_mix`, verifies successful status and representative output colors,
and destroys the context. Focused invalid-argument cases verify null handling
and invalid enum rejection.

Implementation follows red-green-refactor:

1. Register the C test and observe compilation or linkage fail because the C
   API is absent.
2. Add the minimal header and adapter needed to pass.
3. Run the focused C test and the full CTest suite.
4. Inspect the shared library symbol table for unmangled
   `yuvmix_context_create`, `yuvmix_mix`, and `yuvmix_context_destroy` exports.

## Success Criteria

- A C compiler accepts the public header and integration test.
- A C executable directly links and runs against `libyuvmix_video.dylib` or
  `libyuvmix_video.so`.
- The complete existing C++ test suite remains unchanged and passes.
- The three public C functions are exported with unmangled names.
- `src/video/mix_yuv.h` and `src/video/mix_yuv.cc` have no changes.
