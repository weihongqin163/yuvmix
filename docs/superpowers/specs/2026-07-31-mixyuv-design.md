# MixYuv I420 合流函数设计

日期：2026-07-31

状态：待用户最终审阅

## 1. 目标与范围

`MixYuv` 接收一组 I420 source，根据每个 source 的目标矩形和
Contain/Cover 模式，把画面合成到调用方预分配的 I420 输出中。函数同时支持
可配置背景色、每 source 昵称 OSD 和每 source 高亮边框。

实现使用 C++11、libyuv 和 FreeType。`MixYuv` 是库内部 C++ 接口，不向动态库
公共 ABI 暴露 STL 类型。调用方负责输入和输出 plane 的分配与生命周期；函数
不保存任何输入或输出 plane 指针。

首版明确不包含：

- YUV 格式转换、色彩空间转换或 full-range/limited-range 转换；
- 旋转、镜像、透明度混合或 source 间转场；
- 复杂文字 shaping、双向排版、连字或组合字符定位；
- 系统字体发现或多字体 fallback；
- Rect 相交检测和输入、输出内存别名检测；
- context 的并发调用支持。

## 2. 数据格式与接口

### 2.1 基础类型

```cpp
enum class FillMode {
    kContain,
    kCover,
};

enum class MixYuvStatus {
    kOk = 0,
    kInvalidArgument,
    kBufferTooSmall,
    kOutOfMemory,
    kFontError,
    kLibyuvError,
    kInternalError,
};

struct ConstPlane {
    const uint8_t* data;
    int stride;
    size_t size;
};

struct MutablePlane {
    uint8_t* data;
    int stride;
    size_t size;
};

struct I420ImageView {
    ConstPlane y;
    ConstPlane u;
    ConstPlane v;
    uint32_t width;
    uint32_t height;
};

struct MutableI420ImageView {
    MutablePlane y;
    MutablePlane u;
    MutablePlane v;
    uint32_t width;
    uint32_t height;
};

struct Rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct MixSource {
    I420ImageView image;
    Rect destination;
    std::string display_name;
    FillMode fill_mode;
    bool is_highlight;
};

struct I420Color {
    uint8_t y;
    uint8_t u;
    uint8_t v;
};

struct MixOutput {
    MutableI420ImageView image;
    I420Color background_color;
};

struct MixYuvConfig {
    std::string font_path;
    uint32_t font_face_index;
    uint32_t font_size;
    uint32_t osd_left;
    uint32_t osd_bottom;
};
```

输入和输出都解释为 limited-range BT.601 I420。调用方分别提供 Y、U、V plane、
正 stride 和可读或可写容量，不要求 tightly-packed。配置初始化辅助函数和调用方
初始化 `MixOutput` 时采用以下默认值：

```text
font_face_index = 0
font_size = 24
osd_left = 12
osd_bottom = 12
background_color = {Y=16, U=128, V=128}
```

`font_path` 必须非空且指向 FreeType 可以打开的字体文件。`font_size` 的合法范围
为 1 到 512 像素。OSD 边距使用输出像素，不随 source 缩放。以上字段在进入
`Create` 或 `MixYuv` 前必须已经显式初始化；接口不把零值解释为“使用默认值”。

### 2.2 Context 生命周期与合流入口

```cpp
class MixYuvContext {
public:
    static MixYuvStatus Create(
        const MixYuvConfig& config,
        std::unique_ptr<MixYuvContext>* context);

    ~MixYuvContext();

private:
    MixYuvContext();
    MixYuvContext(const MixYuvContext&);
    MixYuvContext& operator=(const MixYuvContext&);
};

MixYuvStatus MixYuv(MixYuvContext* context,
                    const MixSource* sources,
                    size_t source_count,
                    MixOutput* output);
```

`MixYuvContext` 持有 FreeType library/face、字形缓存、渲染计划容量和可复用的高亮
mask。字体、字号和 OSD 边距在 context 创建后保持不变。一个 context 同时只能
执行一次 `MixYuv`；并行合流使用不同 context。

`sources == NULL` 只在 `source_count == 0` 时合法。空 source 集合仍执行背景填充，
产生一帧纯背景 I420 输出。

## 3. 调用契约

### 3.1 函数负责校验的条件

