# YuvMix 设计规格

日期：2026-07-28
更新：2026-07-30（统一 Video 选流优先级并保证 Active Speaker 优先可见）
状态：待用户最终审阅

## 1. 项目目标

YuvMix 是一个运行在 macOS 和 Linux 上的实时媒体处理库，用于合成原始
I420 视频/内容帧以及有符号 PCM 音频。内部使用 C++11、libyuv 和
FreeType 实现，对外只安装和暴露纯 C ABI。

本库按 Conference（会议）和 UserAgent（UA）组织媒体，为每个非 RTC UA
生成独立的视频、内容和音频输出。视频源和内容源会叠加对应 UA 的 UTF-8
显示名称。本库不负责编解码、RTP/网络传输、音频重采样或媒体时间戳同步。

RTC 音频输入必须是只包含 RTC 原生参与者的聚合音频，且必须排除所有
SIP/H323/VoLTE 桥接终端的上行音频。该约束由提供 RTC mix 的业务层保证；
YuvMix 不从聚合 RTC PCM 中识别或消除非 RTC 接收方自身，因此不能修复包含
桥接终端上行的 RTC mix。

## 2. 首版范围

首版包含：

- 通过 UTF-8 Conference ID 区分多个会议。
- 通过 Conference 内生命周期不变的 UTF-8 UA ID 区分 UA。
- H323、SIP、VoLTE 和 RTC 四种 UA 类型。
- 每个 UA 分别保存 Video 和 Content I420 源。
- GalleryView、FullScreen、ActiveTop、ActiveBottom 四种视频布局。
- 1280x720 和 1920x1080 两种视频输出尺寸。
- 按接收方分别处理单流、双流逻辑。
- 每个 UA 预建独立的 Video/Screen 合流结构，像素内存在首次实际合流时分配。
- Video 支持 receiver 级 cover/contain 填充模式，Screen 固定使用 contain。
- 每个 UA 可配置 I420 三分量背景填充颜色，默认为 limited-range 黑色。
- 使用 FreeType 生成名称遮罩，只叠加到 I420 的 Y 平面。
- Conference 级唯一 RTC 音频源，以及每个非 RTC UA 的独立音频源。
- 每 20 ms 为每个非 RTC UA 处理一个接收方专属 PCM 混音时间片。
- 每个音频源使用 1 秒共享 ring buffer，每个接收方使用独立读游标。
- 基于归一化语音能量、候选保持时间和当前发言人释放时间自动选择 Active
  Speaker。
- 按 Spotlight、Pinned、Active Speaker、入会顺序选择 Video，并对当前可见的
  Active Speaker Video cell 绘制绿色高亮边框。
- 音频源使用 60 ms 重新缓冲门槛、默认 200 ms 可配置高水位，以及默认
  200 ms 可配置最大补偿长度。
- 每个 Conference 使用一个条件变量驱动的处理线程。
- 每个 Engine 可配置 Conference 数量，上限为 32。
- 每个 Conference 最多包含 32 个 UA。
- 通过纯 C 回调异步输出。
- 静态库、动态库、安装配置、测试、纯 C 示例、Sanitizer 和性能基准。

首版不包含 Go/cgo、编解码器、RTP、音频重采样、自动增益、回声消除、
降噪、任意视频输出尺寸或 Windows 支持。

一个 Conference 一个线程是首版明确采用的简化模型。功能上允许多个非 RTC
接收方，但 30 ms 性能目标只覆盖“每个 Conference 一个非 RTC 输出腿”的目标
场景；同一 Conference 存在多个个性化接收方时按顺序处理，不承诺相同延迟。

## 3. 构建与发布

- 语言标准：C++11，关闭编译器扩展。
- 构建系统：CMake 3.20 或更高版本，测试使用 CTest。
- 平台：macOS、Linux。
- 依赖：libyuv 用于 I420 缩放/复制，FreeType 用于文字渲染；配置字体缺少字形时，
  Linux 通过 Fontconfig、macOS 通过 CoreText 查找系统回退字体，再由 FreeType
  栅格化回退字形。
- 产物：YuvMix 静态库和动态库。
- 安装的公共头文件：`include/yuvmix/yuvmix.h`。
- 安装 CMake package 文件，下游 C/C++ 项目可通过
  `find_package(yuvmix)` 使用。
- 默认隐藏动态库符号，只导出 `yuvmix_*` C API。
- CMake 优先查找系统依赖；可通过选项启用 FetchContent，并使用仓库内
  固定的依赖版本。
- 只使用 C++11 能力，不使用 `std::optional`、`std::shared_mutex`、
  `std::jthread`、结构化绑定、泛型 Lambda 或 `std::filesystem`。

建议最低编译器基线为 GCC 7、Clang 7、Apple Clang 10。Sanitizer 任务使用
当前稳定编译器。

## 4. 公共 C ABI

### 4.1 ABI 约定

- Engine 通过不透明句柄 `yuvmix_engine_t` 暴露。
- 公共结构体都是 POD。可能在后续 ABI 扩展的结构体以 `struct_size`
  开头。
- 公共枚举字段使用 `int32_t` 类型和命名常量，避免依赖编译器的 C enum
  大小。
- ID 和 DisplayName 均为 UTF-8 `const char *` 输入。实际用于标识或查找对象的
  Conference ID 和 UA ID 必须非 NULL 且非空；RTC Audio Push 明确忽略的
  `ua_id` 除外。需要跨调用保存时由库内部复制。非 NULL DisplayName 必须是合法
  UTF-8，调用方在进入本库前保证该前置条件，首版本库不重复校验非法 UTF-8。
- 字节数和像素数使用明确的整数类型。
- C++ 异常不得越过 C ABI 边界。
- 已发布结构体的字段只能在尾部追加，不能插入、删除、重排或改变已有字段
  类型。
- 所有公共函数使用 `YUVMIX_API` 导出宏，并在 C++ 下使用 `extern "C"` C
  linkage。

当前规格对应尚未对外发布的首版 ABI，版本固定为 1.0；本规格中的完整公共类型、
函数和各 `*_V1_SIZE` 共同构成首次发布的 V1 基线，不属于已发布 ABI 上的增量
扩展。首次发布后新增公共函数或在结构体尾部追加字段时，必须提升 ABI minor，
并保留已经发布的 V1 大小常量及其结构边界。

公共类型定义如下：

```c
#include <stddef.h>
#include <stdint.h>

#define YUVMIX_ABI_VERSION_MAJOR 1u
#define YUVMIX_ABI_VERSION_MINOR 0u
#define YUVMIX_ABI_VERSION \
    ((YUVMIX_ABI_VERSION_MAJOR << 16) | YUVMIX_ABI_VERSION_MINOR)

#if defined(_WIN32)
#  if defined(YUVMIX_BUILDING_LIBRARY)
#    define YUVMIX_API __declspec(dllexport)
#  else
#    define YUVMIX_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define YUVMIX_API __attribute__((visibility("default")))
#else
#  define YUVMIX_API
#endif

typedef struct yuvmix_engine yuvmix_engine_t;

typedef int32_t yuvmix_status_t;
enum {
    YUVMIX_OK = 0,
    YUVMIX_INVALID_ARGUMENT = 1,
    YUVMIX_NOT_FOUND = 2,
    YUVMIX_ALREADY_EXISTS = 3,
    YUVMIX_OUT_OF_MEMORY = 4,
    YUVMIX_INTERNAL_ERROR = 5,
    YUVMIX_ENGINE_CLOSED = 6,
    YUVMIX_WOULD_DEADLOCK = 7,
    YUVMIX_RESOURCE_LIMIT = 8,
    YUVMIX_AUDIO_FORMAT_NOT_SET = 9,
    YUVMIX_AUDIO_FORMAT_MISMATCH = 10
};

typedef int32_t yuvmix_frame_type_t;
enum {
    YUVMIX_VIDEO = 0,
    YUVMIX_CONTENT = 1
};

typedef int32_t yuvmix_layout_mode_t;
enum {
    YUVMIX_GALLERY_VIEW = 0,
    YUVMIX_ACTIVE_TOP = 1,
    YUVMIX_ACTIVE_BOTTOM = 2,
    YUVMIX_FULL_SCREEN = 3
};

typedef int32_t yuvmix_ua_type_t;
enum {
    YUVMIX_UA_H323 = 0,
    YUVMIX_UA_SIP = 1,
    YUVMIX_UA_VOLTE = 2,
    YUVMIX_UA_RTC = 3
};

typedef int32_t yuvmix_fill_mode_t;
enum {
    YUVMIX_FILL_COVER = 0,
    YUVMIX_FILL_CONTAIN = 1
};

typedef struct {
    int32_t width;
    int32_t height;
} yuvmix_size_t;

typedef struct {
    uint8_t y;
    uint8_t u;
    uint8_t v;
} yuvmix_i420_color_t;

typedef struct {
    uint32_t struct_size;
    int32_t width;
    int32_t height;
    yuvmix_frame_type_t type;
    const uint8_t *y_plane;
    int32_t y_stride;
    size_t y_plane_size;
    const uint8_t *u_plane;
    int32_t u_stride;
    size_t u_plane_size;
    const uint8_t *v_plane;
    int32_t v_stride;
    size_t v_plane_size;
} yuvmix_i420_frame_t;

typedef void (*yuvmix_output_callback_t)(
    void *userdata,
    const char *conference_id,
    const char *receiver_ua_id,
    const yuvmix_i420_frame_t *frame);

typedef struct {
    uint32_t struct_size;
    int32_t sample_rate;
    int32_t channels;
    int32_t bits_per_sample;
} yuvmix_audio_format_t;

typedef struct {
    uint32_t struct_size;
    int32_t sample_rate;
    int32_t channels;
    int32_t bits_per_sample;
    yuvmix_ua_type_t ua_type;
    const uint8_t *buffer;
    size_t buffer_size;
} yuvmix_audio_frame_t;

typedef void (*yuvmix_audio_output_callback_t)(
    void *userdata,
    const char *conference_id,
    const char *receiver_ua_id,
    const yuvmix_audio_frame_t *audio);

typedef int32_t yuvmix_log_level_t;
enum {
    YUVMIX_LOG_ERROR = 0,
    YUVMIX_LOG_WARNING = 1,
    YUVMIX_LOG_INFO = 2,
    YUVMIX_LOG_DEBUG = 3
};

typedef void (*yuvmix_log_callback_t)(
    void *userdata,
    yuvmix_log_level_t level,
    const char *message);

typedef struct {
    uint32_t struct_size;
    const char *font_path;
    uint32_t max_conferences;
    yuvmix_log_callback_t log_callback;
    void *log_userdata;
} yuvmix_engine_config_t;

typedef struct {
    uint32_t struct_size;
    yuvmix_layout_mode_t layout;
    yuvmix_size_t composed_size;
    int32_t composed_video_fps;
    int32_t composed_content_fps;
    uint32_t energy_update_interval_ms;
    yuvmix_output_callback_t output_callback;
    void *output_userdata;
    yuvmix_audio_output_callback_t audio_output_callback;
    void *audio_output_userdata;
    uint32_t audio_high_watermark_ms;
    uint32_t max_audio_compensation_ms;
} yuvmix_conference_config_t;

typedef struct {
    uint32_t struct_size;
    yuvmix_ua_type_t ua_type;
    int32_t supports_dual_stream;
    int32_t has_layout_override;
    yuvmix_layout_mode_t layout;
    yuvmix_size_t composed_size;
    const char *display_name;
    int32_t font_size;
    int32_t osd_left;
    int32_t osd_bottom;
    yuvmix_fill_mode_t fill_mode;
    yuvmix_i420_color_t fill_color;
} yuvmix_ua_config_t;

#define YUVMIX_I420_FRAME_V1_SIZE \
    (offsetof(yuvmix_i420_frame_t, v_plane_size) + \
     sizeof(((yuvmix_i420_frame_t *)0)->v_plane_size))
#define YUVMIX_AUDIO_FORMAT_V1_SIZE \
    (offsetof(yuvmix_audio_format_t, bits_per_sample) + \
     sizeof(((yuvmix_audio_format_t *)0)->bits_per_sample))
#define YUVMIX_AUDIO_FRAME_V1_SIZE \
    (offsetof(yuvmix_audio_frame_t, buffer_size) + \
     sizeof(((yuvmix_audio_frame_t *)0)->buffer_size))
#define YUVMIX_ENGINE_CONFIG_V1_SIZE \
    (offsetof(yuvmix_engine_config_t, log_userdata) + \
     sizeof(((yuvmix_engine_config_t *)0)->log_userdata))
#define YUVMIX_CONFERENCE_CONFIG_V1_SIZE \
    (offsetof(yuvmix_conference_config_t, max_audio_compensation_ms) + \
     sizeof(((yuvmix_conference_config_t *)0)->max_audio_compensation_ms))
#define YUVMIX_UA_CONFIG_V1_SIZE \
    (offsetof(yuvmix_ua_config_t, fill_color) + \
     sizeof(((yuvmix_ua_config_t *)0)->fill_color))
```

