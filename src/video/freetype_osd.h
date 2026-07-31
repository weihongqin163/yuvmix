#ifndef YUVMIX_VIDEO_FREETYPE_OSD_H_
#define YUVMIX_VIDEO_FREETYPE_OSD_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "video/mix_yuv.h"

namespace yuvmix {

struct GlyphBitmap {
    int width;
    int rows;
    int bitmap_left;
    int bitmap_top;
    int advance_x;
    std::vector<uint8_t> coverage;
};

struct TextRun {
    std::vector<const GlyphBitmap*> glyphs;
    int64_t advance_x;
};

MixYuvStatus DecodeUtf8(const std::string& text,
                        std::vector<uint32_t>* code_points);

class FreeTypeOsd {
public:
    static MixYuvStatus Create(const MixYuvConfig& config,
                               std::unique_ptr<FreeTypeOsd>* osd);
    ~FreeTypeOsd();

    MixYuvStatus PrepareText(const std::string& text, TextRun* run);
    void DrawText(const TextRun& run,
                  const Rect& clip,
                  MutableI420ImageView* output) const;
    size_t glyph_count() const;
#if defined(YUVMIX_TESTING)
    int descender_pixels() const;
#endif

    FreeTypeOsd(const FreeTypeOsd&) = delete;
    FreeTypeOsd& operator=(const FreeTypeOsd&) = delete;

private:
    struct Impl;

    explicit FreeTypeOsd(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace yuvmix

#endif  // YUVMIX_VIDEO_FREETYPE_OSD_H_