`MixYuv` 在修改输出之前校验：

- context 和 output 非 NULL；
- 输入、输出图像宽高均不小于 2，且为正偶数；
- 所有 plane 指针非 NULL，stride 为正且不小于有效行宽；
- plane 容量足以覆盖最后一行的有效像素；
- Rect 的 `x/y/w/h` 都是正偶数；
- Rect 完整位于输出图像内；
- FillMode 是已定义的枚举值；
- DisplayName 为空或为严格合法的 UTF-8；
- 所有几何运算、plane 容量计算和指针偏移均不溢出；
- Cover/Contain 计算得到的裁剪宽高和输出宽高均不小于 2。

plane 最小容量按实际 stride 计算：

```text
required = stride * (rows - 1) + row_bytes
```

Y plane 的 `rows` 和 `row_bytes` 分别为 `height`、`width`；U/V plane 分别为
`height / 2`、`width / 2`。计算使用带溢出检查的 `size_t` 算术。

Rect 边界使用下式校验，不能先执行可能溢出的无符号加法：

```text
x <= output_width  - w
y <= output_height - h
```

参数或容量校验失败时分别返回 `kInvalidArgument` 或 `kBufferTooSmall`，输出保持
调用前内容不变。

### 3.2 调用方负责的条件

以下条件是入口契约，函数不执行运行时检查：

- 任意两个 source 的 destination Rect 不相交；
- 任何输入 plane 都不与任何输出 plane 内存重叠；
- context 没有被另一个线程同时使用；
- 调用期间所有 plane 指针和 source 字符串保持有效；
- 输入已经完成旋转，并已归一化为 limited-range BT.601 I420。

违反入口契约属于调用方错误，函数行为未定义。

## 4. 预处理与分层流水线

每次调用按以下顺序执行：

1. 校验全部参数、图像、plane 和 Rect。
2. 为每个 source 生成不可变的裁剪、缩放和目标区域计划。
3. 严格解码 DisplayName UTF-8，加载缺失字形并准备本轮文字绘制信息。
4. 按需扩展 context 的渲染计划容量、高亮 mask 和字形缓存。
5. 使用 `background_color` 填充输出 Y/U/V 的全部有效像素。
6. 按 source 数组顺序完成所有 source 图像的裁剪、缩放和不透明合入。
7. 计算全部 `is_highlight == true` source 的边框 mask 并绘制高亮。
8. 按 source 数组顺序绘制全部非空 DisplayName OSD。

步骤 1 至 4 是预处理阶段。所有可能的参数、字体和内存准备错误都必须在步骤 5
开始写输出之前返回。输出 plane 的行 padding 不属于有效像素，背景填充和后续
绘制都不得修改 padding。

图像、高亮和 OSD 分层绘制保证 half-outside 边框不会被后续 source 图像覆盖，
同时保证昵称始终位于边框之上。

## 5. Contain、Cover 与 libyuv 合流

### 5.1 通用计算规则

设 source 尺寸为 `sw x sh`，目标 Rect 尺寸为 `dw x dh`。所有比例比较使用带
溢出检查的 64 位整数交叉相乘，不使用浮点数。所有比例计算产生的裁剪尺寸、
输出尺寸和居中偏移使用：

```text
FloorEven(value) = floor(value / 2) * 2
```

目标 Rect 自身已由入口校验保证为正偶数。

当 `sw < dw && sh < dh` 时，Contain 和 Cover 都禁止放大：source 保持原尺寸，
并按偶数偏移居中到 Rect。只有一个源维度小于目标时仍执行正常的 Contain 或
Cover 规则。

### 5.2 Contain

Contain 不裁剪 source，使用两个方向中较小的缩放比例完整放入 Rect：

```text
scale = min(dw / sw, dh / sh)
```

实际计算通过整数比例比较选择受限方向。输出宽高使用 FloorEven，随后以
FloorEven 偏移居中。因偶数取整留下的额外像素位于 Rect 右侧或底部；未覆盖区域
保持输出背景色。

### 5.3 Cover

Cover 从 source 中心裁出与 Rect 宽高比一致的最大偶数矩形，再缩放到整个 Rect：

