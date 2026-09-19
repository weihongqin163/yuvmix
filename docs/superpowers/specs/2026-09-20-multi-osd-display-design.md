# Multi-OSD Display and Contain Margin Fill Design

## 1. Goal

Extend `MixYuv` with two related rendering capabilities:

1. optionally fill the uncovered bands produced by `FillMode::kContain` with a
   per-source I420 color; and
2. draw up to three caller-provided I420 OSD icons before the existing display
   name, in the fixed order Network Quality, Microphone Status, Camera Status,
   DisplayName.

Icons keep their original size. Each icon has an independent, uniform alpha
applied to all Y, U, and V samples. DisplayName rendering keeps its current
Y-plane-only glyph coverage behavior.

This design does not add a stretch/fill mode. The only supported source fill
modes remain contain and cover.

## 2. Compatibility and Caller Contract

This change extends the public C and C++ structures and is not binary compatible
with clients compiled against the old structure sizes. Clients must rebuild
against the new header and initialize all structures, including the new fields.
When every new field is zero, output remains compatible with the current
behavior.

All source `destination` rectangles in one `MixYuv` call must be non-overlapping.
The caller guarantees this precondition; the library does not add an O(N^2)
overlap check. Results are unsupported when the precondition is violated.

Icon buffers are borrowed views, like the existing main source image. They must
remain valid for the duration of `yuvmix_mix` or `MixYuv`. Icon images and the
output must use the same YUV matrix and range. The library performs no color
matrix or full/limited-range conversion.

The existing `AlphaBlendI420` C++ API and `yuvmix_alpha_blend_i420` C API keep
their current signatures and contracts.

## 3. Public API

### 3.1 C API

Append `osd_gap` to `yuvmix_config`:

```c
typedef struct yuvmix_config {
    const char* font_path;
    uint32_t font_face_index;
    uint32_t font_size;
    uint32_t osd_left;
    uint32_t osd_bottom;
    uint32_t osd_gap;
} yuvmix_config;
```

Append the margin-fill and icon fields to `yuvmix_source`:

```c
typedef struct yuvmix_source {
    yuvmix_i420_image image;
    yuvmix_rect destination;
    const char* display_name;
    yuvmix_fill_mode fill_mode;
    int is_highlight;

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
} yuvmix_source;
```

All new C flags use the existing `is_highlight` convention: zero is false and
any nonzero value is true. `osd_gap` is unsigned, so negative spacing and icon
overlap through the gap setting are not supported.

The C adapter converts image and plane structures to non-owning C++ views and
normalizes every integer flag with `value != 0`. It does not copy pixel data.

### 3.2 C++ API

`MixYuvConfig` and `MixSource` mirror the new C fields. C++ flags use `bool`;
image fields use `I420ImageView`; alpha and color fields use `uint8_t`; and
`osd_gap` uses `uint32_t`.

The public field names remain the same in C and C++ so that the adapter is a
direct field mapping.

## 4. Architecture

Add an internal `i420_osd.h/.cc` module containing `OsdRenderer`. Each
`MixYuvContext` owns one renderer, and each renderer owns one `FreeTypeOsd`.

Responsibilities are divided as follows:

- `mix_yuv_c.h/.cc`: public C structures and C-to-C++ conversion;
- `mix_yuv.h/.cc`: public C++ structures, whole-call validation, source geometry,
  contain margin fill, rendering-stage orchestration, and error translation;
- `i420_osd.h/.cc`: icon validation, OSD layout, clipping, uniform I420 alpha
  blending, and final text coordinates;
- `freetype_osd.h/.cc`: UTF-8 decoding, glyph preparation and cache ownership,
  and Y-plane glyph coverage blending at explicit text coordinates.

Contain margin fill stays in the source/video path because it depends on the
main source geometry. It is not part of `OsdRenderer`.

`FreeTypeOsd` gains an internal drawing entry point that accepts explicit
`pen_x` and `baseline_y`. Glyph placement, clipping, coverage arithmetic, cache
behavior, and missing-glyph behavior remain unchanged.

## 5. Prepared Plans

`SourcePlan` continues to hold the main source pointer and `GeometryPlan`, and
adds an `OsdPlan` prepared before any output is written.

`OsdPlan` contains:

- a fixed-capacity array for at most three `OsdIconPlan` values;
- the count of drawable, non-empty clipped icon plans;
- the prepared `TextRun`;
- the final signed 64-bit text `pen_x` and `baseline_y`; and
- the source `destination` used as the text clip.