公共头文件在类型和函数声明外使用标准的 `#ifdef __cplusplus` / `extern "C"`
保护。所有当前结构体版本大小都必须通过 C++11 `static_assert` 或 C11
`_Static_assert` 验证不超过 `UINT32_MAX`；不支持编译期断言的旧 C 模式由构建
测试执行同一检查。

### 4.2 配置对象

Engine 配置包含：

- `struct_size`。
- 必填字体文件路径（`.ttf` 或 `.ttc`）。
- Conference 数量上限；0 表示默认值 32。
- 可选日志回调及其 userdata。

`max_conferences` 允许 0 或 1 到 32；0 使用默认值 32。超过已配置限制时创建
Conference 返回 `YUVMIX_RESOURCE_LIMIT`。

每个 Conference 固定最多包含 32 个 UA，RTC 和非 RTC UA 一并计数。达到上限时
`yuvmix_ua_add` 返回 `YUVMIX_RESOURCE_LIMIT` 且不修改状态；UA remove 返回后
释放对应名额。

Conference 配置包含：

- `struct_size`。
- 默认布局，初始为 GalleryView。
- 默认合流尺寸，初始为 1280x720。
- Video FPS，初始为 25。
- Content FPS，初始为 5。
- 音量能量统计窗口，默认 3000 ms。
- 必填且非 NULL 的 Video/Content 输出回调及 userdata。
- 必填且非 NULL 的独立 Audio 输出回调及 userdata。
- 音频读游标高水位，默认 200 ms。
- 单次 Audio 调度补偿上限，默认 200 ms。

两个 FPS 的合法范围都是 1 到 60。视频输出尺寸只接受 1280x720 或
1920x1080。`energy_update_interval_ms` 合法范围为 100 到 10000 ms。
`audio_high_watermark_ms` 必须是 20 ms 的整数倍，合法范围为 60 到 1000 ms。
`max_audio_compensation_ms` 也必须是 20 ms 的整数倍，合法范围为 20 到
1000 ms。两个字段相互独立：前者限制单个源对单个接收方的排队延迟，后者限制
一次补偿处理和回调的长度。创建 Conference 时两个输出回调都不得为 NULL；
任一为空都返回 `YUVMIX_INVALID_ARGUMENT`，不创建 Conference。

UA 配置包含：

- `struct_size`。
- UA 类型。
- 是否支持双流。
- 可选布局/输出尺寸覆盖配置。
- 初始 DisplayName。
- 字号，默认 24 像素。
- OSD 左边距和底边距，默认均为 12 像素。
- Video 源填充模式，默认为 `YUVMIX_FILL_COVER`。
- Video/Screen 共用的 I420 背景填充颜色，默认为 limited-range 黑色
  `{16, 128, 128}`。

输出尺寸 `{-1, -1}` 表示继承 Conference；只有一个维度为负数时参数非法。
布局继承使用明确的 `has_layout_override` 字段，不使用特殊枚举值。UA 的
`composed_size` 只控制 Video 输出；Screen 是双流 Content 输出，首版固定为
1920x1080。

### 4.3 公共函数

首版公共头文件暴露以下确定的函数签名，所有声明都带 `YUVMIX_API`。init 函数
显式接收调用方实际分配的结构体大小，只初始化调用方容量与当前库已知结构体
大小的交集，并把实际初始化的前缀长度写入 `struct_size`。这样旧调用方加载
新版动态库时不会被新版结构体大小越界写入，新调用方加载旧版动态库时也不会
把旧库未知且未初始化的尾字段标记为有效。

init 容量小于 `sizeof(uint32_t)` 或大于 `UINT32_MAX` 时返回
`YUVMIX_INVALID_ARGUMENT`，且不写目标内存。库读取配置结构时只读取
`struct_size` 覆盖的已知前缀，缺失尾字段使用当前版本默认值；配置必填字段不在
有效前缀内时仍返回非法参数。`yuvmix_audio_format_init` 默认写入 48000 Hz、
单声道、16-bit；`yuvmix_conference_config_init` 默认写入 200 ms Audio 高水位
和 200 ms 最大 Audio 补偿长度；`yuvmix_ua_config_init` 默认写入 12 像素 OSD
左/下边距、cover 模式和 limited-range I420 黑色。

I420 输入的 `struct_size` 至少为 `YUVMIX_I420_FRAME_V1_SIZE`，Audio 格式输入
至少为 `YUVMIX_AUDIO_FORMAT_V1_SIZE`，Audio 帧输入至少为
`YUVMIX_AUDIO_FRAME_V1_SIZE`。回调输出分别把 `struct_size` 填为对应的 v1
大小。超过库已知结构体大小的输入尾部一律忽略。

```c
YUVMIX_API yuvmix_status_t yuvmix_engine_config_init(
    yuvmix_engine_config_t *config,
    size_t config_size);

YUVMIX_API yuvmix_status_t yuvmix_conference_config_init(
    yuvmix_conference_config_t *config,
    size_t config_size);

YUVMIX_API yuvmix_status_t yuvmix_ua_config_init(
    yuvmix_ua_config_t *config,
    size_t config_size);

YUVMIX_API yuvmix_status_t yuvmix_audio_format_init(
    yuvmix_audio_format_t *format,
    size_t format_size);

YUVMIX_API yuvmix_status_t yuvmix_engine_create(
    const yuvmix_engine_config_t *config,
    yuvmix_engine_t **out_engine);

YUVMIX_API yuvmix_status_t yuvmix_engine_close(yuvmix_engine_t *engine);
YUVMIX_API yuvmix_status_t yuvmix_engine_destroy(yuvmix_engine_t *engine);

YUVMIX_API yuvmix_status_t yuvmix_conference_create(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const yuvmix_conference_config_t *config);

YUVMIX_API yuvmix_status_t yuvmix_conference_remove(
    yuvmix_engine_t *engine,
    const char *conference_id);

YUVMIX_API yuvmix_status_t yuvmix_conference_set_defaults(
    yuvmix_engine_t *engine,
    const char *conference_id,
    yuvmix_layout_mode_t layout,
    yuvmix_size_t composed_size,
    int32_t composed_video_fps,
    int32_t composed_content_fps);

YUVMIX_API yuvmix_status_t yuvmix_conference_set_spotlight_ua(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *spotlight_ua_id);

YUVMIX_API yuvmix_status_t yuvmix_conference_set_audio_format(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const yuvmix_audio_format_t *format);

YUVMIX_API yuvmix_status_t yuvmix_ua_add(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    const yuvmix_ua_config_t *config);

YUVMIX_API yuvmix_status_t yuvmix_ua_remove(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id);

YUVMIX_API yuvmix_status_t yuvmix_ua_set_display_name(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    const char *display_name,
    int32_t font_size,
    int32_t osd_left,
    int32_t osd_bottom);

YUVMIX_API yuvmix_status_t yuvmix_ua_set_layout(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    int32_t has_layout_override,
    yuvmix_layout_mode_t layout,
    yuvmix_size_t composed_size);

YUVMIX_API yuvmix_status_t yuvmix_ua_set_pinned_ua(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *receiver_ua_id,
    const char *pinned_ua_id);

YUVMIX_API yuvmix_status_t yuvmix_ua_set_fill_mode(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    yuvmix_fill_mode_t fill_mode);

YUVMIX_API yuvmix_status_t yuvmix_ua_set_fill_color(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    yuvmix_i420_color_t fill_color);

YUVMIX_API yuvmix_status_t yuvmix_ua_set_audio_volume_energy(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    float audio_volume_energy);

YUVMIX_API yuvmix_status_t yuvmix_add_yuv(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    const yuvmix_i420_frame_t *frame);

YUVMIX_API yuvmix_status_t yuvmix_clear_yuv(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    yuvmix_frame_type_t type);

YUVMIX_API yuvmix_status_t yuvmix_push_audio(
    yuvmix_engine_t *engine,
    const char *conference_id,
    const char *ua_id,
    const yuvmix_audio_frame_t *audio);

YUVMIX_API const char *yuvmix_status_string(yuvmix_status_t status);
YUVMIX_API uint32_t yuvmix_get_abi_version(void);
```

`spotlight_ua_id` 为 NULL 或空字符串时清除 Conference Spotlight；非空值必须
引用 Conference 内已存在的 UA。`pinned_ua_id` 为 NULL 或空字符串时清除指定
接收 UA 的 Pinned；非空值必须引用同 Conference 内已存在且不同于接收方自身的
UA。设置只保存 UA ID，不要求目标当时已有 Video；选流时再检查 Video 是否
有效。Conference 的
Video/Content 和 Audio 回调及 userdata 在 Conference 生命周期内不允许替换，
避免已捕获旧 userdata 的回调与替换操作产生竞态；如需替换，应删除并重新创建
Conference。