- source 相对更宽时保持完整高度，计算并裁剪宽度；
- source 相对更高时保持完整宽度，计算并裁剪高度；
- 宽高比相同时不裁剪。

裁剪尺寸和中心偏移使用 FloorEven。若极端宽高比导致计算出的裁剪尺寸小于 2，
预处理返回 `kInvalidArgument`。

### 5.4 libyuv 调用

需要缩放时直接把计划中的 source 裁剪区域写入输出 Rect 内部：

```cpp
libyuv::I420Scale(..., libyuv::kFilterBilinear);
```

无需缩放时使用 `libyuv::I420Copy`。因为 source 和目标的坐标、尺寸均为偶数，
Y/U/V 起始指针分别按完整分辨率和二分之一分辨率偏移。合流过程不创建每 source
的完整临时 I420 帧。

任一 libyuv 调用意外返回非零值时停止绘制并返回 `kLibyuvError`。此错误发生在
输出写入开始之后，因此输出可能只完成部分合成。

## 6. 高亮边框

每个 `is_highlight == true` 的 source 都绘制高亮；`MixYuv` 不限制一帧的高亮
数量。Active Speaker 只能有一个等业务约束由上层调用方负责。

高亮颜色固定为 sRGB `#00E676`，对应 limited-range BT.601 I420：

```text
Y = 143
U = 113
V = 35
```

每个 source 根据自己的 Rect 高度计算边框：

```text
border_width = max(2, (3 * rect.h + 125) / 250)
inside_width = (border_width + 1) / 2
outside_width = border_width / 2
```

除法均为正整数除法。偶数边框内外各半；奇数边框多出的一个像素位于 Rect 内侧。
对于半开 Rect `[x, x+w) x [y, y+h)`，边框是下列外矩形减去内矩形：

```text
outer = [x-outside, x+w+outside) x [y-outside, y+h+outside)
inner = [x+inside,  x+w-inside)  x [y+inside,  y+h-inside)
```

外矩形只裁剪到完整输出画布，不裁剪到 source Rect，因此允许延伸到布局间隙。

当本轮至少存在一个高亮 source 时，context 把输出有效区域对应的 8-bit mask
清零，并把所有边框以值 1 写入同一 mask。取边框并集后统一更新 I420，防止多个
相邻边框对同一个 chroma sample 重复混合。

mask 覆盖的 Y 像素直接写入 143。每个 U/V sample 对应 2x2 Y 像素，根据其中
被 mask 覆盖的像素数 `n` 做 coverage blend：

```text
sum = n * chroma_border + (4 - n) * chroma_old
chroma_out = (sum + 2) / 4
```

`n` 的范围为 0 到 4，U 使用 113，V 使用 35。此规则保留奇数边框和
half-outside 几何，不为色度对齐修改边框宽度。

## 7. FreeType 昵称 OSD

### 7.1 UTF-8 与字形

DisplayName 按 Unicode code point 严格解码。必须拒绝截断序列、非法 continuation、
overlong 编码、UTF-16 surrogate 和大于 U+10FFFF 的值。

context 创建时使用 `FT_New_Face(font_path, font_face_index, ...)` 打开唯一字体，并
通过 `FT_Set_Pixel_Sizes` 设置统一字号。文字按 code point 从左到右逐个绘制，
不执行复杂 shaping、双向排版、连字、组合字符定位或额外 kerning。字体缺少字符
时使用该 face 的 glyph index 0，即 `.notdef` 替代字形。

context 按 glyph index 缓存 8-bit coverage bitmap、bitmap left/top、advance 和
尺寸。新字形必须在预处理阶段完成加载与栅格化；绘制输出时不再分配内存或调用
可能改变 face 状态的操作。

### 7.2 定位与裁剪

每个昵称的水平 pen 起点为：

```text
pen_x = rect.x + osd_left
```

垂直位置使用当前 FreeType size 的 ascender/descender 指标。把 descender 向远离
零的方向转换为输出像素后，基线定义为：

```text
baseline_y = rect.y + rect.h - osd_bottom - abs(descender_px)
```

每个 glyph bitmap 根据 `bitmap_left` 和 `bitmap_top` 放置，pen 按 FreeType 的
26.6 advance 四舍五入到整数像素后前进。所有中间坐标使用有符号 64 位整数，
防止无符号下溢。

