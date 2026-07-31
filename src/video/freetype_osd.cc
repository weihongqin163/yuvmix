#include "video/freetype_osd.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yuvmix {

struct FreeTypeOsd::Impl {
    Impl()
        : library(NULL),
          face(NULL),
          osd_left(0),
          osd_bottom(0),
          descender_pixels(0) {}

    ~Impl() {
        if (face != NULL) {
            FT_Done_Face(face);
        }
        if (library != NULL) {
            FT_Done_FreeType(library);
        }
    }

    FT_Library library;
    FT_Face face;
    uint32_t osd_left;
    uint32_t osd_bottom;
    int descender_pixels;
    std::unordered_map<unsigned int, std::unique_ptr<GlyphBitmap> > glyphs;
    std::vector<uint32_t> code_points;
    std::vector<const GlyphBitmap*> prepared_glyphs;
};

namespace {

bool IsContinuation(uint8_t value) {
    return (value & 0xC0u) == 0x80u;
}

MixYuvStatus DecodeUtf8Into(const std::string& text,
                            std::vector<uint32_t>* code_points) {
    code_points->clear();
    code_points->reserve(text.size());
    size_t offset = 0;
    while (offset < text.size()) {
        const uint8_t first = static_cast<uint8_t>(text[offset]);
        uint32_t value = 0;
        size_t length = 0;
        uint32_t minimum = 0;
        if (first <= 0x7Fu) {
            value = first;
            length = 1;
        } else if (first >= 0xC2u && first <= 0xDFu) {
            value = first & 0x1Fu;
            length = 2;
            minimum = 0x80u;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            value = first & 0x0Fu;
            length = 3;
            minimum = 0x800u;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            value = first & 0x07u;
            length = 4;
            minimum = 0x10000u;
        } else {
            return MixYuvStatus::kInvalidArgument;
        }

        if (offset + length > text.size()) {
            return MixYuvStatus::kInvalidArgument;
        }
        for (size_t i = 1; i < length; ++i) {
            const uint8_t continuation =
                static_cast<uint8_t>(text[offset + i]);
            if (!IsContinuation(continuation)) {
                return MixYuvStatus::kInvalidArgument;
            }
            value = (value << 6) | (continuation & 0x3Fu);
        }
        if (value < minimum || value > 0x10FFFFu ||
            (value >= 0xD800u && value <= 0xDFFFu)) {
            return MixYuvStatus::kInvalidArgument;
        }
        code_points->push_back(value);
        offset += length;
    }
    return MixYuvStatus::kOk;
}

int RoundedAdvance(FT_Pos advance) {
    if (advance >= 0) {
        return static_cast<int>((advance + 32) / 64);
    }
    return -static_cast<int>((-advance + 32) / 64);
}

MixYuvStatus CopyGlyphBitmap(const FT_GlyphSlot slot, GlyphBitmap* glyph) {
    if (glyph == NULL || slot->bitmap.width > static_cast<unsigned int>(INT32_MAX) ||
        slot->bitmap.rows > static_cast<unsigned int>(INT32_MAX)) {
        return MixYuvStatus::kFontError;
    }

    glyph->width = static_cast<int>(slot->bitmap.width);
    glyph->rows = static_cast<int>(slot->bitmap.rows);
    glyph->bitmap_left = slot->bitmap_left;
    glyph->bitmap_top = slot->bitmap_top;
    glyph->advance_x = RoundedAdvance(slot->advance.x);
    glyph->coverage.assign(
        static_cast<size_t>(glyph->width) * glyph->rows, 0);

    const int pitch = slot->bitmap.pitch;
    const int absolute_pitch = pitch >= 0 ? pitch : -pitch;
    for (int row = 0; row < glyph->rows; ++row) {
        const int source_row = pitch >= 0 ? row : glyph->rows - 1 - row;
        const uint8_t* source =
            slot->bitmap.buffer + static_cast<size_t>(source_row) * absolute_pitch;
        uint8_t* destination =
            glyph->coverage.data() + static_cast<size_t>(row) * glyph->width;

        if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
            const unsigned int levels = slot->bitmap.num_grays;
            if (levels < 2) {
                return MixYuvStatus::kFontError;
            }
            for (int column = 0; column < glyph->width; ++column) {
                destination[column] = static_cast<uint8_t>(
                    static_cast<unsigned int>(source[column]) * 255u /
                    (levels - 1u));
            }
        } else if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
            for (int column = 0; column < glyph->width; ++column) {
                const uint8_t bit =
                    static_cast<uint8_t>(0x80u >> (column & 7));
                destination[column] =
                    (source[column >> 3] & bit) != 0 ? 255 : 0;
            }
        } else {
            return MixYuvStatus::kFontError;
        }
    }
    return MixYuvStatus::kOk;
}

}  // namespace