创建、查找或删除 Conference/UA 时，`conference_id` 和 `ua_id` 均不得为 NULL
或空字符串；违反时返回 `YUVMIX_INVALID_ARGUMENT`，不修改状态。仅
`spotlight_ua_id`、`pinned_ua_id` 和 DisplayName 使用 NULL/空字符串的特殊清除
或空名称语义。

UA 创建配置或 `yuvmix_ua_set_display_name` 的 `display_name` 为 NULL 时等同于
空字符串：保存为空名称且不生成 OSD 遮罩。

`yuvmix_clear_yuv` 用于在不删除 UA 的情况下暂停 Video 或 Content 源；后续
重新 Push 即可恢复。清除 Video 后，后续合成不再选择该源；清除活动 Content
后还会取消其尚未发送的 dirty 状态，不再输出 Content，也不再把它加入单流
Video 合成。已经进入业务回调的最后一帧允许完成，clear 返回后不再准入使用
被清理快照的新回调。该操作不发送空帧或 EOS。

`yuvmix_ua_remove` 表示 UA 完整离会，不需要先逐项 clear。调用开始后不再接受
该 UA 的新输入，并删除其 Video、Content、DisplayName、能量、布局和输出状态；
非 RTC UA 的源 ring 及其在其他源上的接收游标也一并删除。UA 不再作为任何
合流源或接收方。若它是当前 Content 源，则清除 Content 和 dirty 状态；若它是
Spotlight，则清除 Conference Spotlight；若它是任意接收 UA 的 Pinned，则清除
所有指向它的 Pinned 引用。RTC UA 删除不清除 Conference 级 RTC 聚合音频。
remove 是同步操作，返回时不再存在涉及该 UA 的在途输出回调。
双流开始、停止、离会和下游展示状态仍由业务层控制消息负责，不属于 YuvMix
回调协议。

`yuvmix_add_yuv` 接收 Engine、Conference ID、UA ID 和三平面 I420 帧。
宽高必须为不小于 16 的正偶数，三个 plane 指针均非 NULL，stride 为正数且满足
`y_stride >= width`、`u_stride/v_stride >= width / 2`。每个 plane 的可读
字节数至少覆盖 `stride * (rows - 1) + row_bytes`，所有乘加都做溢出检查。
首版业务输入还必须避免极端宽高比，保证放入实际目标 cell 时按第 10.2 节计算的
裁剪或缩放宽高均不小于 2；业务确认实际输入满足该前置条件，首版不增加额外的
宽高比校验或零尺寸防御分支。

`yuvmix_push_audio` 接受单声道、有符号 16-bit little-endian PCM。
`audio->ua_type == YUVMIX_UA_RTC` 时忽略 `ua_id`，包括 NULL；
H323/SIP/VoLTE 输入必须提供已存在的 UA ID，且类型必须与 UA 配置一致。

Conference 创建后必须先调用 `yuvmix_conference_set_audio_format` 才能 Push
音频。未设置时返回 `YUVMIX_AUDIO_FORMAT_NOT_SET`；Push 格式不完全一致时返回
`YUVMIX_AUDIO_FORMAT_MISMATCH`，两种错误都不修改 ring、能量或游标。第一次
设置成功后，格式在 Conference 生命周期内不可改变；重复设置完全相同格式
幂等返回 `YUVMIX_OK`，尝试设置不同格式返回
`YUVMIX_AUDIO_FORMAT_MISMATCH` 且不修改任何状态。Audio 输出始终使用该固定
格式，库内不做重采样、声道转换或位深转换。第一次设置成功时，以同一个
`steady_clock::now()` 为所有现有非 RTC 接收 UA 设置 `counts = 0` 和
`last_compensation_time = now`，设置下一 Audio deadline 为 `now + 20 ms`，并
唤醒 Conference 线程。音频格式设置成功之前不调度 Audio deadline、不执行混音。

`yuvmix_get_abi_version` 返回运行时动态库 ABI 版本。主版本不一致，或主版本
相同但运行库 minor 低于调用方所需 minor 时，不得继续创建 Engine。

`yuvmix_ua_set_fill_mode` 只控制该接收 UA 的 Video 源合流；Screen 源始终使用
contain。非法 fill mode 返回 `YUVMIX_INVALID_ARGUMENT`。I420 颜色的三个
`uint8_t` 分量全部合法，`yuvmix_ua_set_fill_color` 同时更新该 UA 的 Video 和
Screen 背景颜色。对 RTC UA 调用两个 setter 均保存设置并返回 `YUVMIX_OK`，但
RTC 不产生 Video/Screen 输出，因此这些设置暂不参与任何合流。RTC UA 创建配置
中的 fill mode/color 按相同规则校验和保存。

所有 setter 都是同步状态更新，不等待下一帧输出。DisplayName 改变时同步重建
FreeType 遮罩，从该 UA 下一次输入帧开始生效。fill mode/color 已被本轮合流
快照捕获时允许该轮完成，新值从下一轮合流开始生效。fill mode/color 更新不把
已有 Content 标记为 dirty，也不主动唤醒或重发静止 Content；若此后没有新的
Content 合流，Screen 上看不到这次设置变化，符合“下一次合流生效”的语义。

## 5. Engine 生命周期与安全关闭

Engine 状态机：

```text
ACTIVE -> CLOSING -> CLOSED -> DESTROYED
```

`yuvmix_engine_close` 是幂等、线程安全的同步停止屏障。第一次调用原子地把状态
切换为 CLOSING，立即关闭所有新任务、setter、删除操作和媒体输入的准入；这些
Engine API 返回 `YUVMIX_ENGINE_CLOSED`，不修改状态。已经在 ACTIVE 状态进入的
公共 API 可以安全结束，close 通过 Engine 级在途 API 计数等待它们退出。

进入 CLOSING 后丢弃尚未开始的媒体工作，不再准入新的 Video、Content、Audio
或日志回调。close 设置全部 Conference 停止标记，取消 timer deadline，唤醒并
join 所有 Conference 线程，同时等待已经准入的媒体和日志回调返回。所有内部
线程、timer 和回调退出后状态变为 CLOSED，close 才返回。返回后库内不再运行
后台工作，也不再访问业务 userdata。并发的重复 close 等待同一次关闭完成并
返回 `YUVMIX_OK`。

同步 close 会等待调用它的 Conference 线程或回调自身，因此不得从同一 Engine
的 Video、Content、Audio 或日志回调调用 close。从这些回调调用 close、destroy、
Conference remove 或 UA remove 时，必须通过线程局部 callback context 在修改
Engine 状态前返回 `YUVMIX_WOULD_DEADLOCK`。

推荐的集成顺序是：应用层停止产生新的 YuvMix 调用，调用 close，确认自身调用
任务已经退出，再按业务需要等待一段宽限时间后调用 destroy。10 秒可以作为客户
侧建议值，但库不依赖固定等待时长。宽限期不能替代“destroy 开始后不再使用
Engine 指针”的调用方约束。

`yuvmix_engine_destroy` 可以对未关闭的 Engine 隐式执行同步 close，但推荐始终
显式 close。close 已经完成在途 API、回调和内部线程的回收；destroy 只释放
Conference、FreeType、回调状态和 Engine 句柄。destroy 只能从非回调线程调用
一次；destroy 开始后不得并发 destroy，也不得再调用任何使用该句柄的 API。

Conference/UA 删除使用删除标记和共享帧/对象快照，保证在途处理不访问已释放
裸指针，并同步等待相关处理完成。Video/Content/Audio 和日志 userdata 至少
保持有效到 close 返回。

## 6. 内部对象模型

### 6.1 Engine

Engine 持有：

- FreeType library 和字体 face 资源。
- 平台字体发现适配和按 Unicode code point 缓存的系统回退 FreeType face。
- 保护共享 FreeType face 的专用字体互斥锁。
- Conference 注册表。
- Engine 状态、公共 API/回调在途计数及关闭同步对象。
- 可选日志回调状态和线程局部回调上下文。

本库不使用进程级全局单例。

### 6.2 Conference

Conference 以 Conference ID 为键，持有：

- 默认布局和输出尺寸。
- Video FPS、Content FPS。
- 首次设置后不可变的 Conference 音频格式。
- 音频读游标高水位，默认 200 ms。
- 最大 Audio 补偿长度，默认 200 ms。
- 音量能量统计窗口长度，默认 3000 ms。
- 可选 Conference 级 Spotlight UA ID。
- 自动 Active Speaker 的当前 UA ID、候选 UA ID、候选开始时间、当前发言人
  最后一次超过阈值的时间，以及相应状态是否有效的标记。状态只保存 UA ID，
  不持有裸 UA 指针。
- Video/Content、Audio 回调及各自 userdata。
- UA 集合和单调递增的加入序号。
- UA 数量固定上限 32，RTC 和非 RTC UA 一并计数。
- 一个处理线程和条件变量。
- 停止标记。
- 下一个 Audio/Video deadline、上一次 Content 调度时刻和 Content 限频状态。
- 下一个能量窗口更新时间。
- 最新 Content 快照、单调递增的 generation，以及是否仍有接收方待投递的
  dirty 状态。
- 一个 Conference 级 RTC 音频 ring。
- 可复用的音频累加和输出缓冲。

### 6.3 UserAgent

UA 以生命周期不变的 UA ID 为键，持有：

- UA 类型：H323、SIP、VoLTE 或 RTC。
- 是否支持双流。
- DisplayName、字号、OSD 边距和缓存的灰度字形遮罩。
- Video fill mode，默认 cover；Video/Screen 共用的 I420 fill color，默认
  `{16, 128, 128}`。
- `0.0f..1.0f` 的已发布归一化 PCM16 均方能量，初始为 `0.0f`。
- RTC setter 的最后更新时间；非 RTC 自动累加器位于对应源 ring 状态中。
- 可选的接收方级 Pinned UA ID。
- 可选接收方布局和输出尺寸覆盖配置。
- 最新 Video、Content 输入的不可变帧快照。
- 该 UA 作为双流接收方时最后成功输出的 Content generation。
- 预建但像素内存延迟分配的接收方专属 Video 和 Screen 合流结构。
- 非 RTC UA 的一个输入音频源 ring。
- 非 RTC 接收方的 Audio `counts` 和上次补偿 `steady_clock` 时间点。

Video 源元数据概念上的默认尺寸为 1280x720，但像素内存在第一次输入帧时
延迟分配，避免未使用内存。Content 初始为 `0x0/NULL`。UA 创建时初始化
Video 合流结构的逻辑默认尺寸为 1280x720，Screen 合流结构为 1920x1080，
两个结构的 plane 初始都为 NULL；仅在第一次实际合流时分配像素内存。