OSD 只裁剪到所属 source Rect。边距超过 Rect、文字过宽或 glyph 超出上下边界时，
允许部分或全部文字不可见，不返回错误。字号和边距以输出像素计，不随 source
缩放。由于 OSD 在 Cover 和高亮之后绘制，Cover 不会裁掉昵称，昵称允许覆盖
高亮边框的内侧部分。

### 7.3 Y plane 混合

OSD 不绘制背景框，只把 glyph coverage alpha 混合到输出 Y plane，白色目标值
为 235，不修改 U/V：

```text
Y_out = (coverage * 235 + (255 - coverage) * Y_old + 127) / 255
```

空 DisplayName 不生成字形信息，也不执行任何 OSD 绘制。

## 8. 错误与异常安全

状态码含义如下：

- `kOk`：背景、全部 source、高亮和 OSD 均完成；
- `kInvalidArgument`：指针、尺寸、stride、Rect、FillMode、UTF-8 或几何非法；
- `kBufferTooSmall`：任一输入或输出 plane 容量不足；
- `kOutOfMemory`：context 创建或预处理阶段内存分配失败；
- `kFontError`：FreeType 初始化、face 创建、字号设置、glyph 加载或栅格化失败；
- `kLibyuvError`：绘制阶段的 libyuv 操作失败；
- `kInternalError`：未归类的内部异常或内部不变量失败。

公共入口不允许 C++ 异常越过函数边界。`std::bad_alloc` 转换为 `kOutOfMemory`，
`std::length_error` 也按无法准备所需容量转换为 `kOutOfMemory`，其他意外异常转换
为 `kInternalError`。参数、容量、字体或预处理失败时输出保持不变；写输出开始后
返回 `kLibyuvError` 或 `kInternalError` 时，输出内容不保证完整。实现内部记录
`output_started`，使异常处理可以保留这一明确语义。

source plane 只读，函数不得修改其有效像素或 padding。输出只修改各 plane 的
有效行宽，不修改行 padding。

## 9. 模块边界

建议实现拆分为：

- `mix_yuv.h`：数据结构、状态码、context 生命周期和 `MixYuv` 声明；
- `mix_yuv.cc`：预检、渲染计划和分层流水线调度；
- `i420_geometry.h/.cc`：Contain/Cover、FloorEven 和裁剪/居中计算；
- `i420_highlight.h/.cc`：边框 mask、Y 写入和 U/V coverage blend；
- `freetype_osd.h/.cc`：FreeType 生命周期、UTF-8、字形缓存、排版和 Y 混合。

几何模块不依赖 FreeType，OSD 模块不负责 source 缩放，高亮模块不读取业务层的
Active Speaker 状态。`mix_yuv.cc` 只组合这些独立能力。

## 10. 性能与资源复用

- source 图像通过 libyuv 直接写入输出 Rect，不分配 full-frame source 临时帧；
- 高亮 mask 每个 luma 像素使用 1 byte，仅在存在高亮时清零和扫描；
- context 按已见最大输出尺寸复用高亮 mask，输出变小时不缩容；
- 渲染计划数组按已见最大 source 数量保留容量；
- 字形按 glyph index 缓存，context 销毁时统一释放；
- 同一输出尺寸、source 数量和字符集合稳定后，正常帧合流不发生动态内存分配；
- 时间复杂度由背景填充、实际缩放像素、高亮 mask 和可见 glyph 像素线性决定。

字形缓存首版不设置淘汰上限。context 只服务同一套固定字体配置，调用方应按实际
业务生命周期销毁不再使用的 context。

## 11. 测试与验收

### 11.1 参数与缓冲

- NULL context/output/source 组合和 `source_count == 0`；
- 输入、输出的 0、1、奇数宽高和最小 2x2 偶数尺寸；
- NULL plane、负数/零/过小 stride、短 plane 和容量计算溢出；
- 非 tightly-packed stride，确认有效像素正确且输入、输出 padding 不变；
- Rect 的奇数、零尺寸、越界和无符号边界值；
- 非法 FillMode、非法 UTF-8 和极端比例产生小于 2 的派生尺寸；
- 所有预处理错误返回时输出逐字节保持不变。

### 11.2 背景、缩放与合流

