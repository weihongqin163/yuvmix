# Colored Narrow-Band Highlight Design

## Goal

Restore a visually distinct fixed-color active-speaker border without restoring
the output-sized highlight mask or any full-frame highlight scan.

## Rendering Model

The existing four-band Y renderer remains unchanged in geometry and continues to
write `Y=143`. Chroma rendering writes `U=113` and `V=35` only for I420 chroma
samples touched by those border bands.

Each chroma sample represents a 2x2 luma block. The renderer counts how many of
those four luma pixels belong to the union of all highlighted borders and blends
the fixed highlight chroma with the existing output chroma using that coverage.
Full coverage replaces the sample; partial coverage preserves the old
anti-spill behavior at odd border boundaries.

## Work Bound

Chroma candidates are enumerated as four non-overlapping bands around each
highlight rectangle. The renderer does not scan the rectangle interior or the
complete output plane and does not allocate a mask. If highlighted borders
overlap, a candidate owned by an earlier rectangle is skipped and union coverage
is computed once, preventing repeated blending.

The work therefore scales with highlighted perimeter and highlight count rather
than output area. In the four-source 720p scenario, the renderer touches about
6,000 luma pixels and 1,500 chroma samples for one highlighted 640x360 cell.

## Validation And Errors

Existing output and rectangle validation remains complete before the first
write. Invalid arguments continue to leave the output unchanged. Plane guards,
row padding, output-edge clipping, collapsed inner rectangles, adjacent
highlights, and overlapping highlights remain supported.

## Testing

Focused tests verify exact chroma values for partial and full 2x2 coverage,
unchanged chroma outside the border, adjacent-highlight union behavior, and
allocation guards. The MixYuv integration tests verify that a highlighted source
receives the fixed highlight U/V values rather than inheriting source chroma.

The full CTest suite and the Release four-source benchmark must run after the
change. The performance report must retain the historical Y-only result and add
the colored narrow-band result as a separate measurement.