MixYuvStatus DecodeUtf8(const std::string& text,
                        std::vector<uint32_t>* code_points) {
    if (code_points == NULL) {
        return MixYuvStatus::kInvalidArgument;
    }

    try {
        std::vector<uint32_t> decoded;
        const MixYuvStatus status = DecodeUtf8Into(text, &decoded);
        if (status != MixYuvStatus::kOk) {
            return status;
        }
        code_points->swap(decoded);
        return MixYuvStatus::kOk;
    } catch (const std::bad_alloc&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (const std::length_error&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (...) {
        return MixYuvStatus::kInternalError;
    }
}

FreeTypeOsd::FreeTypeOsd(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

FreeTypeOsd::~FreeTypeOsd() {}

MixYuvStatus FreeTypeOsd::Create(const MixYuvConfig& config,
                                 std::unique_ptr<FreeTypeOsd>* osd) {
    if (osd == NULL) {
        return MixYuvStatus::kInvalidArgument;
    }
    osd->reset();

    if (config.font_path.empty() || config.font_size == 0 ||
        config.font_size > 512) {
        return MixYuvStatus::kInvalidArgument;
    }

    try {
        std::unique_ptr<Impl> impl(new Impl());
        if (FT_Init_FreeType(&impl->library) != 0 ||
            FT_New_Face(impl->library, config.font_path.c_str(),
                        static_cast<FT_Long>(config.font_face_index),
                        &impl->face) != 0 ||
            FT_Select_Charmap(impl->face, FT_ENCODING_UNICODE) != 0 ||
            FT_Set_Pixel_Sizes(impl->face, 0, config.font_size) != 0) {
            return MixYuvStatus::kFontError;
        }
        impl->osd_left = config.osd_left;
        impl->osd_bottom = config.osd_bottom;
        const FT_Pos descender = impl->face->size->metrics.descender;
        impl->descender_pixels = descender < 0
            ? static_cast<int>((-descender + 63) / 64)
            : 0;
        osd->reset(new FreeTypeOsd(std::move(impl)));
        return MixYuvStatus::kOk;
    } catch (const std::bad_alloc&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (const std::length_error&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (...) {
        return MixYuvStatus::kInternalError;
    }
}

MixYuvStatus FreeTypeOsd::PrepareText(const std::string& text, TextRun* run) {
    if (run == NULL) {
        return MixYuvStatus::kInvalidArgument;
    }

    try {
        MixYuvStatus status = DecodeUtf8Into(text, &impl_->code_points);
        if (status != MixYuvStatus::kOk) {
            return status;
        }

        impl_->prepared_glyphs.clear();
        impl_->prepared_glyphs.reserve(impl_->code_points.size());
        int64_t advance_x = 0;
        for (size_t i = 0; i < impl_->code_points.size(); ++i) {
            const FT_UInt glyph_index =
                FT_Get_Char_Index(impl_->face, impl_->code_points[i]);
            std::unordered_map<unsigned int,
                               std::unique_ptr<GlyphBitmap> >::iterator found =
                impl_->glyphs.find(glyph_index);
            if (found == impl_->glyphs.end()) {
                if (FT_Load_Glyph(impl_->face, glyph_index, FT_LOAD_DEFAULT) != 0 ||
                    FT_Render_Glyph(impl_->face->glyph,
                                    FT_RENDER_MODE_NORMAL) != 0) {
                    return MixYuvStatus::kFontError;
                }
                std::unique_ptr<GlyphBitmap> glyph(new GlyphBitmap());
                status = CopyGlyphBitmap(impl_->face->glyph, glyph.get());
                if (status != MixYuvStatus::kOk) {
                    return status;
                }
                GlyphBitmap* glyph_pointer = glyph.get();
                impl_->glyphs.insert(std::make_pair(
                    static_cast<unsigned int>(glyph_index), std::move(glyph)));
                found = impl_->glyphs.find(glyph_index);
                if (found == impl_->glyphs.end() ||
                    found->second.get() != glyph_pointer) {
                    return MixYuvStatus::kInternalError;
                }
            }
            impl_->prepared_glyphs.push_back(found->second.get());
            advance_x += found->second->advance_x;
        }
        run->glyphs.assign(impl_->prepared_glyphs.begin(),
                           impl_->prepared_glyphs.end());
        run->advance_x = advance_x;
        return MixYuvStatus::kOk;
    } catch (const std::bad_alloc&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (const std::length_error&) {
        return MixYuvStatus::kOutOfMemory;
    } catch (...) {
        return MixYuvStatus::kInternalError;
    }
}

void FreeTypeOsd::DrawText(const TextRun& run,
                           const Rect& clip,
                           MutableI420ImageView* output) const {
    if (output == NULL || output->y.data == NULL) {
        return;
    }

    const int64_t clip_left = clip.x;
    const int64_t clip_top = clip.y;
    const int64_t clip_right = std::min<int64_t>(
        static_cast<int64_t>(clip.x) + clip.w, output->width);
    const int64_t clip_bottom = std::min<int64_t>(
        static_cast<int64_t>(clip.y) + clip.h, output->height);
    int64_t pen_x = static_cast<int64_t>(clip.x) + impl_->osd_left;
    const int64_t baseline_y =
        static_cast<int64_t>(clip.y) + clip.h - impl_->osd_bottom -
        impl_->descender_pixels;

    for (size_t i = 0; i < run.glyphs.size(); ++i) {
        const GlyphBitmap& glyph = *run.glyphs[i];
        const int64_t glyph_x = pen_x + glyph.bitmap_left;
        const int64_t glyph_y = baseline_y - glyph.bitmap_top;
        for (int row = 0; row < glyph.rows; ++row) {
            const int64_t destination_y = glyph_y + row;
            if (destination_y < clip_top || destination_y >= clip_bottom) {
                continue;
            }
            for (int column = 0; column < glyph.width; ++column) {
                const int64_t destination_x = glyph_x + column;
                if (destination_x < clip_left || destination_x >= clip_right) {
                    continue;
                }
                const uint8_t coverage = glyph.coverage[
                    static_cast<size_t>(row) * glyph.width + column];
                if (coverage == 0) {
                    continue;
                }
                uint8_t* pixel =
                    output->y.data +
                    static_cast<size_t>(destination_y) * output->y.stride +
                    destination_x;
                const uint32_t mixed =
                    coverage * 235u + (255u - coverage) * *pixel;
                *pixel = static_cast<uint8_t>((mixed + 127u) / 255u);
            }
        }
        pen_x += glyph.advance_x;
    }
}

size_t FreeTypeOsd::glyph_count() const {
    return impl_->glyphs.size();
}

#if defined(YUVMIX_TESTING)
int FreeTypeOsd::descender_pixels() const {
    return impl_->descender_pixels;
}
#endif

}  // namespace yuvmix