RTC UA 只提供输入，不接收合成 Video、Screen 或混合 Audio 回调，因此其两个
合流结构不会分配像素内存。RTC 音频属于 Conference，不关联任何 RTC UA ID。
RTC UA 的 fill mode/color 仍按配置或 setter 保存，但首版不使用。

## 7. I420 输入与 OSD

每个 UA 分别保存 Video 和 Content 快照。

`yuvmix_add_yuv` 同步执行：

1. 校验 Engine 状态、ID、帧类型、偶数尺寸、三个 plane、stride 和可读长度。
2. 分配或复用私有可写的 tightly-packed I420 缓冲。
3. 按行从 Y/U/V plane 和各自 stride 复制完整 I420 输入；调用返回后不再读取
   调用方 plane。
4. 捕获该次输入对应的 DisplayName、字号、边距和灰度字形遮罩状态，但不在源
   快照上直接绘制 OSD。
5. 把原始 I420 缓冲及其 OSD 状态一起发布为不可变共享快照。
6. Content 输入完成时递增 Conference Content generation，并唤醒 Conference
   线程。

OSD 使用 FreeType 动态生成 8-bit coverage mask。非空 DisplayName 按 UTF-8
解码后的 Unicode code point 顺序从左到右逐个绘制；首版不支持复杂 shaping、
双向排版、连字或组合字符定位。配置字体缺少某个 code point 的字形时，通过
平台字体发现接口查找能覆盖该 code point 的系统回退字体，并缓存对应 FreeType
face；系统回退仍无可用字形时绘制系统回退字体的替代字形。字号默认 24 像素；
遮罩宽度由上述简单排版结果决定，不固定为 48x48。空名称不生成遮罩。左边距和
底边距是 UA 级非负像素值，默认均为 12。全部选中 Video cell 完成
cover/contain 裁剪、缩放和画布合入，并按第 10.3 节完成 Active Speaker 边框后，
再使用各源快照捕获的 OSD 状态把文字绘制到对应 cell 左下角。Active 与非 Active
cell 使用相同的 OSD 规则。字号和边距均以输出 surface 像素计，不随源缩放；
文字在 Y 平面以白色 coverage alpha 混合，不修改 U/V，不绘制背景框，超出目标
区域的部分裁剪。这样 cover 的居中裁剪不会裁掉名称；contain 的 OSD 允许覆盖
目标区域内的填充色部分。

首版把输入解释为 limited-range I420，不做色彩空间或 range 转换；输出黑色为
Y=16/U=128/V=128，OSD 白色目标 Y=235。调用方必须在 Push 前完成旋转和任何
色彩范围归一化。

修改 DisplayName、字号或边距时同步更新 OSD 状态，但不修改旧快照捕获的 OSD
状态，因此仍从该 UA 下一次输入帧开始生效。
所有 FreeType face 操作都必须持有 Engine 专用字体锁；释放字体锁后才能获取
Conference/UA 锁或调用任何业务回调。可写帧缓冲只有在引用计数证明没有在途
快照持有时才能复用，否则从缓冲池取得另一个缓冲或重新分配。

发布 Content 时清空同 Conference 其他所有 UA 的 Content 快照，再把新快照
设为唯一活动 Content。业务层保证只有一路 Content，库内清理规则用于消除
历史残留。

## 8. 每 Conference 线程与并发

每个 Conference 拥有一个处理线程。Conference 成功加入 Engine 注册表后启动
线程。线程启动时捕获一次 `start_time = steady_clock::now()`，并设置
`next_video_deadline = start_time + video_period`；第一个 Video 周期不在启动
瞬间立即执行。线程使用 `std::condition_variable::wait_until` 和
`std::chrono::steady_clock` 休眠，不做空闲轮询。

从非回调业务线程修改 Conference FPS 时唤醒线程，并从修改时刻重新计算未来
deadline。`next_video_deadline` 设置为修改时刻加新 Video 周期；Content 限频
状态保留上一次 Content 调度时刻，并按新 Content 周期重新计算最早可调度时刻。
其他 setter 从下一次处理快照起生效。

音频格式设置成功后，线程每 20 ms 处理 Audio；设置成功之前不调度 Audio。
线程始终按 Conference Video FPS 处理 Video，并按 dirty 限频规则处理 Content：

- Content 从 clean 变为 dirty 时，如果从未调度过 Content，则立即具备调度资格；
  否则最早调度时刻为
  `max(now, last_content_dispatch_time + content_period)`。已经 dirty 时到达的新
  Content 只更新 generation 和最新快照，不推迟既有最早调度时刻。
- Content 调度器进入一次实际处理轮次时，无论各 UA 最终成功、失败或变为不合格，
  都把 `last_content_dispatch_time` 更新为本轮时刻。仍然 dirty 的接收 UA 只能在
  下一个 Content 周期重试。clean 状态不设置周期唤醒；长时间 idle 后的新 Content
  因已经超过限频间隔而立即具备调度资格。
- 新 Content 更新最新快照、递增 generation，并使当前所有合格双流接收 UA 的
  最后成功 generation 落后于当前值，从而设置 dirty。
- 每个合格接收 UA 在一个 Content 周期内最多发送一次。
- 一个周期内多个 Content 输入合并为最新帧。
- 每个合格双流接收 UA 只在其最后成功 generation 小于当前 generation 时参与
  本轮；surface 和合流准备成功后实际调用输出回调，回调正常返回或异常被第
  14 节的边界捕获后，才把该 UA 的最后成功 generation 推进到当前值。
- surface 分配或合流准备失败、未调用该 UA 回调时，不推进它的 generation；
  下一 Content 周期仅为仍落后的 UA 重试，已经成功的 UA 不重复收到同一
  generation。
- 所有当前合格接收 UA 都成功、被删除或变为不合格后清除 dirty。Content 非
  dirty 时不发送回调。

新 UA 加入时把其最后成功 Content generation 初始化为 Conference 当前值，
因此不接收加入前已经发布或仍在其他 UA 重试中的 Content。新 Content 覆盖旧
Content 时，仍落后的 UA 直接以最新 generation 和快照输出，不补发中间帧。
清除活动 Content 或删除 Content 源时清除 dirty；删除接收 UA 时直接移除其
generation 状态，不阻塞其他接收方完成当前 generation。

所有 UA 的选流能量使用相同的归一化 PCM16 均方量纲：

```text
normalized_energy =
    sum(sample * sample) / (sample_count * 32768.0 * 32768.0)
```

该值不是均方根；计算结果钳位到 `0.0f..1.0f`。setter 只接受有限浮点数，传入
NaN、正负无穷或范围外的值时返回 `YUVMIX_INVALID_ARGUMENT`，不修改状态。

Conference 线程按 `energy_update_interval_ms` 结算非 RTC 自动能量。每次格式
校验成功的非 RTC Audio Push 都在对应源 ring 锁内，把 PCM append 和
`sample * sample` 的平方和及 sample 数作为同一次成功输入更新。平方和及 sample
数使用 64-bit 检查/饱和加法，禁止整数回绕。窗口到期时，Conference 线程在源
ring 锁内取走并清空累加器，释放 ring 锁后再在 Conference 状态锁内发布归一化
均方值；无 sample 时发布 `0.0f`。ring 锁和 Conference 锁不嵌套。

对非 RTC UA 调用 `yuvmix_ua_set_audio_volume_energy` 会立即覆盖已发布能量，从
下一次 Video 选流开始生效；到下一个自动统计窗口结束时，无条件由自动结果
替换。Conference 级 RTC PCM 没有 UA ID，无法计算单个 RTC 视频 UA 的能量，
因此 RTC UA 只通过 setter 更新。RTC setter 值立即生效，并使用 `steady_clock`
记录更新时间；在 `now >= last_update + energy_update_interval_ms` 时过期为
`0.0f`。
调度器把最近 RTC 过期时刻作为唤醒 deadline，Video 选流前也检查过期状态。

同一 Conference 内 Audio、Video、Content 串行执行；同时到期时优先级为
Audio、Video、Content。Video 处理跨过一个或多个 deadline 时直接推进到第一
个未来 deadline，不生成追赶帧。Audio 不按调用次数追赶，而是按第 13 节使用
每个接收 UA 的 `steady_clock` 时间轴计算一次合并后的补偿长度。

每轮处理在短 Conference 锁内获取所需共享帧和接收方设置快照，随后释放锁，
再执行缩放、视频合成、音频混合和回调。不同 Conference 并行，同一
Conference 的 Audio/Video/Content 回调不重叠。不同 Conference 的回调可能
并发，因此业务回调仍需线程安全。

持有 Engine、Conference、UA 或音频 ring 锁时不得调用输出回调或日志回调。

`yuvmix_conference_remove` 先标记停止并从新查找中移除 Conference，再唤醒
线程，在 Engine/Conference 锁外 join，最后释放状态。Engine close 对全部
Conference 执行相同停止流程，并在返回前完成 join。

回调应快速返回。慢回调只延迟对应 Conference 的下一处理周期，但该时间计入
端到端调度延迟。

## 9. 视频接收方和选流

只为非 RTC UA 生成输出。接收方永远不接收自己的 Video 或 Content。

候选 Video 源必须已有有效快照，并排除接收方自身。布局的普通候选按 UA 单调
递增的加入序号排列，即先入会者在前，不按能量排序。能量只用于第 9.2 节的
Conference 级 Active Speaker 仲裁。Spotlight 和 Pinned 只保存选择偏好；目标
UA 暂无有效 Video 或恰好是接收方自身时，本轮视为不可用并继续向下一优先级
回退，偏好本身不清除，后续 Video 恢复时自动重新生效。

### 9.1 布局排列和源优先级

所有布局的物理槽位按从左到右、从上到下的顺序渲染。每个接收 UA 独立构造
Video 候选队列，顺序固定为可用的 Conference Spotlight、该接收 UA 的可用
Pinned、Conference Active Speaker，最后是按 UA 加入序号递增的其余候选。
候选必须具有有效 Video 并排除接收方自身；Active Speaker 是接收方自身时不加入
该接收方的队列。Spotlight、Pinned、Active Speaker 或普通候选指向同一 UA 时
只保留其最高优先级位置。

Content 不属于 Video 候选，不占用 Video 槽位，也不与同 UA 的 Video 去重。
跳过无效、重复或接收方自身的 Video 源后，后续候选向前紧凑填充，不保留空槽；
候选耗尽后的剩余槽位保持接收 UA 的填充颜色。

各布局从上述候选队列取源：

- **GalleryView**：依次选择最多 25 路，第一个源进入左上角，之后逐行从左到右、
  从上到下填充。
- **FullScreen**：只选择队列第一路并填满整个 Video surface。因为只有一个
  Video 槽位，Spotlight 或 Pinned 可按既定优先级覆盖 Active Speaker；没有任何
  候选时输出纯填充色帧。