Each `OsdIconPlan` contains:

- a non-owning view of the original icon image;
- even source crop offsets;
- an even destination rectangle already intersected with the source
  `destination`; and
- the uniform alpha.

The fixed icon array avoids per-source dynamic allocation for icon plans.
`TextRun` keeps its existing prepared glyph storage.

## 6. Preflight

`MixYuv` completes the following work for every source before calling
`FillOutput`:

1. validate the output and the existing main source fields;
2. validate `destination` and build the contain/cover `GeometryPlan`;
3. validate every icon whose `is_*` flag is nonzero;
4. prepare the DisplayName `TextRun` using the existing strict UTF-8 rules;
5. calculate icon placement, clipping, and final text coordinates; and
6. collect highlighted destination rectangles.

An icon with `is_* == 0` is completely ignored. Its image may be empty or
invalid. An icon with `is_* != 0` is fully validated even when its alpha is
zero. This keeps input validity independent of runtime opacity.

Every enabled icon image must satisfy the existing I420 input rules: width and
height are even and at least two, planes and positive strides are valid, and
plane capacities are sufficient. Invalid pointers, sizes, strides, or layout
values return `kInvalidArgument`; insufficient plane capacity returns
`kBufferTooSmall`.

An enabled icon with alpha zero is not visible, does not enter the icon plan,
does not advance the horizontal cursor, and does not trigger even-coordinate
adjustment. Its image is still validated.

All layout calculations use signed 64-bit integers. Large unsigned OSD margins
or gaps therefore move content outside the clip without unsigned wraparound.
Content fully outside `destination` is not an error.

## 7. Contain Margin Fill

For a source with `is_fill_margin_color == true` and contain geometry that does
not cover all of `destination`, fill only the uncovered bands with `y_color`,
`u_color`, and `v_color`. Then copy or scale the main image into the geometry's
draw rectangle.

If margin fill is disabled, the uncovered bands are not written. Because source
destinations do not overlap and the output is initialized first, those pixels
retain the output background color.

Cover mode never applies the per-source margin color. This includes the current
no-upscale path, where a source smaller in both dimensions is copied at its
original size and can leave uncovered pixels even in cover mode; those pixels
retain the output background. Contain geometry with no uncovered bands also
performs no margin-fill write. Margin filling writes only valid pixels and never
modifies plane padding. Existing even destination and geometry guarantees keep
the Y and U/V band boundaries chroma-aligned.

## 8. OSD Layout

The unadjusted text baseline remains:

```text
raw_baseline_y = destination.y
               + destination.h
               - osd_bottom
               - descender_pixels
```

Visible icons are those with a nonzero `is_*` flag and alpha greater than zero.

### 8.1 No Visible Icons

When there are no visible icons, preserve the old text positioning exactly:

```text
text_pen_x = destination.x + osd_left
text_baseline_y = raw_baseline_y
```

No coordinate is rounded. This includes the case where icons are enabled but
all enabled icon alphas are zero.

### 8.2 One or More Visible Icons

When at least one icon is visible, move the shared bottom baseline upward to the
largest even integer not greater than `raw_baseline_y`:

```text
baseline_y = floor_to_even(raw_baseline_y)
cursor_x = destination.x + osd_left
```

Process Network Quality, Microphone Status, and Camera Status in that fixed
order. Skip icons that are disabled or have alpha zero. For each visible icon:

```text
icon_x = ceil_to_even(cursor_x)
icon_y = baseline_y - icon.height
full_icon_rect = {icon_x, icon_y, icon.width, icon.height}
clipped_rect = intersection(full_icon_rect, destination)
cursor_x = icon_x + icon.width + osd_gap
```

The icon width and height are even, so `icon_y` is even. `destination` and its
right and bottom edges are even. Intersecting the two even rectangles therefore
produces even crop offsets, coordinates, width, and height suitable for direct
I420 plane blending.

If `clipped_rect` is non-empty, store a drawable icon plan with source offsets
derived from its displacement from `full_icon_rect`. If the icon is completely
clipped, store no drawable plan, but still advance `cursor_x` by the icon's full
width and the configured gap.

After the last visible icon:

```text
text_pen_x = cursor_x
text_baseline_y = baseline_y
```

