# YuvMix 设计检查记录

日期：2026-07-28

检查对象：`docs/superpowers/specs/2026-07-28-yuvmix-design.md`

本文保存设计检查结论及处理状态。正式行为以设计规格为准；本文件用于后续跟踪，
不直接定义公共 API 或运行时行为。

## 1. 已解决

### 1.1 音频最短长度策略导致持续积压

原设计使用所有非空源中的最短可读长度生成回调，并且只推进相同长度的游标。
短源会导致其他源的剩余音频在后续周期迟到播放，最终积压到 ring overflow。

当前设计已改为：

- Audio timer 基本间隔为 20 ms。
- 每个接收方与源之间使用 `BUFFERING`/`ACTIVE` 状态机。
- `BUFFERING` 缓存达到 60 ms 后进入 `ACTIVE`。
- `ACTIVE` 每个逻辑时间片消费 20 ms；不足 20 ms 时按静音处理并重新缓冲。
- 空源或 underflow 源不限制其他源，也不缩短输出长度。
- 每个接收 UA 独立维护 `counts` 和 `last_compensation_time`。
- 每次 timer 使用 `steady_clock` 计算 elapsed time；达到 1000 ms 时进行时间补偿。
- 多个补偿时间片合并为一个 PCM 回调。
- 即使没有产生回调，接收 UA 的逻辑 `counts` 仍然推进。
- 下游必须提供至少 60 ms 的输出抖动缓冲。

### 1.2 缺少音频高水位

原设计只有 1 秒物理 ring 容量，只能限制内存，不能限制接收方排队延迟。

当前设计为每个 Conference 增加 `audio_high_watermark_ms`：

- 默认 200 ms。
- 创建 Conference 时可配置。
- 合法范围为 60 至 1000 ms，且必须是 20 ms 的整数倍。
- 某个接收方在某个源上的可读数据超过高水位时，丢弃最旧数据，只保留 60 ms。
- 高水位处理只推进对应接收方的读游标，不影响其他接收方。

### 1.3 音频累加整数溢出

原设计使用 `int32_t` 累加全部源，在未限制 UA 数量时可能发生 C++ 有符号整数
溢出。当前设计改用 `int64_t` 累加，最后钳位为 int16。多人同时讲话产生的
削波属于当前业务可接受行为，不增加平均、自动增益或 limiter。

### 1.4 音频格式并发被业务契约消除

格式切换竞态不再通过 generation 和多 ring 原子切换解决。当前设计把 Conference
音频格式定义为首次设置后不可变：相同格式重复设置幂等，不同格式设置和不匹配
Push 都返回 `YUVMIX_AUDIO_FORMAT_MISMATCH` 且没有副作用。首次设置与 Push
格式检查由 Conference 状态锁线性化，输出始终使用相同固定格式，库内不做
重采样或格式转换。

### 1.5 close 成为同步停止屏障

close 进入 CLOSING 后拒绝新任务和输入，通过 Engine 级在途 API/回调计数等待
已进入调用完成，停止 timer、唤醒并 join 全部 Conference 线程。close 返回时
内部不再运行后台工作或触发回调。destroy 只负责最终释放；应用层在 close 后
停止新调用，并可按客户业务等待建议宽限时间再 destroy。从媒体或日志回调调用
close/destroy 会在状态修改前返回 `YUVMIX_WOULD_DEADLOCK`。

### 1.6 Video/Content 停流与 UA 离会

停止 Push 不表示停流。UA 保留在会议中、只暂停一路媒体时调用
`yuvmix_clear_yuv`；UA 完整退出时调用 `yuvmix_ua_remove`，无需先 clear。
remove 会删除该 UA 的媒体、能量、布局、音频源/游标和接收方状态，使其不再
参与任何合流。两种操作都不发送空帧、EOS 或控制消息。

### 1.7 音量能量定义与时序

RTC setter 和非 RTC 自动统计统一使用 `0.0f..1.0f` 的 PCM16 归一化样本均方
量纲，范围外或非有限值非法。非 RTC 累加器与源 PCM append 在同一 ring 锁内
更新，窗口结算释放 ring 锁后再发布。非 RTC setter 立即覆盖，但在下一自动
窗口结束时被替换；RTC setter 立即生效，并在 `steady_clock` 到达一个完整窗口
时精确过期。能量用于 Conference 级 Active Speaker 仲裁，不用于普通布局槽位
排序。

### 1.8 公共 C ABI 版本规则

当前设计补充了 major/minor 编码、`extern "C"`、`YUVMIX_API`、只允许尾部追加
字段、各媒体结构 v1 最小大小、回调输出 `struct_size`、init 超过
`UINT32_MAX` 的无写入错误，以及配置短前缀的默认值规则。当前项目尚未发布
首版 ABI，因此保持 1.0，并以当前完整公共 API 和结构体作为首次发布的 V1
基线；首次发布后的扩展必须提升 minor 并保留已发布的 V1 大小边界。