- 空 sources 输出自定义纯背景；
- 逐 plane 验证不同 `{Y,U,V}` 背景色；
- Contain 的横向/纵向受限、偶数居中和右侧/底部取整余量；
- Cover 的横向/纵向中心裁剪、相同比例和 FloorEven；
- source 两个维度都较小时不放大并居中；
- 只有一个维度较小时仍按正常模式处理；
- 无缩放路径使用 I420Copy，缩放路径使用 bilinear；
- 多 source 合流以及每个 Rect 外像素保持背景色；
- libyuv 错误传播。

### 11.3 高亮

- 无高亮、单高亮和多高亮；
- `rect.h` 对应边框宽度的固定样例：
  `162->2`、`242->3`、`558->7`、`720->9`、`838->10`、`1080->13`；
- 偶数宽度内外均分，奇数宽度多一个像素在内侧；
- Rect 位于画布四边和四角时的 outside 裁剪；
- 相邻边框 mask 取并集，不对同一 chroma sample 重复 blend；
- Y 固定值 143，以及 U/V 在 `n=0..4` 下的 coverage blend 舍入。

### 11.4 OSD

- ASCII、多字节 UTF-8、空名称、无效 UTF-8 和缺字 `.notdef`；
- glyph bearing、ascender、descender 和 advance 的确定性定位；
- OSD 左/下边距、Rect 四边裁剪和完全不可见场景；
- coverage 为 0、1、127、128、254、255 时的 Y 混合舍入；
- OSD 不修改 U/V；
- Cover 后名称仍完整按 Rect 裁剪，且 OSD 位于高亮之上；
- 字形缓存复用和 context 销毁后的 FreeType 资源释放。

### 11.5 稳定性与性能

- 1280x720 和 1920x1080 的重复合流；
- 输出尺寸增大、减小和再次增大时的 mask 复用；
- source 数量和字符集合稳定后的零动态分配检查；
- 输入/输出 guard bytes 检查；
- AddressSanitizer 和 UndefinedBehaviorSanitizer；
- 多 context 并行调用，以及单 context 串行重复调用。

验收以输出像素和状态码的确定性为准。对缩放结果使用固定 libyuv 版本生成的
golden I420 数据或逐 plane hash；对边框、背景和 OSD 的非缩放区域使用精确像素
断言。

## 12. 平台与第三方依赖交付

首版支持以下目标：

- macOS x86_64；
- macOS arm64；
- Linux x86_64。

libyuv 和 FreeType 默认以平台专属静态库引入，YuvMix 自身仍可由上层构建为静态
库或动态库。不同操作系统或架构不得共用同一份二进制依赖。依赖包使用以下布局：

```text
third_party/prebuilt/
  macos-x86_64/{include,lib}/
  macos-arm64/{include,lib}/
  linux-x86_64/{include,lib}/
```

CMake 提供：

```text
YUVMIX_DEPS_ROOT=<平台依赖包根目录>
YUVMIX_LINK_DEPS_STATIC=ON
```

未显式设置 `YUVMIX_DEPS_ROOT` 时，默认选择仓库内与
`CMAKE_SYSTEM_NAME/CMAKE_SYSTEM_PROCESSOR` 匹配的目录。找不到受支持的平台映射、
头文件、`libyuv.a` 或 `libfreetype.a` 时配置立即失败，不静默链接其他架构或动态
库。开发环境可显式关闭 `YUVMIX_LINK_DEPS_STATIC` 后使用系统 CMake package，
但发布产物必须使用固定版本的静态依赖包。

Linux 静态库必须以 `-fPIC` 构建，使其可链接进 YuvMix 动态库。macOS 若需要单一
交付件，可由相同源码和配置分别构建 x86_64、arm64 后合成为 Universal 2；原始
单架构产物仍需保留用于校验。libyuv 的 C++ ABI 必须与 YuvMix 使用的 C++ runtime
兼容。FreeType 构建关闭未使用的可选压缩/图片依赖，或把其全部静态传递依赖一并
列入包清单。

每个平台依赖包必须包含版本、源码 revision、编译器、最低系统版本、编译选项、
架构和许可证清单。CI 分别配置三个目标，至少完成链接验证；可运行目标还要执行
全部 MixYuv 测试。