- **ActiveTop/ActiveBottom**：队列第一路进入大画面，之后最多五路依次进入
  小画面。Spotlight 或 Pinned 占据大画面时，未与其重复的 Active Speaker 会在
  普通候选之前优先进入小画面。大画面源选择优先级与物理渲染顺序无关。

因此，除 Active Speaker 是接收方自身、没有有效 Video，或者 FullScreen 的唯一
槽位已被更高优先级 Spotlight/Pinned 占用外，Active Speaker 必须出现在一个
Video cell 中。

ActiveTop 的物理渲染顺序是顶部五个小画面从左到右，然后底部大画面；
ActiveBottom 的顺序是顶部大画面，然后底部五个小画面从左到右。GalleryView
逐行从左到右、从上到下；FullScreen 只有一个槽位。Conference Spotlight 对
所有接收 UA 生效，Pinned 只对持有该设置的接收 UA 生效，Active Speaker 是
Conference 级自动仲裁结果。

四种布局的槽位和优先级示意见 `docs/yuvmix-layout-priority.html`；正式行为仍以
本节文字规则为准。

### 9.2 自动 Active Speaker 仲裁

自动仲裁是 Conference 级状态机，固定参数为：

```text
kEnergyThreshold = 0.3f
kHoldTime = 1500 ms
kReleaseTime = 3000 ms
```

每次能量发布、setter 成功、RTC 能量过期、UA/Video 源增删，以及每次 Video
选流前，都使用同一时刻捕获的 Conference 能量和 Video 可用性快照执行一次
仲裁。先从已有有效 Video 快照的 UA 中找出归一化能量最大的 UA；能量相同时按
UA 加入序号选择最早加入者。该 UA 是本轮唯一的 `max_energy_ua`，不得按 UA
逐个调用仲裁逻辑，以免多个同时超过阈值的 UA 反复重置候选计时。
最近一次已发布的能量在被下一次发布或 RTC 过期替换前视为当前有效状态；本文
的“连续保持”和“持续静默”均按这些离散状态及其 `steady_clock` 时间计算，
不推测两个能量更新点之间的原始音频变化。

仲裁规则如下：

1. `max_energy_ua` 的能量必须严格大于 `0.3f` 才能成为候选。没有合格 UA 时
   清除候选及其开始时间。
2. 合格的 `max_energy_ua` 与候选不同，则把它设为新候选，并用
   `steady_clock::now()` 记录 `candidate_since`。最高能量 UA 发生变化，或其
   能量降到小于等于阈值时，原候选的连续保持时间立即失效。
3. 候选与当前发言人相同时清除候选状态，不进行自切换。候选不是当前发言人，
   且从 `candidate_since` 起连续保持最高能量并严格超过阈值达到至少 1500 ms
   时，把候选切换为当前发言人，同时设置 `last_spoke = now` 及其有效标记，并
   清除候选状态。
4. 当前发言人自身能量严格大于阈值时，更新其 `last_spoke`。当前发言人能量
   小于等于阈值持续达到至少 3000 ms 时，清除当前发言人；下次按相同规则选举。
5. 挑战者满足 1500 ms 保持条件后立即切换，不需要等待原当前发言人先满足
   3000 ms 静默释放条件。ReleaseTime 只用于没有挑战者完成切换时释放沉默的
   当前发言人。
6. 当前发言人或候选 UA 被删除、清除 Video，或不再具有有效 Video 快照时，
   立即清除对该 UA 的对应仲裁状态。首次选举和当前发言人被清除后的再次选举
   都必须重新满足 1500 ms 保持条件。

所有持续时间比较使用 `steady_clock` 和 `>=` 边界。自动仲裁结果是 Conference
级 UA ID；为某个接收方选流时仍必须排除接收方自身。若自动 Active Speaker
正是该接收方或其 Video 已不可用，则仅对该接收方按第 9.1 节继续尝试下一
优先级或普通候选，不修改 Conference 级仲裁状态。

Spotlight 和 Pinned 不参与上述 Hold/Release 仲裁，也不重置或暂停自动仲裁。
它们在 Video 候选队列中排在 Active Speaker 之前；清除或暂时跳过这些显式偏好
后，已经稳定选出的 Conference Active Speaker 会在队列中向前移动。

## 10. Video/Screen 合流、布局与缩放

### 10.1 合流结构与缓冲

每个 UA 创建时预建 `video_mix_surface` 和 `screen_mix_surface`，但不分配 plane
内存。Video 的逻辑默认尺寸为 1280x720，实际目标尺寸取接收 UA 当前有效 Video
输出配置；Screen 就是双流 `YUVMIX_CONTENT` 输出，首版固定为 1920x1080。

每次准备对应输出时：

1. 如果 surface 尚未分配，或已分配宽高与本次目标宽高不同，释放旧像素缓冲，
   按 tightly-packed I420 重新分配并更新宽高、stride 和 plane size。
2. 分配失败时把 surface 保持为空，释放相关锁后记录错误日志，并只跳过该接收
   UA 的本轮对应输出；源快照和其他接收方不受影响。Video 在下一 Video 周期
   重试；Screen 保留该接收 UA 的待投递 generation，在下一 Content 周期重试。
3. 无论缓冲是新分配还是复用，每轮合流前都用接收 UA 的 `fill_color` 填满 Y、
   U、V plane，再向目标区域写入媒体。
4. Video 只写 `video_mix_surface`；双流 Content 只写
   `screen_mix_surface`，不再直接把源 Content 快照作为输出回调。
5. Screen surface 只在支持双流的非 RTC UA 实际需要发送 Content 时分配。不支持
   双流的 UA 把 Content 合入 Video surface，不分配 Screen 像素内存。
6. 回调执行期间不得重分配或覆盖其 surface；回调返回后可在下一轮复用。

不支持双流的 UA 在 Content 开始时，Video 目标从普通有效尺寸切换为 1920x1080；
Content 清除后恢复普通有效尺寸。两次变化都按上述规则重建 Video surface。
UA remove 同时释放两个 surface。

### 10.2 填充模式

Video 源放入 Gallery/FullScreen/Active 目标区域时使用接收 UA 的 `fill_mode`。
Screen 源无条件使用 contain；不支持双流时放入 Video 大画面区域的 Content
仍视为 Screen 源并强制 contain。

在应用 cover/contain 前先判断：

```text
source.width < target.width && source.height < target.height
```

只有两个条件同时成立时才禁止放大：不裁剪、不缩放，按源原始尺寸以偶数偏移
居中，四周保持填充颜色。只要任一源维度不小于目标区域，就正常执行对应模式。

缩放统一调用 libyuv 并使用 `libyuv::kFilterBilinear`。所有由比例计算得到的
裁剪宽高、输出宽高和居中偏移都使用 `floor_even(x) = floor(x / 2) * 2`，即
Floor to Even；目标区域本身保持规格定义的正偶数尺寸。

cover 使用 CSS `object-fit: cover` 语义：先在源画面中心计算与目标宽高比一致的
最大裁剪矩形，再用上述过滤模式缩放至完整目标区域。比例比较和裁剪尺寸计算使用
带溢出检查的 64-bit 整数算术；裁剪坐标和宽高使用 Floor to Even。例如
640x1080 放入 1280x720 时，先居中裁成 640x360，再放大到 1280x720。

contain 不裁剪，使用
`min(target_width / source_width, target_height / source_height)` 等比缩放，
输出宽高使用 Floor to Even，再以 Floor to Even 的偏移居中。未覆盖区域保持
填充颜色。因取整多出的 1 至 2 个像素留在目标区域右侧或底部。

### 10.3 Cell 合流、Active Speaker 高亮和 OSD 层级

每轮 Video 输出在同一 Conference 状态快照中捕获本轮选中的 cell、当前自动
Active Speaker UA ID 和各源帧快照。Spotlight 和 Pinned 只决定布局位置，不改变
Active Speaker 身份。按以下分层顺序渲染完整 Video surface：

1. 用接收 UA 的 `fill_color` 填充完整输出画布。
2. 按第 9.1 节定义的物理槽位顺序处理所有 Video cell：使用
   `libyuv::kFilterBilinear` 和 Floor to Even 完成 cover/contain 裁剪、缩放，
   再把 cell 图像不透明合入输出画布；本阶段不绘制边框或 OSD。
3. 若当前 Conference Active Speaker 在本帧选中的 Video cell 中可见，则在该
   cell 最终坐标上绘制高亮边框。相同 UA 已按第 9.1 节去重，因此每个输出画面
   最多有一个高亮 cell。Active Speaker 是接收方自身、没有有效 Video，或在
   FullScreen 中被更高优先级 Spotlight/Pinned 覆盖时不绘制高亮。
4. 按物理槽位顺序为所有 Video cell 绘制昵称 OSD。Active cell 先有边框、后有
   OSD；非 Active cell 只绘制 OSD。自定义 OSD 边距允许文字覆盖边框，层级仍以
   OSD 在上为准。

分层绘制保证 half-outside 边框不会被后续 cell 的视频合入覆盖。高亮只应用于
合成 Video surface 中的 Video cell；双流 Screen Content 和单流 Video 大画面
中的 Content cell 均不高亮。单流 Content 场景中的 Active Speaker 如果出现在
顶部 Video 小画面中，仍正常高亮该小画面。Content 完成 contain 合入后仍按其
源快照绘制昵称 OSD，只是不执行 Active Speaker 边框阶段。

高亮颜色固定为 sRGB `#00E676`。首版 limited-range BT.601 I420 对应常量为
`{Y=143, U=113, V=35}`。边框宽度按 cell 高度独立计算：

```text
border_width = max(2, (3 * cell_height + 125) / 250)
inside_width = (border_width + 1) / 2
outside_width = border_width / 2
```

以上除法均为正整数除法。`border_width` 公式精确等价于
`max(2, floor(cell_height * 0.012 + 0.5))`，但不使用浮点运算。因此偶数宽度
内外各半；奇数宽度多出的一个像素放在 cell 内侧。对于 cell 半开矩形
`[x, x + width) x [y, y + height)`，边框是外矩形
`[x - outside_width, x + width + outside_width) x
[y - outside_width, y + height + outside_width)` 减去内矩形
`[x + inside_width, x + width - inside_width) x
[y + inside_width, y + height - inside_width)`。外矩形只裁剪到完整输出画布，不
裁剪到 cell，因此内部 cell 的外半边允许覆盖相邻 cell 或布局间隙；位于画布
边缘的外半边自然被画布裁剪。

Y plane 对边框覆盖的 luma 像素直接写入 143。I420 的每个 U/V sample 对应
2x2 luma block；当边框只覆盖其中一部分像素时，分别按覆盖数 `n` 做
coverage blend：

```text
sum = n * chroma_border + (4 - n) * chroma_old
chroma_out = (sum + 2) / 4
```