### 1.9 日志回调约束

日志可从公共 API 调用线程或 Conference 线程同步触发，并可跨 Conference 并发。
message 和 userdata 生命周期、锁外调用、统一回调准入/在途计数、CLOSING 后
抑制、同 Engine API 重入拒绝及嵌套日志抑制均已明确。

### 1.10 最大音频补偿长度

每个 Conference 增加独立的 `max_audio_compensation_ms`，默认 200 ms，合法范围
20 至 1000 ms 且必须是 20 ms 的整数倍。超过上限的较早逻辑时间片作为缺口
批量跳过，仅输出时间上最新的补偿片段；单次回调、累加缓冲和正常逐片处理量
均受该配置限制。该字段与音频高水位相互独立。

### 1.11 Video/Screen 合流缓冲与填充规则

每个 UA 预建 Video 和 Screen 合流结构，像素内存在第一次实际输出时按需分配；
Video 默认 1280x720 并使用当前有效输出尺寸，Screen 等同双流 Content 且首版
固定 1920x1080。每轮输出前用 UA 级 I420 fill color 填满 surface，默认值为
limited-range 黑色。

Video 源使用接收 UA 的 cover/contain 模式，默认 cover；Screen 及单流 Video
中的 Content 强制 contain。只有源宽和高都小于目标区域时才禁止放大并原尺寸
居中，否则 cover 先居中裁剪到目标宽高比再缩放，contain 等比完整放入。公共
UA config 和 setter 已补充 fill mode/color，surface 重建、失败处理、双流 1080p
输出及缩放边界测试也已写入规格。

缩放内部固定使用 `libyuv::kFilterBilinear`，所有比例计算产生的裁剪尺寸、输出
尺寸和居中偏移采用 Floor to Even。RTC UA 的 fill mode/color 仍校验、保存并
返回成功，但 RTC 首版不产生输出，所以暂不使用。fill setter 不把静止 Content
标记为 dirty；没有后续合流时看不到变化，符合“下一次合流生效”。

### 1.12 Content 接收方级 generation

Conference 为最新 Content 维护单调递增 generation，每个双流接收 UA 维护最后
成功输出的 generation。单个 UA 的 surface 分配或合流准备失败、没有调用回调
时，只保留该 UA 的待投递状态并在下一 Content 周期重试；实际调用回调且回调
返回后才推进 generation，其他已成功的 UA 不重复输出。新 UA 以当前 generation
初始化，因而不接收加入前的缓存帧。Content 被新帧覆盖时只投递最新 generation，
不补发中间帧。

### 1.13 OSD 绘制顺序

输入快照保存原始 I420 和该次 Push 捕获的 OSD 状态，不在源帧上预先绘制文字。
合流时先完成所有 cell 的 cover/contain 裁剪、缩放和画布合入，再绘制 Active
Speaker 高亮边框，最后在各 cell 左下角绘制 OSD，因此 cover 不会裁掉名称，
OSD 也不会被边框覆盖。OSD 左/下边距仍可按 UA 配置，默认值从 8 调整为 12
像素。DisplayName、字号或边距更新仍只从该 UA 下一次输入开始生效，旧快照
继续使用旧 OSD 状态。

### 1.14 Audio Push 后检查高水位

每次成功 Audio Push 先 append，并完成 1 秒 ring overflow 修正；随后仍在同一
ring 锁内检查该源的每个接收方游标。超过高水位的游标丢弃最旧数据，只保留最新
60 ms 并回到 `BUFFERING`。混音阶段不再先执行高水位裁剪，而是直接处理补偿
跳过和输出时间片。

### 1.15 配置范围与性能覆盖

`max_conferences` 的合法值明确为 0 或 1 至 32，其中 0 使用默认值 32。每个
Conference 固定最多 32 个 UA，RTC 和非 RTC 一并计数，超限返回
`YUVMIX_RESOURCE_LIMIT`。I420 输入宽高必须是至少 16 的正偶数。Conference
创建时 Video/Content 和 Audio 两个输出回调都必须非 NULL。本轮不增加固定
1080p Screen、尺寸变化重分配和 cover/contain 的专项性能验收；现有功能测试
仍覆盖这些行为。业务确认实际输入不会出现使裁剪或缩放结果小于 2 的极端
宽高比；首版把这一点保留为调用方前置条件，不增加额外校验或防御分支。

### 1.16 布局排列与选流优先级

首版布局增加 FullScreen。所有布局统一按 Spotlight、接收方 Pinned、Active
Speaker、UA 入会顺序构造 Video 候选队列。GalleryView 依次取最多 25 路，
FullScreen 取第一路，Active 大画面取第一路、小画面继续取后续最多五路。源选择
按接收方独立执行，排除自身、无效 Video 和重复 UA，并在跳过后紧凑前移。
Active Speaker 是接收方自身时不渲染；否则除 FullScreen 唯一槽位被更高优先级
Spotlight/Pinned 占用外，Active Speaker 必须优先进入一个可用 Video cell。

