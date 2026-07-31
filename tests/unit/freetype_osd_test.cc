#include <memory>
#include <string>
#include <vector>

#include "test_support/i420_test_image.h"
#include "test_support/test_assert.h"
#include "video/freetype_osd.h"

#ifndef YUVMIX_TEST_FONT
#error "YUVMIX_TEST_FONT must name a deterministic test font"
#endif

int main() {
    using namespace yuvmix;
    using yuvmix_test::OwnedI420;

    std::vector<uint32_t> code_points;
    const std::string valid =
        std::string("A") + "\xE4\xB8\xAD" + "\xF0\x9F\x98\x80";
    EXPECT_EQ(DecodeUtf8(valid, &code_points), MixYuvStatus::kOk);
    EXPECT_EQ(code_points.size(), 3u);
    EXPECT_EQ(code_points[0], 0x41u);
    EXPECT_EQ(code_points[1], 0x4E2Du);
    EXPECT_EQ(code_points[2], 0x1F600u);

    const std::vector<uint32_t> valid_snapshot = code_points;
    EXPECT_EQ(DecodeUtf8(std::string("\xF0\x28\x8C\x28", 4),
                         &code_points),
              MixYuvStatus::kInvalidArgument);
    EXPECT_TRUE(code_points == valid_snapshot);
    EXPECT_EQ(DecodeUtf8(std::string("\xC0\x80", 2), &code_points),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(DecodeUtf8(std::string("\xED\xA0\x80", 3), &code_points),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(DecodeUtf8(std::string("\xF4\x90\x80\x80", 4),
                         &code_points),
              MixYuvStatus::kInvalidArgument);

    MixYuvConfig config;
    config.font_path = YUVMIX_TEST_FONT;
    config.font_face_index = 0;
    config.font_size = 18;
    config.osd_left = 2;
    config.osd_bottom = 2;
    std::unique_ptr<FreeTypeOsd> osd;
    EXPECT_EQ(FreeTypeOsd::Create(config, &osd), MixYuvStatus::kOk);
    EXPECT_TRUE(osd.get() != NULL);

    TextRun run;
    EXPECT_EQ(osd->PrepareText("Mix", &run), MixYuvStatus::kOk);
    EXPECT_EQ(run.glyphs.size(), 3u);
    EXPECT_TRUE(run.advance_x > 0);
    const size_t cached_glyphs = osd->glyph_count();
    EXPECT_EQ(osd->PrepareText("Mix", &run), MixYuvStatus::kOk);
    EXPECT_EQ(osd->glyph_count(), cached_glyphs);

    TextRun unchanged = run;
    EXPECT_EQ(osd->PrepareText(std::string("\xF0\x28\x8C\x28", 4),
                               &run),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(run.glyphs.size(), unchanged.glyphs.size());
    EXPECT_EQ(run.advance_x, unchanged.advance_x);

    TextRun missing;
    EXPECT_EQ(osd->PrepareText(std::string("\xF4\x8F\xBF\xBF", 4),
                               &missing),
              MixYuvStatus::kOk);
    EXPECT_EQ(missing.glyphs.size(), 1u);

    OwnedI420 image(64, 32, 5);
    image.Fill(16, 128, 128);
    MutableI420ImageView output = image.MutableView();
    const Rect full_rect = {0, 0, 64, 32};
    osd->DrawText(run, full_rect, &output);
    bool changed_y = false;
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            changed_y = changed_y || image.Y(x, y) != 16;
        }
    }
    EXPECT_TRUE(changed_y);
    EXPECT_TRUE(image.ActivePixelsEqual(image.Y(0, 0), 128, 128) == false);
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            EXPECT_EQ(image.U(x, y), 128);
            EXPECT_EQ(image.V(x, y), 128);
        }
    }
    EXPECT_TRUE(image.PaddingEquals(0xCC));
    EXPECT_TRUE(image.GuardsIntact());

    image.Fill(16, 128, 128);
    GlyphBitmap synthetic_first = {};
    synthetic_first.width = 6;
    synthetic_first.rows = 1;
    synthetic_first.bitmap_left = 1;
    synthetic_first.bitmap_top = 1;
    synthetic_first.advance_x = 10;
    synthetic_first.coverage.push_back(0);
    synthetic_first.coverage.push_back(1);
    synthetic_first.coverage.push_back(127);
    synthetic_first.coverage.push_back(128);
    synthetic_first.coverage.push_back(254);
    synthetic_first.coverage.push_back(255);
    GlyphBitmap synthetic_second = {};
    synthetic_second.width = 1;
    synthetic_second.rows = 1;
    synthetic_second.bitmap_left = -1;
    synthetic_second.bitmap_top = 2;
    synthetic_second.advance_x = 1;
    synthetic_second.coverage.push_back(255);
    TextRun synthetic_run;
    synthetic_run.glyphs.push_back(&synthetic_first);
    synthetic_run.glyphs.push_back(&synthetic_second);
    synthetic_run.advance_x = 11;
    osd->DrawText(synthetic_run, full_rect, &output);
    const uint32_t first_y =
        32u - config.osd_bottom -
        static_cast<uint32_t>(osd->descender_pixels()) - 1u;
    const uint32_t coverages[] = {0, 1, 127, 128, 254, 255};
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t mixed =
            coverages[i] * 235u + (255u - coverages[i]) * 16u;
        EXPECT_EQ(image.Y(config.osd_left + 1u + i, first_y),
                  static_cast<uint8_t>((mixed + 127u) / 255u));
    }
    const uint32_t second_y = first_y - 1u;
    EXPECT_EQ(image.Y(config.osd_left + 10u - 1u, second_y), 235);
    EXPECT_EQ(image.Y(config.osd_left + 10u, second_y), 16);
    EXPECT_TRUE(image.PaddingEquals(0xCC));
    EXPECT_TRUE(image.GuardsIntact());

    image.Fill(16, 128, 128);
    const Rect clipped_rect = {10, 10, 10, 10};
    osd->DrawText(run, clipped_rect, &output);
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            const bool inside = x >= 10 && x < 20 && y >= 10 && y < 20;
            if (!inside) {
                EXPECT_EQ(image.Y(x, y), 16);
            }
        }
    }

    image.Fill(143, 128, 128);
    osd->DrawText(run, full_rect, &output);
    bool text_over_highlight = false;
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            text_over_highlight = text_over_highlight || image.Y(x, y) > 143;
        }
    }
    EXPECT_TRUE(text_over_highlight);

    return yuvmix_test::Finish();
}
