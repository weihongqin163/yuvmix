# I420 多 Source 原地 Alpha Blend 设计

日期：2026-08-15

状态：已确认，待用户审阅文档

## 1. 目标与场景

新增一个轻量函数，把 N 个静态 source 以各自固定 alpha 混合到实时 I420
background 上。典型场景是在 SIP 会议终端的视频画面上叠加交互提示。

background 通常不超过 1080p，目标帧率为 60 fps；常见 source 数量不超过 4，
单个 source 不超过 640p，但公共接口不设置 source 数量上限。source 的内容、显示
尺寸、位置和 alpha 在实时合成期间均不变化。调用方在调用前完成图片解码、格式
转换和缩放。

设计优先级依次为：

1. 避免整帧颜色格式往返转换和临时缓冲；
2. 只处理 source 覆盖区域；
3. 保持未覆盖 background 像素完全不变；
4. 接受 I420 4:2:0 色度采样带来的少量边缘色差；
5. 同时提供 C++ 和纯 C API。

首版明确不包含：

- source 解码、缩放或像素格式转换；
- 逐像素 alpha plane、透明 mask、阴影或不规则透明边缘；
- 动态位置、动态尺寸或动态 alpha 的专用缓存机制；
- source 重叠检测、裁剪或 z-order；
- BT.601/BT.709、limited/full-range 之间的转换；
- 函数内部多线程调度；
- NV12 或其他 YUV/RGB 输入格式。

## 2. 方案选择

采用直接在 I420 Y/U/V plane 上进行原地 alpha blend 的方案。

未采用局部 ROI 或整帧 I420 到 ARGB 的转换方案，因为它们需要额外格式转换、
临时内存和更高内存带宽。整帧转换还会改变未被 source 覆盖的 background 像素。
本方案的实时工作量只与全部 source 的覆盖总面积成正比。

由于每个 source 使用统一 alpha，而不是逐像素 alpha mask，并且 source 的位置与
尺寸均为偶数，Y/U/V 可以直接对应混合。色差主要来自 I420 本身的 4:2:0 色度
分辨率，不会引入额外的全帧 RGB/YUV 往返误差。

## 3. C++ 公共接口

在 `src/video/mix_yuv.h` 中新增：

```cpp
struct I420BlendSource {
    I420ImageView image;
    uint32_t x;
    uint32_t y;
    uint8_t alpha;
};

MixYuvStatus AlphaBlendI420(
    const I420BlendSource* sources,
    size_t source_count,
    MutableI420ImageView* background);
```

该函数不需要 `MixYuvContext`，不依赖 FreeType，也不保存调用方提供的指针。

函数声明上方必须包含公共契约注释，明确以下内容：

- `background` 是输入输出缓冲，成功时被原地修改；
- source 必须已经转换并缩放为最终显示尺寸的 I420；
- 所有 source 和 background 必须使用相同 YUV 色彩空间及范围；
- `x`、`y`、source width 和 source height 必须为正偶数；
- 每个 source 必须完整位于 background 内，越界不执行裁剪；
- source 区域必须互不重叠，该条件由调用方保证，函数不做运行时检查；
- 任一 source plane 都不得与 background plane 内存重叠；
- `alpha` 范围为 0 到 255，0 表示完全透明，255 表示完全覆盖；
- `sources == NULL` 仅在 `source_count == 0` 时合法；
- source 数量没有人为上限；
- 任一校验失败时 background 保持调用前内容不变；
- 函数只修改覆盖区域的有效像素，不修改 stride padding。

“source 区域互不重叠”和“source/background 内存不别名”是调用方前置条件，违反
时行为未定义。函数不为检查这些条件引入 O(N^2) 扫描或复杂内存区间分析。

## 4. 纯 C 公共接口

在 `src/video/mix_yuv_c.h` 中新增：

```c
typedef struct yuvmix_i420_blend_source {
    yuvmix_i420_image image;
    uint32_t x;
    uint32_t y;
    uint8_t alpha;
} yuvmix_i420_blend_source;

yuvmix_status yuvmix_alpha_blend_i420(
    const yuvmix_i420_blend_source* sources,
    size_t source_count,
    yuvmix_mutable_i420_image* background);
```

C 头文件中的函数声明也必须带有与 C++ 接口等价的契约注释。函数使用现有
`yuvmix_status`、I420 image 和 plane 类型，不新增 context 或所有权规则。

C adapter 显式转换 C 结构到 C++ 结构，并调用唯一的 C++ 混合实现。转换 source
数组期间发生内存分配失败时返回 `YUVMIX_STATUS_OUT_OF_MEMORY`；其他异常返回
`YUVMIX_STATUS_INTERNAL_ERROR`。任何异常都不得跨越 C ABI。

## 5. 校验与失败语义

函数在修改 background 前完成全部校验：

1. 校验 `background` 指针、偶数宽高、三个 plane 指针、正 stride 和容量；
2. 校验 `sources` 与 `source_count` 的组合；
3. 校验每个 source 的偶数宽高、三个 plane、stride 和容量；
4. 校验每个 source 的偶数 `x/y`；
5. 使用防溢出的减法形式确认 source 完整位于 background 内。