Conference Spotlight、接收 UA Pinned 和自动 Active Speaker 分别保存，使用
独立 setter 和生命周期规则。Spotlight/Pinned 暂无 Video 时只临时回退，偏好
保留；删除目标 UA 时清除所有引用。Spotlight/Pinned 不重置自动 Active Speaker，
因此显式偏好清除后可以立即恢复到已经稳定选出的发言人。单流 Content 强制
ActiveTop 的规则保持优先，大画面仍为 Content，五个 Video 小画面按统一候选
队列选择。Content 不参与 Video 排序、槽位计算或去重，Content 源 UA 的有效
Video 可以同时进入小画面。

已保存独立布局示意文件 `docs/yuvmix-layout-priority.html`，其内容与正式规格
第 9.1 节一致。

### 1.17 Active Speaker 高亮

当前 Conference Active Speaker 只要出现在某个合成 Video cell 中，就对该
cell 绘制绿色高亮，不受 Spotlight、Pinned 或大小画面位置影响。Active Speaker
等于接收方自身、无有效 Video 或在 FullScreen 被更高优先级源覆盖时不产生高亮；
Content cell 和双流 Screen 不高亮。Video 分层顺序固定为
全部 cell 视频合入、高亮边框、全部昵称 OSD，防止 half-outside 边框被后续 cell
覆盖，并保证 OSD 层级最高。

候选满足 HoldTime 晋升为当前 Active Speaker 时，同步设置 `last_spoke = now`
及其有效标记，使后续 ReleaseTime 始终具有确定的计时基准。

边框颜色固定为 `#00E676`，limited-range BT.601 I420 值为 `{143,113,35}`；
宽度使用等价的正整数公式 `max(2, (3 * cell_height + 125) / 250)`。偶数宽度
内外均分，奇数宽度多出的一个像素放在内侧，边框只按输出画布裁剪，允许覆盖
相邻 cell 或间隙。U/V 对部分覆盖的 2x2 luma block 使用 coverage blend；先算
非负整数 `sum`，再以 `(sum + 2) / 4` 精确表达对实数 `sum / 4` 四舍五入，避免
整数除法顺序歧义，也不为色度对齐改变规定的边框宽度。

布局示意 HTML 已同步增加 Active Speaker 绿色描边和渲染层级说明。

### 1.18 整体复查补充契约

Conference 两个媒体输出回调都定义为必填且创建后不可替换。媒体回调内使用
明确的同 Engine setter 白名单，其他同 Engine API 在修改状态前返回
`YUVMIX_WOULD_DEADLOCK`；业务回调不得抛出 C++ 异常，库边界仍捕获异常以保护
Conference 线程，异常的 Content 回调按已尝试投递推进 generation。

音频格式设置成功前不启动 Audio deadline。首次设置成功时统一重置现有非 RTC
接收 UA 的 `counts` 和 `last_compensation_time`，设置下一 20 ms deadline 并
唤醒线程。NULL DisplayName 等同空字符串。单流新 Content 等待下一正常 Video
deadline 输出，`composed_content_fps` 只控制双流 Content 输出。

首次 Video deadline 固定为 Conference 线程启动时刻加一个 Video 周期。Content
首次 dirty 或长时间 idle 后重新 dirty 时立即具备调度资格，连续 dirty 按上一次
Content 调度时刻限频；dirty 期间的新输入不推迟既有 deadline。业务层禁止从媒体
输出回调修改 Conference 默认布局、尺寸或 FPS，因此
`yuvmix_conference_set_defaults` 从同 Engine 回调白名单移除。

实际用于标识或查找对象的 Conference ID 和 UA ID 必须非 NULL 且非空，RTC Audio
Push 明确忽略的 `ua_id` 除外。DisplayName 由业务保证为合法 UTF-8，库内按
Unicode code point 顺序简单绘制，不支持复杂 shaping 或双向排版；配置字体缺字时
使用平台系统字体回退，回退 face 仍由 FreeType 栅格化。

## 2. 当前状态

本轮已确认项已落实到正式设计规格，包括统一 Video 候选队列、接收方自身
Active Speaker 排除、Content 不参与 Video 规则、32 UA 上限、回调必填与重入
约束、Audio/Video/Content 首次启动时间轴、非空 Conference/UA ID，以及简单
Unicode code point OSD 和系统字体回退。

以下项目按本轮决定暂缓或保留为业务前置条件：

- 暂不扩展 Gallery 非方阵、极端输入和最大负载性能矩阵。
- 极端宽高比由调用方避免，首版不增加运行时防御。
- 尚未规定 Conference/UA ID 最大长度、整数布尔字段是否只接受 0/1、DisplayName
  最大长度、字号/OSD 边距上限，以及 I420/Audio 单次输入最大资源边界；本轮确认
  这些资源上限继续暂缓。

下一步由用户最终审阅规格；确认后再编写实现计划。
