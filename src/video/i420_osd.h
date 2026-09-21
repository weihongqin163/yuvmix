#ifndef YUVMIX_VIDEO_I420_OSD_H_
#define YUVMIX_VIDEO_I420_OSD_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "video/freetype_osd.h"
#include "video/mix_yuv.h"

namespace yuvmix {

struct OsdIconPlan {
  I420ImageView image;
  uint32_t source_x;
  uint32_t source_y;
  Rect destination;
  uint8_t alpha;
};

struct OsdPlan {
  OsdIconPlan icons[3];
  size_t icon_count;
  TextRun text;
  Rect clip;
  int64_t text_pen_x;
  int64_t baseline_y;
};

class OsdRenderer {
public:
  static MixYuvStatus Create(const MixYuvConfig &config,
                             std::unique_ptr<OsdRenderer> *renderer);
  ~OsdRenderer();

  MixYuvStatus Prepare(const MixSource &source, OsdPlan *plan);
  void Draw(const OsdPlan &plan, MutableI420ImageView *output) const;
  size_t glyph_count() const;

  OsdRenderer(const OsdRenderer &) = delete;
  OsdRenderer &operator=(const OsdRenderer &) = delete;

private:
  struct Impl;
  explicit OsdRenderer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace yuvmix

#endif // YUVMIX_VIDEO_I420_OSD_H_