其中 `n` 为 `0..4`，U 使用 113，V 使用 35，`sum` 和除法均使用非负整数。
`(sum + 2) / 4` 精确表达对实数 `sum / 4` 四舍五入。这样可保持奇数边框宽度
和 half-outside 几何，不额外把边框宽度取偶。昵称 OSD 随后仍只混合 Y plane，
白色目标为 Y=235。

### 10.4 GalleryView

选择 N 路源时：

```text
columns = ceil(sqrt(N))
rows = ceil(N / columns)
```

N=0 时输出纯填充色帧。分区边界调整为偶数；尺寸不能整除时，最后的格子吸收
剩余偶数像素。空格保留填充颜色。最多选择 25 路，即最大 5x5。

源进入格子的先后顺序按第 9.1 节确定，第一个源进入左上角，之后逐行从左到右、
从上到下填充。

### 10.5 FullScreen

FullScreen 只有一个与完整 Video surface 等大的区域。源按第 9.1 节的优先级
选择，使用接收 UA 的 Video fill mode；没有有效候选时输出纯填充色帧。

### 10.6 ActiveTop

顶部五个小画面，底部一个大画面：

- 1280x720：5 个 256x162 小区域，1 个 1280x558 大区域。
- 1920x1080：5 个 384x242 小区域，1 个 1920x838 大区域。

原始需求中的 1080p 高度 243/837 调整为 242/838，保证所有 I420 子区域坐标
和尺寸为偶数。

### 10.7 ActiveBottom

顶部一个大画面，底部五个小画面，区域尺寸与 ActiveTop 相同。

### 10.8 UA 覆盖配置

UA 可以覆盖布局和输出尺寸；未覆盖时继承 Conference。输出尺寸只允许 720p
或 1080p。

## 11. 单流与双流

以下规则针对每个非 RTC 接收方。

### 11.1 不支持双流

存在其他 UA 的活动 Content 时，强制使用 1920x1080 ActiveTop。Content 放在
底部大画面并强制使用 contain；它不属于 Video 候选，不占用 Video 槽位，也不
与同 UA 的 Video 去重。按第 9.1 节的 Spotlight、Pinned、Active Speaker、UA
加入顺序候选队列选择最多五路 Video，使用接收 UA 的 Video fill mode 放在顶部
小画面。Content 源 UA 的 Video 只要满足普通 Video 候选条件，仍可同时进入小画面。
输出通过 `YUVMIX_VIDEO` 回调发送，Spotlight、Pinned 和 Active Speaker 都不替换
此场景的大画面 Content。新 Content 只更新最新快照；单流接收方不因 Content
输入立即产生 Video 输出，而是在下一次正常 Video deadline 使用最新快照合成。
`composed_content_fps` 只限制双流 `YUVMIX_CONTENT` 输出，不控制单流 Video。

没有其他 UA 的 Content 时，仅使用其他 UA 的 Video，按该接收方的有效布局和
尺寸合成，并回调 `YUVMIX_VIDEO`。

### 11.2 支持双流

Content 不参与 Video 合成、Video 候选排序、Video 槽位计算或 Video UA 去重。
Content 源 UA 的 Video 仍可作为独立 Video 候选。其他 UA 的 Video 按接收方有效
布局和尺寸以及第 9.1 节候选队列合成，通过 `YUVMIX_VIDEO` 回调发送。

新 Content 到达限频发送时刻后，对除 Content 源 UA 外的每个支持双流非 RTC
接收方，把最新 Content 快照以 contain 放入该接收 UA 的 1920x1080 Screen
surface，再按源快照捕获的 OSD 状态绘制 DisplayName，并作为 `YUVMIX_CONTENT`
输出。源宽和高都
小于 1920x1080 时保持原尺寸居中，不放大。没有新 Content 时不重复发送。

这里“没有新 Content 时不重复发送”按接收 UA 和 generation 判断：已经成功收到
当前 generation 的 UA 不会重发；因 surface 分配或合流准备失败、尚未调用回调
的 UA 允许在后续 Content 周期重试当前 generation。

双流 Content 回调是纯帧数据通道，不表达 START/STOP/EOS。新加入接收方只从
加入后的下一帧 Content 开始接收，不补发加入前缓存帧；业务层必须通过独立
控制消息通知双流开始/停止，并保证活动共享期间继续向 YuvMix Push 新帧。
Screen 回调的 `type` 固定为 `YUVMIX_CONTENT`，宽高固定为 1920x1080。

### 11.3 停流与离会

YuvMix 把最后一次成功 Push 的 Video/Content 视为持续有效快照；仅停止 Push
不表示停流。UA 保留在 Conference、只暂停一路媒体时，业务层必须调用
`yuvmix_clear_yuv` 清理对应 Video 或 Content，后续重新 Push 即恢复。参与者
完整离会时调用 `yuvmix_ua_remove`，无需先逐路 clear；remove 后该 UA 不再参与
任何合流，也不再接收任何输出。两种操作都不产生空帧、EOS 或控制消息。

## 12. 音频格式与缓冲模型

### 12.1 支持的 PCM

首版只接受有符号 16-bit little-endian 单声道 PCM。采样率只允许 8000、
16000、32000、44100、48000 Hz。本库不重采样、不转换声道、不转换位深。

每个 Conference 必须通过 `yuvmix_conference_set_audio_format` 设置唯一内部
音频格式。全部 RTC 和非 RTC Push 必须与该格式完全一致；不一致直接返回
`YUVMIX_AUDIO_FORMAT_MISMATCH`，不得清空、append 或推进任何 ring/游标。
格式第一次设置成功后在 Conference 生命周期内不可改变。设置为当前相同格式
幂等返回；设置不同格式返回 `YUVMIX_AUDIO_FORMAT_MISMATCH`，不清空或修改
现有状态。首次设置和 Push 的格式检查由 Conference 状态锁线性化；Push 只会
观察到“尚未设置”或完整发布的固定格式。输出格式始终与输入格式相同。

`yuvmix_audio_frame_t` 同时用于输入和输出。`buffer_size` 必须大于 0 且
能被 2 整除。输入时 `ua_type` 表示源类型；输出时表示该混音对应的非 RTC
接收方类型。

### 12.2 共享源 ring buffer

每个音频源只有一个固定容量 ring buffer，容量等于 Conference 采样率下 1 秒的 sample
数。RTC 使用一个 Conference 级 ring；每个非 RTC UA 使用自己的源 ring。
RTC 输入忽略 UA ID，直接 append 到 Conference ring，不存在 RTC 源切换或
RTC UA ID 校验。

每个音频 ring 包含：

- 固定容量 `std::vector<int16_t>` sample 数组。
- 单调递增的 64-bit `write_seq`。
- 每个非 RTC 接收方一个游标，游标包含单调递增的 64-bit `read_seq` 以及
  `BUFFERING`/`ACTIVE` 状态。
- 非 RTC 源 ring 还包含当前能量窗口的 sample 平方和及 sample 数。
- 一个互斥锁。

物理数组下标为 `sequence % capacity_samples`。消费数据只推进接收方
read_seq，不执行 erase、memmove 或物理删除。最早可读序号由 write_seq 和
固定容量计算。

通过 Conference 固定格式校验后执行 append。输入字节显式按 little-endian
有符号 16-bit 解码，不依赖主机字节序。首次设置格式时为已有源初始化 ring；
之后新加入的非 RTC UA 直接按该固定格式创建 ring。任何分配失败都不得发布
部分初始化状态。

单次 Push 超过 1 秒时只保留最新 1 秒。新写入覆盖未读数据时，把落后接收方
read_seq 推进到新的最早可读位置。每次成功 Push 在完成 append 和 overflow
游标修正后、释放目标 ring 锁前，立即检查该源上每个接收方游标的高水位。
Push 返回成功，释放 ring 锁后通过日志回调记录 overflow 或高水位丢弃。

高水位按“接收方在一个源上的游标”独立计算。某游标的可读数据超过
`audio_high_watermark_ms` 时，丢弃该游标最旧的数据，把可读数据恢复到 60 ms，
并把该游标置为 `BUFFERING`。该操作在本次 Push append 后执行，不影响同一源上
的其他接收方游标，也不修改物理 ring。
高水位用于限制排队延迟；1 秒 ring 容量用于限制物理内存，两者含义不同。

### 12.3 游标生命周期

新非 RTC 接收方加入时，它在所有现有源上的游标初始化为各源当前 write_seq，
状态为 `BUFFERING`，不播放加入前的历史数据。该接收方的 Audio `counts` 初始化
为 0，`last_compensation_time` 初始化为当前 `steady_clock` 时间。

一个源第一次产生数据时，所有现有合格接收方从该次首批数据的起点开始读取。
源不为自己创建接收游标。

删除非 RTC UA 时，删除其源 ring，并从其他所有源中删除它作为接收方的游标。
删除 RTC UA 不影响 Conference RTC ring，因为 RTC 音频不关联 UA ID。

Push 只执行 append；完成 Conference/UA 查找和校验后，仅锁目标源 ring。
Conference 创建时 Audio 回调已经保证非 NULL；音频格式设置成功之前不调度
20 ms Audio deadline，也不执行音频混合。

### 12.4 简化时间轴约定

首版 Audio Push 和回调都不携带 PTS/NTP。调用方必须按播放顺序 Push 已经完成
解码、抖动缓冲和时钟归一化的连续 PCM；ring 中的 append 顺序就是源时间轴，
每个接收方的 read_seq 就是该接收方消费时间轴。YuvMix 不按采集时刻对齐不同
源，也不补偿源时钟漂移。

Audio 输出以 20 ms 为基本时间片。正常回调为 20 ms；时间补偿时，一个回调可以
包含多个连续 20 ms 时间片。没有回调的时间片表示接收方时间轴上的缺口。

下游必须维护至少 60 ms 的输出抖动缓冲，在实际播放/发送 deadline 到达前接收
正常或补偿回调；仍未取得数据时补足静音，连续推进 RTP/sample 时钟，并完成
所需 ptime 分包。超出下游抖动缓冲吸收范围的迟到数据由下游丢弃，不允许倒退
或重复推进已经发送的 RTP/sample 时间轴。

## 13. 音频混合

每 20 ms，Conference 线程依次把每个非 RTC UA 作为音频接收方。接收方永远
不接收自己的输入音频。候选源为 Conference RTC ring 和其他所有非 RTC UA
源 ring。

首版使用以下 Audio 时间参数：

- `mix_interval_ms = 20`，即一个基本混音时间片。
- `min_buffer_ms = 60`，即三个基本时间片，是源首次参与或 underflow 后重新参与
  的缓存门槛。
- `compensation_window_ms = 1000`，用于周期性校正 timer 调度抖动。
- `audio_high_watermark_ms` 默认 200，可在 Conference 创建时配置。
- `max_audio_compensation_ms` 默认 200，可在 Conference 创建时配置，用于限制
  单次补偿处理和回调长度。

