#ifndef YUVMIX_VIDEO_MIX_YUV_H_
#define YUVMIX_VIDEO_MIX_YUV_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace yuvmix {

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

class MixYuvContext {
public:
    static MixYuvStatus Create(const MixYuvConfig& config,
                               std::unique_ptr<MixYuvContext>* context);
    ~MixYuvContext();

    MixYuvContext(const MixYuvContext&) = delete;
    MixYuvContext& operator=(const MixYuvContext&) = delete;

private:
    struct Impl;

    explicit MixYuvContext(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;

    friend MixYuvStatus MixYuv(MixYuvContext*, const MixSource*, size_t,
                               MixOutput*);
#if defined(YUVMIX_TESTING)
    friend size_t MixYuvContextPreparedCapacity(const MixYuvContext&);
    friend size_t MixYuvContextGlyphCount(const MixYuvContext&);
#endif
};

MixYuvStatus MixYuv(MixYuvContext* context,
                    const MixSource* sources,
                    size_t source_count,
                    MixOutput* output);

#if defined(YUVMIX_TESTING)
size_t MixYuvContextPreparedCapacity(const MixYuvContext& context);
size_t MixYuvContextGlyphCount(const MixYuvContext& context);
#endif

}  // namespace yuvmix

#endif  // YUVMIX_VIDEO_MIX_YUV_H_