plane 最小容量沿用现有规则：

```text
required = stride * (rows - 1) + row_bytes
```

Y plane 的 `rows` 和 `row_bytes` 为 `height` 和 `width`；U/V plane 分别使用一半
高度和宽度。所有容量和偏移计算使用带溢出检查的 `size_t` 算术。

即使 source 的 `alpha == 0`，仍校验其图像、plane、坐标和边界，避免参数合法性
依赖运行时 alpha 值。

无效指针、尺寸、stride、坐标或边界返回 `kInvalidArgument`；容量不足返回
`kBufferTooSmall`。全部 source 完成预检后才进入写阶段，因此最后一个 source
无效时 background 也必须保持不变。

C++ 核心写阶段不分配内存、不缩放、不转换格式，也不调用存在正常运行时失败
路径的操作。预检成功后混合应完成并返回 `kOk`，不存在部分写入后返回错误的
正常路径。

## 6. 混合算法

全部预检成功后，按 source 数组顺序处理。因为调用方保证区域互不重叠，数组顺序
不影响结果。

每个 source 先计算一次：

```text
source_weight = alpha
background_weight = 255 - alpha
```

随后分别处理 Y、U、V plane：

- Y 使用 source 的完整宽高，目标位置为 `(x, y)`；
- U/V 使用一半宽高，目标位置为 `(x / 2, y / 2)`。

一般 alpha 对每个 8-bit sample 执行：

```text
result = (source * alpha + background * (255 - alpha) + 127) / 255
```

中间计算使用 `uint32_t`。最大加权和为 65025，加上舍入项也不会溢出。
`+127` 实现最近整数舍入。算法直接混合存储的 8-bit sample，不做 limited-range
裁剪或颜色矩阵变换。

特殊路径：

- `alpha == 0`：完成预检后跳过该 source；
- `alpha == 255`：逐行 `memcpy` source 的 Y/U/V 有效像素；
- 其他 alpha：使用连续逐行整数混合循环。

逐行处理只覆盖有效像素，不写 source、background 的其他区域或任何 stride
padding。

## 7. 代码组织

- `src/video/mix_yuv.h`：C++ source 结构、函数声明和公共契约；
- `src/video/i420_alpha_blend.cc`：校验、plane copy 和通用混合实现；
- `src/video/mix_yuv_c.h`：C source 结构、函数声明和公共契约；
- `src/video/mix_yuv_c.cc`：C 到 C++ 的结构与状态适配；
- `CMakeLists.txt`：把新实现加入现有 `yuvmix_video` shared target。

不创建新的库、context 或公共辅助函数。现有 `MixYuv` 的缩放、OSD、高亮和背景色
填充行为保持不变。

## 8. 性能设计

首版使用无分配、逐行、连续访问的整数循环。函数不创建整帧或 ROI 临时缓冲，
也不启动内部线程。常见的 4 个 source 不足以保证线程调度成本能够被稳定摊销，
调用方也不应为获得正确结果而依赖特定线程模型。

通用循环保持便于编译器自动向量化的结构。增加一个 1920x1080 background 加
4 个 640x480 source 的 benchmark，覆盖非 0/255 alpha，并报告单帧耗时和吞吐量。
性能 benchmark 不作为跨机器的硬编码单元测试断言。

目标是支持 60 fps。由于缺少固定目标 CPU，首版先以 benchmark 数据验证余量。
如果目标设备上通用循环不足，再加入 NEON、SSE 或 AVX 行内核；SIMD 优化必须
保持公共 API、逐 sample 结果和舍入规则完全不变。

## 9. 测试

新增聚焦的 C++ 单元测试，覆盖：

- `alpha` 为 0、1、128、254、255 时精确的 Y/U/V 结果；
- 单 source 和 4 个互不重叠 source；
- `source_count == 0` 的成功 no-op；
- 非紧密 stride、不同 Y/U/V stride、padding 和 guard bytes；
- 奇数图像尺寸、奇数位置、越界、空指针和非法 stride；
- Y/U/V 任一 buffer 容量不足；
- source 数组最后一项无效时 background 完全不变；
- source 内容和调用方结构不被修改；
- 同一组只读 source 并发用于不同 background，不存在共享可变状态。

扩展纯 C 集成测试，验证：

- 严格 C11 编译器可以包含公共头文件并调用新函数；
- C API 的成功结果与 C++ 精确算法一致；
- C API 的空指针、空 source 集合和错误状态映射；
- shared library 导出未修饰的 `yuvmix_alpha_blend_i420` 符号。

不测试重叠 source 或 source/background 内存别名后的具体结果，因为它们属于明确
记录的调用方前置条件。

## 10. 完成标准

- C++ 和纯 C API 均提供带完整契约注释的新函数；
- 直接在调用方 background 上完成 I420 原地混合；
- 精确实现约定的 0..255 alpha 和整数舍入；
- 所有参数在首次写入前完成校验；
- 未覆盖像素和 stride padding 保持不变；
- source 数量无硬编码限制；
- 新增正确性、契约、C ABI 和性能测试；
- 全部现有测试继续通过；
- benchmark 提供 1080p、4 路 640x480、60 fps 场景的性能依据。
