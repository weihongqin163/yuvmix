# Colored Narrow-Band Highlight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore a fixed-color high-contrast border while retaining the mask-free perimeter-scaled highlight renderer.

**Architecture:** Keep the existing Y-plane four-band fills. Enumerate the corresponding I420 chroma ring, calculate union coverage for each 2x2 luma block, and blend fixed highlight U/V values exactly once per chroma sample.

**Tech Stack:** C++11, I420 planar video, CMake, CTest, libyuv, FreeType

---

### Task 1: Add Colored Highlight Regression Coverage

**Files:**
- Modify: `tests/unit/i420_highlight_test.cc`
- Modify: `tests/unit/mix_yuv_test.cc`
- Modify: `tests/integration/mix_yuv_four_source_test.cc`

- [ ] **Step 1: Write failing assertions**

Add exact U/V expectations for partial coverage in the focused highlight test
and fixed `U=113`, `V=35` expectations for fully covered integration samples.

- [ ] **Step 2: Run the focused tests**

Run the highlight, mixer, and four-source tests. Expect failures showing that the
current Y-only renderer leaves source chroma unchanged.

### Task 2: Render Chroma In Narrow Bands

**Files:**
- Modify: `src/video/i420_highlight.cc`

- [ ] **Step 1: Add border geometry helpers**

Represent clipped outer and inner rectangles once so Y and chroma paths share
the same geometry.

- [ ] **Step 2: Enumerate chroma candidates**

Partition each chroma ring into top, bottom, left, and right bands. Skip samples
already owned by an earlier highlighted rectangle.

- [ ] **Step 3: Blend union coverage**

For each unique candidate, count the four covered luma pixels across all
highlight rectangles and blend `U=113`, `V=35` with the existing sample.

- [ ] **Step 4: Run focused and full tests**

Confirm the regression tests pass, then run the complete CTest suite.

### Task 3: Document And Benchmark

**Files:**
- Create: `tests/performance/i420_highlight_benchmark.cc`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/perform/perferm_test.md`

- [ ] **Step 1: Run the Release benchmark**

Use 10 warmups and 1000 measured iterations through the existing benchmark.

- [ ] **Step 2: Record the investigation and result**

Document the Y-only visibility regression, its test gap, the colored narrow-band
fix, and a separately labeled benchmark result.

- [ ] **Step 3: Review the final diff**

Confirm only intended source, test, design, plan, and performance-documentation
files changed; preserve the user's existing YUV artifacts.
