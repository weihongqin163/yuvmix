#include <cstddef>

#include "test_support/test_assert.h"
#include "video/i420_geometry.h"

namespace {

bool PlansEqual(const yuvmix::GeometryPlan& left,
                const yuvmix::GeometryPlan& right) {
    return left.crop_x == right.crop_x &&
           left.crop_y == right.crop_y &&
           left.crop_w == right.crop_w &&
           left.crop_h == right.crop_h &&
           left.dest_x == right.dest_x &&
           left.dest_y == right.dest_y &&
           left.dest_w == right.dest_w &&
           left.dest_h == right.dest_h &&
           left.use_copy == right.use_copy;
}

struct GeometryCase {
    uint32_t source_width;
    uint32_t source_height;
    uint32_t target_width;
    uint32_t target_height;
    yuvmix::FillMode mode;
    yuvmix::GeometryPlan expected;
};

}  // namespace

int main() {
    using namespace yuvmix;

    const GeometryCase cases[] = {
        {640, 360, 1280, 720, FillMode::kContain,
         {0, 0, 640, 360, 320, 180, 640, 360, true}},
        {1920, 1080, 640, 480, FillMode::kContain,
         {0, 0, 1920, 1080, 0, 60, 640, 360, false}},
        {1920, 1080, 640, 480, FillMode::kCover,
         {240, 0, 1440, 1080, 0, 0, 640, 480, false}},
        {640, 1080, 1280, 720, FillMode::kCover,
         {0, 360, 640, 360, 0, 0, 1280, 720, false}},
        {640, 480, 640, 480, FillMode::kContain,
         {0, 0, 640, 480, 0, 0, 640, 480, true}},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        GeometryPlan actual = {};
        EXPECT_EQ(BuildGeometryPlan(cases[i].source_width,
                                    cases[i].source_height,
                                    cases[i].target_width,
                                    cases[i].target_height,
                                    cases[i].mode,
                                    &actual),
                  MixYuvStatus::kOk);
        EXPECT_TRUE(PlansEqual(actual, cases[i].expected));
    }

    GeometryPlan plan = {};
    EXPECT_EQ(BuildGeometryPlan(0, 480, 640, 480,
                                FillMode::kContain, &plan),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(BuildGeometryPlan(641, 480, 640, 480,
                                FillMode::kContain, &plan),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(BuildGeometryPlan(640, 480, 639, 480,
                                FillMode::kCover, &plan),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(BuildGeometryPlan(640, 480, 640, 480,
                                static_cast<FillMode>(99), &plan),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(BuildGeometryPlan(2, 65534, 65534, 2,
                                FillMode::kContain, &plan),
              MixYuvStatus::kInvalidArgument);
    EXPECT_EQ(BuildGeometryPlan(640, 480, 640, 480,
                                FillMode::kContain, NULL),
              MixYuvStatus::kInvalidArgument);

    return yuvmix_test::Finish();
}