Text `pen_x` is not rounded. Consequently, inter-icon spacing can be one pixel
larger than `osd_gap` when the next icon requires inward even alignment, while
the final icon-to-text gap remains exactly `osd_gap`.

All icon rectangles and text glyphs are clipped strictly to `destination`.
Clipping discards pixels and never scales an icon. The horizontal cursor always
uses the full icon width, not its visible clipped width.

## 9. Rendering Order and Blending

After all source plans pass preflight, render in this order:

1. fill the output with `background_color`;
2. in source-array order, fill each enabled contain margin and draw its main
   image;
3. draw all highlights with the existing highlight implementation; and
4. in source-array order, draw each source's Network, Mic, and Camera icon plans,
   then its DisplayName.

Thus icons and text can cover the inner part of a highlight. Destination
clipping plus the non-overlap caller contract prevents OSD from entering another
source's destination.

For each icon plane, alpha 1 through 254 uses:

```text
result = (source * alpha + destination * (255 - alpha) + 127) / 255
```

Alpha 255 uses a row-copy fast path. Alpha zero never reaches the draw path.
The same uniform alpha applies independently to Y, U, and V, preserving icon
color. Plane padding is not written.

DisplayName rendering remains Y-only and keeps the existing glyph coverage
formula with white target value 235. It does not modify U or V.

OSD drawing performs no allocation and has no normal runtime failure branch
after successful preflight.

## 10. Error and Exception Safety

All validation, OSD plan construction, UTF-8 decoding, glyph loading, and
required allocation complete before output modification begins. Errors from
these operations leave output unchanged.

Public C++ exceptions keep the current mapping:

- allocation failure during preflight returns `kOutOfMemory`;
- unexpected preflight exceptions return `kInternalError` without modifying
  output; and
- an unexpected exception after writing begins returns `kInternalError` and may
  leave partial output.

The existing theoretical `libyuv` draw-time failure behavior remains unchanged:
a failure after output writing begins can leave partial output. The new margin
fill and OSD draw loops are infallible after preflight.

## 11. Concurrency and Performance

The change adds no global mutable state. Separate contexts remain usable in
parallel under the current contract; concurrent calls on the same context remain
unsupported.

OSD icon layout uses fixed storage for three icons. Draw loops access each plane
row sequentially, use integer arithmetic, and include the alpha-255 row-copy
fast path. No icon-sized or frame-sized temporary buffer is allocated.

Add a benchmark case with four sources and three icons per source. Record timing
and throughput without introducing a platform-sensitive pass/fail threshold in
the first version.

## 12. Testing

### 12.1 OSD Unit Tests

Add focused `i420_osd_test` coverage for:

- fixed Network, Mic, Camera ordering with different even dimensions;
- exact gap and DisplayName start positioning;
- inward one-pixel alignment from odd calculated coordinates;
- reachable top/right icon clipping and complete clipping, while retaining the
  existing all-edge text clipping coverage;
- no writes outside destination or into plane padding;
- exact Y/U/V results for alpha 0, 1, 128, 254, and 255;
- disabled invalid images being ignored;
- enabled alpha-zero images being validated but not drawn or laid out;
- invalid dimensions, planes, strides, and capacities; and
- output preservation when the last prepared icon is invalid.

### 12.2 MixYuv Tests

Extend the main unit and contract tests for:

- horizontal and vertical contain bands with exact Y/U/V fill values;
- disabled margin fill preserving the output background;
- cover preserving its current no-margin-fill behavior, including the
  no-upscale path that can leave uncovered pixels;
- contain geometry that covers all of destination producing no margin-fill
  write;
- main image, highlight, icon, and text layer order;
- pixel-identical legacy text placement when no icon is visible;
- adjacent, non-overlapping source destinations remaining isolated;
- output and plane-padding guards; and
- no draw-time allocation after plan preparation.

### 12.3 C Integration

Extend the strict C11 integration test to initialize every new public field,
verify nonzero integer flags, exercise all three icons, and produce a sample
I420 output frame. Existing API signature and exported-symbol checks remain in
place.

Run the complete existing test suite in addition to the new focused tests.

## 13. Non-Goals

This feature does not include:

- a stretch/fill source mode;
- per-pixel icon alpha masks or chroma-key transparency;
- icon scaling, automatic state selection, or asset ownership;
- negative OSD gaps;
- YUV matrix or range conversion;
- destination overlap detection;
- text shaping, wrapping, centering, or right alignment; or
- changes to the standalone public I420 alpha-blend APIs.
