# Y-Only Highlight Optimization Design

## 1. Goal

Optimize active-speaker highlight rendering for the four-source 720p mix. The
implementation prioritizes minimum CPU time over preserving the current colored
border. It must eliminate the output-sized highlight mask and all full-frame
highlight scans.

## 2. Performance Principle

Highlight rendering modifies only the I420 Y plane. The U and V planes remain
unchanged. A luminance-only border is an intentional visual trade-off and is the
primary design rule for this optimization.

The renderer writes only the pixels covered by the border. Its work scales with
border area instead of output-frame area, and it performs no highlight-specific
dynamic allocation.

## 3. Border Geometry

The existing border width and placement rules remain unchanged:

- `BorderWidth(cell_height)` continues to determine the total border width.
- The inside width is `(border_width + 1) / 2`.
- The outside width is `border_width / 2`.
- The outer border rectangle is clipped to the output dimensions.
- The inner rectangle is derived from the source destination rectangle.

The area between the outer and inner rectangles is partitioned into four
non-overlapping bands:

1. top: the full outer width above the inner rectangle;
2. bottom: the full outer width below the inner rectangle;
3. left: the middle rows left of the inner rectangle;
4. right: the middle rows right of the inner rectangle.

If the inner rectangle collapses, the renderer fills the complete clipped outer
rectangle. Each row span is written as a contiguous fill with highlight value
`Y=143`.

## 4. Multiple Highlights

Every highlighted source is rendered independently. Overlapping bands may write
the same Y pixels more than once, which is safe and deterministic because every
write uses the same constant value. No union mask or overlap bookkeeping is
needed.

This intentionally replaces the old exact YUV union and chroma coverage blend.
The Y-plane result retains the same border geometry; U/V union semantics no
longer apply because U/V are not modified.

## 5. API And Context Changes

`DrawHighlights` no longer accepts a mask argument. `EnsureHighlightMask` and
`BlendHighlightChroma` are removed. `MixYuvContext::Impl` no longer owns
`highlight_mask`, and `MixYuv` no longer performs mask allocation during
preflight.

The testing-only `MixYuvContextHighlightMaskCapacity` hook is removed together
with the storage it inspected. These are internal C++ interfaces and are not part
of a stable installed public API.

All output and rectangle validation still completes before the first highlight
pixel is written. Invalid input therefore preserves the existing no-partial-write
behavior.

## 6. Testing

Tests will be updated before production code and observed failing against the old
API/behavior. The focused highlight tests cover:

- existing border-width calculations;
- unchanged Y geometry for interior and output-edge rectangles;
- a collapsed inner rectangle;
- overlapping or adjacent highlights;
- unchanged U and V planes;
- row padding and allocation guards;
- invalid arguments rejected before output modification.

The context contract test will stop asserting output-sized mask allocation and
will continue verifying zero stable-state allocations. The complete CTest suite
must pass after the change.

## 7. Performance Verification

The benchmark uses the existing four-source scenario: four 1280x720 I420 inputs
scaled into a 2x2 1280x720 output, one highlighted source, four OSD names, Release
build, 10 warm-up calls, and 1000 measured calls. The report records the median
complete `MixYuv` time before and after the optimization.

Success requires a substantial measured reduction from the documented 0.97 ms
baseline. Absolute timing is machine-dependent, so correctness and removal of
full-frame highlight work are hard requirements while the measured median is
reported rather than encoded as a flaky unit-test threshold.

## 8. Scope

This change does not alter layout, scaling, background fill, OSD rendering,
public source/output structures, or highlight color configuration. It does not
optimize other render stages.