每个非 RTC 接收 UA 独立维护 `counts` 和 `last_compensation_time`。`counts` 表示
从上次补偿时间起已经处理的 20 ms 逻辑时间片数量；即使该时间片没有源贡献、
没有产生 Audio 回调，也必须递增。每次 Audio timer 触发都使用
`std::chrono::steady_clock` 计算：

```text
elapsed_ms = now - last_compensation_time

if elapsed_ms < 1000:
    raw_requested_intervals = 1
    完成本轮后 counts += 1
else:
    expected_intervals = ceil(elapsed_ms / mix_interval_ms)
    raw_requested_intervals = max(0, expected_intervals - counts)
    完成本轮后 counts = 0
    last_compensation_time = now

max_intervals = max_audio_compensation_ms / mix_interval_ms
emitted_intervals = min(raw_requested_intervals, max_intervals)
skipped_intervals = raw_requested_intervals - emitted_intervals
```

`expected_intervals`、sample 数和 buffer 字节数使用带溢出检查的 64-bit 算术。
超过上限的 `skipped_intervals` 是时间轴上较早的缺口，不生成 PCM。对每个源以
带溢出检查的算术计算 `skipped_samples = skipped_intervals * samples_per_20ms`，
把 read_seq 一次推进 `min(available_samples, skipped_samples)`，并把游标统一置回
`BUFFERING`；不得按时间片逐个循环。随后只混合时间上最新的
`emitted_intervals`。

`emitted_intervals` 大于 1 时，把所有输出时间片写入一个连续 PCM buffer，只
执行一次 Audio 回调。例如结果为 3 时生成一个 60 ms 回调。结果为 0 时不混音、
不回调，但仍完成补偿时间点和 `counts` 的重置。下游把被跳过的历史时间片作为
缺口补静音；单次 PCM 回调、累加缓冲和正常逐片混音量都不超过 Conference
配置的最大补偿长度。

为一个接收方混音时：

1. 在 Conference 锁内取得候选源对象快照并释放 Conference 锁。
2. 按 RTC 在前、非 RTC UA 加入序号递增的固定顺序锁定所有候选 ring。
   Push 每次只锁一个 ring，不存在反向加锁顺序。
3. 所有候选源都已经通过 Conference 固定音频格式校验；各游标的高水位已在
   对应源最近一次成功 Push append 后完成检查和修正。
4. 批量跳过 `skipped_intervals`，再按 20 ms 子时间片依次处理
   `emitted_intervals`。每个源游标使用以下状态机：

```text
BUFFERING:
    available_samples >= samples_per_60ms 时进入 ACTIVE，并在当前时间片继续读取
    否则本时间片贡献静音，并保留已有数据

ACTIVE:
    available_samples >= samples_per_20ms 时读取并推进 20 ms
    否则本时间片贡献静音，保留不足 20 ms 的数据，并转回 BUFFERING
```

5. 空源、`BUFFERING` 源和发生 underflow 的源都按静音处理，不限制其他源，
   也不缩短本轮输出长度。一个源只推进自己实际成功读取的完整 20 ms 时间片。
6. 对应位置的有符号 16-bit sample 使用 `int64_t` 累加；全部源累加完成后钳位
   到 `[-32768, 32767]` 并写为有符号 16-bit PCM。首版接受多人同时讲话时的
   削波，不做平均、增益归一化或饱和钳位之外的 limiter。
7. 如果本轮至少一个源贡献了一个完整时间片，则输出长度固定为
   `emitted_intervals * samples_per_20ms`；各源未贡献的位置保持静音。如果整轮
   没有任何源贡献，不发送 Audio 回调，由下游按第 12.4 节补静音。
8. 只推进当前接收方的 read_seq，其他接收方游标不变。
9. 释放所有 ring 锁后再调用该接收方 Audio 回调。

例如 SIP UA-1 不发送音频，但 RTC 或 UA-2 有可读数据时，UA-1 仍会收到混音。
对其他接收方而言，UA-1 空源是静音，不会阻塞 RTC 或 UA-2。

回调帧使用 Conference 固定音频格式，`ua_type` 为接收方类型，
`buffer_size = emitted_intervals * samples_per_20ms * 2`。PCM 内存只在回调
期间有效。

## 14. 回调约束

Video/Content 回调接收 userdata、Conference ID、接收方 UA ID 和视频帧；
视频帧包含宽高、Video/Content 类型，以及 Y/U/V 三个 plane 的指针、stride
和可读字节数。库内部输出通常是连续 I420，但调用方必须按回调提供的 stride
逐行读取，不能自行假设紧密布局。输出 `frame->struct_size` 为
`YUVMIX_I420_FRAME_V1_SIZE`。Video 回调的 `type` 为 `YUVMIX_VIDEO`，宽高取
该接收 UA 本轮实际 Video surface 目标尺寸：通常为有效 Video 配置尺寸，单流
Content 活动时为强制的 1920x1080。Screen 回调的 `type` 为
`YUVMIX_CONTENT`，宽高固定为 1920x1080。

独立 Audio 回调接收自己的 userdata、两个 ID 和 PCM 帧；PCM 帧包含格式、
接收方 UA 类型、指针和字节数，`audio->struct_size` 为
`YUVMIX_AUDIO_FRAME_V1_SIZE`。

两个 ID 和帧内存都只在回调执行期间有效，需要长期持有时由调用方复制。
不同 Conference 线程可并发执行回调，业务回调必须线程安全；同一 Conference
内 Audio/Video/Content 回调严格串行。

媒体输出回调内只允许调用以下使用同一 Engine 的状态 setter：

- `yuvmix_conference_set_spotlight_ua`
- `yuvmix_ua_set_display_name`
- `yuvmix_ua_set_layout`
- `yuvmix_ua_set_pinned_ua`
- `yuvmix_ua_set_fill_mode`
- `yuvmix_ua_set_fill_color`
- `yuvmix_ua_set_audio_volume_energy`

配置 init、`yuvmix_status_string` 和 `yuvmix_get_abi_version` 没有 Engine 参数，
也允许调用。业务集成契约明确禁止从媒体输出回调修改 Conference 默认布局、尺寸
或 FPS，因此 `yuvmix_conference_set_defaults` 不在白名单内。对同一 Engine 的
其他公共 API，包括该函数、创建/增加/删除、媒体输入、`yuvmix_clear_yuv`、设置
Audio 格式、close 和 destroy，必须在修改状态前返回
`YUVMIX_WOULD_DEADLOCK`。对其他 Engine 的调用按普通规则处理。

业务提供的媒体和日志回调不得抛出 C++ 异常。库在每次业务回调边界仍执行
`catch (...)`，防止异常终止 Conference 线程或越过 C ABI：媒体回调异常在释放
相关回调在途状态后记录错误；日志回调异常直接抑制，不递归记录。已经实际调用
的 Content 回调即使异常也视为该 generation 的投递尝试完成并推进接收 UA 的
成功 generation，避免对违反契约的回调无限重试。

## 15. 错误处理与日志

每个 API 在修改状态前完成全部输入校验。非法输入不得造成部分状态更新。
I420 输入宽高小于 16、不是偶数或其他帧参数非法时返回
`YUVMIX_INVALID_ARGUMENT`。I420 尺寸和内存分配使用溢出检查。

Audio Push 在以下情况返回 `YUVMIX_INVALID_ARGUMENT`：

- audio 或 buffer 为 NULL。
- buffer 长度为 0 或奇数。
- 采样率不在支持列表。
- channels 不为 1。
- bits per sample 不为 16。
- 非 RTC 输入缺少 UA ID、UA 不存在或类型不一致。

RTC Push 忽略 UA ID。Conference 未设置音频格式时返回
`YUVMIX_AUDIO_FORMAT_NOT_SET`，Push 与固定格式不一致时返回
`YUVMIX_AUDIO_FORMAT_MISMATCH`，均不修改状态。ring overflow 按第 12 节规则
处理，属于成功操作。

同步公共 API 中的 C++ 内存分配失败映射为 `YUVMIX_OUT_OF_MEMORY`；Conference
工作线程中的 surface 分配失败按第 10.1 节记录日志并跳过对应输出。其他异常在
C ABI 边界捕获，可用时记录日志，并映射为 `YUVMIX_INTERNAL_ERROR`。

默认不输出日志。可选 Engine 日志回调接收诊断信息，内部代码不直接写
stdout/stderr。日志可以在调用公共 API 的业务线程或任意 Conference 工作线程
同步触发，同一 Engine 的日志回调可能并发执行，业务实现必须线程安全。不创建
专用日志线程，也不异步保存 message 或 userdata。

日志 `message` 是以 NUL 结尾的 UTF-8 字符串，只在本次回调期间有效；
`log_userdata` 原样传回并至少保持有效到 close 返回。日志回调与媒体输出回调
使用统一的准入和在途计数，进入 CLOSING 后不再准入新日志，close 等待已准入
日志返回。日志只能在释放 Engine、Conference、UA、音频 ring 和 FreeType 锁后
调用。

从某个 Engine 的日志回调重入该 Engine 的任何公共 API，必须在修改状态前返回
`YUVMIX_WOULD_DEADLOCK`；无 Engine 参数的 `yuvmix_status_string` 和
`yuvmix_get_abi_version` 允许调用。线程局部 callback context 用于识别重入，
同一线程产生的嵌套日志直接抑制。ring overflow、高水位丢弃等诊断在释放相关
锁后记录；日志回调本身无法返回状态，其执行不改变原 API 的返回结果。

`yuvmix_status_string` 为每个状态返回静态字符串指针。

## 16. 测试

CTest 覆盖：

- 从 `.c` 和 `.cc` 编译单元编译纯 C ABI，检查 `extern "C"`、导出符号、
  major/minor 编码、首版未发布 ABI 1.0 基线和结构体布局。
- config init 使用旧版较小容量时不越界，并正确为缺失尾字段使用默认值；容量
  小于 `uint32_t` 或大于 `UINT32_MAX` 时不写内存。
- I420/Audio v1 最小输入 `struct_size`、未知尾部忽略和回调输出
  `struct_size`。
- Engine、Conference、UA 生命周期和全部参数校验，包括 Conference 的两个输出
  回调任一为 NULL 时创建失败，以及 Conference/UA ID 为 NULL 或空字符串时失败。
- Conference 数量限制、每 Conference 32 个 UA 的固定上限、RTC/非 RTC 共同计数、
  UA remove 释放名额，以及 `YUVMIX_RESOURCE_LIMIT`。
- 同步 close/destroy：close 拒绝新调用、等待在途 API/回调、停止 timer、join
  全部 Conference 线程，以及从媒体/日志回调调用 close/destroy 时在状态修改前
  返回 `YUVMIX_WOULD_DEADLOCK`。
