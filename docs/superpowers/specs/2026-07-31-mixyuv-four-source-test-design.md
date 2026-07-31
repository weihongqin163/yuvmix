# MixYuv Four-Source 720p Artifact Test Design

## 1. Goal

Add an independent C++11 integration test executable that constructs four synthetic
1280x720 I420 sources, composites them through the existing `MixYuv` API, and writes
the resulting tightly-packed 1280x720 I420 frame to `tests/mix-4-720.yuv`.

The output file is a test artifact. The test must exercise the real compositor; it
must not create the final frame by directly concatenating or copying quadrant data.

## 2. Inputs

The test creates four caller-owned I420 images. Each image is 1280x720 and is filled
with one BT.709 limited-range color:

| Source | Display name | Color | Y | U | V | Highlight |
|---|---|---:|---:|---:|---:|---:|
| 1 | `agora-yuv-1` | red | 63 | 102 | 240 | yes |
| 2 | `agora-yuv-2` | blue | 32 | 240 | 118 | no |
| 3 | `agora-yuv-3` | yellow | 219 | 16 | 138 | no |
| 4 | `agora-yuv-4` | green | 173 | 42 | 26 | no |

All sources use `FillMode::kContain`. Because every 640x360 destination has the same
16:9 aspect ratio as the 1280x720 source, the media fills its destination without
letterboxing or cropping.

## 3. Layout And Decoration

The output is 1280x720 with BT.709 limited-range black background `{16, 128, 128}`.
Sources are passed to one `MixYuv` call in this order:

```text
+-----------------------+-----------------------+
| red                   | blue                  |
| agora-yuv-1           | agora-yuv-2           |
| Rect {0,0,640,360}    | Rect {640,0,640,360}  |
+-----------------------+-----------------------+
| yellow                | green                 |
| agora-yuv-3           | agora-yuv-4           |
| Rect {0,360,640,360}  | Rect {640,360,640,360}|
+-----------------------+-----------------------+
```

Source 1 has `is_highlight=true`. The existing MixYuv highlight implementation
determines the border width and BT.709-independent highlight YUV values. All four
names are drawn by the existing FreeType OSD stage using one context font size and
margin configuration.

## 4. Test Executable And File Output

Create `tests/integration/mix_yuv_four_source_test.cc` and register it as
`mix_yuv_four_source_test` in CMake. The executable accepts one optional output path.
CTest passes a path in the build directory so routine tests do not modify the source
tree. After the test passes, the same executable is invoked with
`tests/mix-4-720.yuv` to create the requested workspace artifact.

The writer serializes only active pixels, ignoring fixture padding:

1. 1280x720 Y rows;
2. 640x360 U rows;
3. 640x360 V rows.

The required file size is `1280 * 720 * 3 / 2 = 1,382,400` bytes. Existing files are
opened with truncation only after `MixYuv` succeeds.

## 5. Assertions And Failure Handling

The executable fails when context creation, composition, file creation, writing, or
closing fails. It also asserts:

- the output buffer guard bytes and padding remain intact;
- representative interior Y/U/V samples match each source color;
- the first source border is highlighted;
- at least one luma pixel in each name area differs from its media color;
- the written file has exactly 1,382,400 bytes.

The test returns a nonzero exit status on any failure and leaves no successful-looking
partial artifact: write failures are reported and the path is removed when possible.

## 6. Portability

The test uses the existing `YUVMIX_TEST_FONT` CMake setting and works with both static
and system dependency modes. The checked-in artifact is generated on the current
macOS arm64 environment; CTest regenerates an equivalent frame for each supported
platform using that platform's configured test font. Exact media colors and layout
are portable, while glyph rasterization may vary if different font files are used.