- 三平面 I420 的 NULL、stride、plane size、宽高小于 16、奇数尺寸、带 padding
  行复制和输出 stride。
- Video/Content OSD、有效 UTF-8 按 Unicode code point 顺序简单绘制、不执行复杂
  shaping、配置字体缺字时使用系统字体回退、动态遮罩、默认 12 像素及自定义
  边距、U/V 不变，
  以及先 cover/contain、再 Active Speaker 边框、最后按 cell 绘制 OSD，cover
  不裁掉名称、NULL DisplayName 等同空字符串和旧快照保留旧 OSD 状态。
- UA 创建时 Video/Screen surface 只有逻辑默认尺寸且 plane 为空；Video 首次
  输出按有效尺寸分配，Screen 首次双流输出固定分配 1920x1080，RTC 和未使用
  Screen 不分配像素内存。
- surface 尺寸变化时释放并重建、尺寸不变时复用、分配失败只跳过对应 UA 本轮
  输出，以及 UA remove 同时释放两个 surface。
- 每轮合流前完整填充 Y/U/V，覆盖默认 limited-range 黑色和自定义 I420 颜色。
- cover 的横向/纵向中心裁剪、`kFilterBilinear` 缩放、Floor to Even，以及
  contain 的等比缩放、居中和填充区域。
- 源宽高都小于目标时 cover/contain 均保持原尺寸；只有一个维度较小时仍执行
  正常模式，包括 640x1080 居中裁成 640x360 再放大到 1280x720 的 cover 用例。
- Video 源使用接收 UA fill mode；Screen 和单流 Video 中的 Content 强制
  contain；双流 Content 输出统一为 1920x1080。
- fill mode/color 的 config 默认值、短结构体默认值、setter 下一轮生效且不重发
  静止 Content、非法 fill mode、RTC 保存后暂不使用、并发合流快照、close 和
  UA remove。
- GalleryView 的 0、1、2、4、9、16、25 路输入，以及从左到右、从上到下的
  槽位映射。
- 720p/1080p FullScreen、ActiveTop、ActiveBottom 及各自物理渲染顺序。
- Conference Spotlight 和接收 UA Pinned 的设置、替换、清除、目标校验、
  接收方自身拒绝、无 Video 时暂时回退、Video 恢复后重新生效，以及 UA 删除时
  清理全部关联引用。
- GalleryView、FullScreen 和 ActiveTop/ActiveBottom 统一使用 Spotlight、
  Pinned、Active Speaker、入会顺序候选队列；Active 布局的大画面取第一路，
  小画面继续取后续候选。
- Spotlight、Pinned、Active Speaker 为三个不同 UA 时的顺序、任意两项或三项
  指向同一 UA 时的 Video 去重、无效源跳过后紧凑前移、接收方自身排除、无候选
  时填充色输出，以及不同接收 UA 的 Pinned 互不影响。
- Active Speaker 排在普通候选之后入会时，GalleryView 和 ActiveTop/ActiveBottom
  仍优先选中它；FullScreen 分别覆盖 Spotlight、Pinned、Active Speaker 和普通
  候选成为队列第一路的情况。
- Active Speaker 高亮在 GalleryView、FullScreen、ActiveTop/ActiveBottom 大小
  画面中的定位；Spotlight/Pinned 不改变高亮身份；Active Speaker 等于接收方
  自身、无有效 Video 或在 FullScreen 被更高优先级源覆盖时不渲染且无高亮；
  其他场景优先进入可用 Video cell；Content cell 和双流 Screen 永不高亮。
- 所有 cell 视频先合入、随后绘制唯一 Active Speaker 边框、最后绘制全部 OSD
  的分层顺序；后续 cell 不覆盖 half-outside 边框，自定义 OSD 边距造成重叠时
  OSD 位于边框之上。
- `#00E676` 到 limited-range BT.601 I420 `{143,113,35}` 的颜色、2x2 chroma
  coverage blend，以及边框在 cell/画布边缘、相邻 cell 和布局间隙上的裁剪。
- 边框最小 2 像素、偶数宽度内外均分、奇数宽度多一个像素在内侧。固定覆盖
  `cell_height → (border, inside, outside)`：`162→(2,1,1)`、`242→(3,2,1)`、
  `558→(7,4,3)`、`720→(9,5,4)`、`838→(10,5,5)`、`1080→(13,7,6)`，并验证
  整数公式与原 1.2% 四舍五入定义等价。
- PCM16 归一化均方量纲、`0.0f`/`0.3f`/`1.0f` 边界、范围外或非有限能量拒绝、
  非 RTC ring 锁内累加、默认/自定义能量窗口、空窗口归零、非 RTC setter 立即
  覆盖及下一窗口替换、RTC setter 立即更新和 `steady_clock` 精确过期。
- 自动 Active Speaker 的会议级最高能量选择、同能量加入顺序、阈值过滤、
  候选变化或降到阈值时重置、1500 ms 前不切换及边界时切换、当前发言人
  晋升时同步设置 `last_spoke = now` 和有效标记、3000 ms 静默释放，以及挑战者
  满足 HoldTime 时不等待 ReleaseTime 直接切换。
- 自动当前发言人/候选 UA 删除或清除 Video 时立即清理状态；首次和释放后选举
  重新满足 HoldTime；自动结果等于接收方时只为该接收方回退且不修改会议级
  状态。
- 单流 Content 合成、双流独立 Content 输出；Content 不参与 Video 候选排序、
  槽位计算或去重，Content 源 UA 的有效 Video 可以同时进入 Video cell；单流
  Content 顶部五个小画面按统一候选队列选源并优先包含 Active Speaker。覆盖
  Content 源同时是 Spotlight、Pinned、Active Speaker 或普通 Video 候选，以及
  Active Speaker 是接收方自身时不进入 Video cell 的情况；单流新 Content 等待
  下一 Video deadline，且 `composed_content_fps` 只影响双流 Content 输出。
- 唯一 Content 源切换、generation/dirty 合并、接收 UA 级成功记录、单 UA 分配
  失败重试且不重复其他 UA、新 UA 不收历史帧、限频、Video/Content clear、无
  新帧不重复成功输出，以及 UA remove 后不再作为源或接收方参与任何合流。
- RTC 只输入行为。
- 并发输入、setter、删除、close、回调、destroy。
- 同 Conference 回调串行、跨 Conference 并行、首次 Video deadline 为线程启动
  时刻加一个周期、deadline 跳过、业务线程 FPS 更新后从修改时刻重算，以及全部
  32 个 Conference 线程正常 join。
- Content 首次 dirty 立即具备调度资格、连续 dirty 按 Content FPS 限频、dirty
  期间新输入不推迟 deadline、失败接收方下一周期重试，以及长时间 idle 后重新
  dirty 时立即具备调度资格。
- 所有支持音频采样率、单声道 16-bit 约束、NULL/奇数字节缓冲、RTC 忽略
  UA ID、非 RTC 类型匹配。
- 未设置 Conference 音频格式、首次设置与并发 Push 的线性化、相同格式幂等、
  不同格式设置和不匹配 Push 无副作用，以及初始化分配失败时全状态不变。覆盖
  格式设置前无 Audio deadline，首次成功设置统一重置现有接收 UA 的 `counts`、
  `last_compensation_time` 和下一 deadline，并唤醒 Conference 线程。
- Audio append、ring wrap、单次超过 1 秒、overflow 游标推进和日志。
- 多接收方独立游标、新接收方不读历史、首批 Push 游标初始化、UA/源清理。
- 20 ms 基本时间片、60 ms `BUFFERING` 门槛、`ACTIVE` 持续消费、underflow
  返回 `BUFFERING`，以及空周期不回调但 UA `counts` 仍递增。
- 每次 timer 使用 `steady_clock` 检查 elapsed time、1 秒补偿边界、UA 级独立
  `counts`、多时间片合并、补偿结果为 0、默认/自定义最大补偿长度、超限历史
  时间片批量跳过，以及单次回调和逐片处理不超过配置上限。
- 默认/自定义 Audio 高水位、每次成功 Push append 后检查、超过高水位只保留
  最新 60 ms、不同接收方游标互不影响，以及 1 秒物理 ring overflow。
- 空源静音、自身排除、源间 underflow 互不阻塞、静音填充、`int64_t` 累加、
  正负饱和以及接收方专属混音结果。
- 并发 Audio Push、首次格式设置、UA 删除和同步 close；close 返回后无内部线程、
  timer 或回调活动，业务停止新调用后延迟 destroy。
- 日志回调的业务/Conference 线程来源、跨 Conference 并发、message/userdata
  生命周期、锁外调用、重入拒绝、嵌套日志抑制和 close 等待。
- 媒体回调内同 Engine setter 白名单、`yuvmix_conference_set_defaults` 和其他
  同 Engine API 返回 `YUVMIX_WOULD_DEADLOCK`、跨 Engine 正常调用，以及媒体/
  日志回调异常的边界捕获、日志抑制和 Content generation 推进规则。
- 多 Conference 并发 DisplayName 更新与 FreeType 字体锁，以及在途快照下的
  帧缓冲 copy-on-write/缓冲池复用。
- 同 Conference Audio/Video/Content 严格串行及同时到期时 Audio 优先。

CI 在 Ubuntu 和 macOS 运行 Debug、Release。AddressSanitizer 和
UndefinedBehaviorSanitizer 使用独立任务；ThreadSanitizer 因运行成本和平台
限制单独执行。

## 17. 性能验收

基准创建 30 个 Conference。每个 Conference 有 9 个 RTC 视频输入 UA 和
1 个 SIP 接收 UA，Video 输出为 25 FPS；同时每个 Conference 接收一路
Conference 级 RTC PCM，并每 20 ms 为 SIP UA 处理 Audio；发生 timer 调度补偿
时允许一次回调携带多个连续 20 ms 时间片。预热后在音视频同时运行时测量
Video 批次完成时间和 Audio 调度延迟。

报告必须记录 CPU 型号、逻辑核数、Conference/线程数量、源/输出视频尺寸、
布局、音频格式和回调耗时。在选定的 32 核部署机器上，Video 批次 P95 目标
仍为不超过 30 ms。Audio 报告相对 20 ms deadline 的 P50/P95/P99 调度
延迟，并且不得出现持续累积的 backlog。

性能目标依赖硬件，以实测为准，不能仅根据架构宣称达标。基准使用最小回调，
把库内媒体处理开销和业务处理开销分离。

## 18. 仓库结构

```text
CMakeLists.txt
cmake/
include/yuvmix/yuvmix.h
src/
tests/
benchmarks/
examples/c/
docs/superpowers/specs/
```

实现文件分别承载 Engine 生命周期、调度、视频缓冲、音频 ring、OSD、布局
计算、视频合成、音频混合和 C ABI 适配，各模块使用明确且窄的内部接口。
