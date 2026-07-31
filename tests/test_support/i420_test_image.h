#ifndef YUVMIX_TEST_SUPPORT_I420_TEST_IMAGE_H_
#define YUVMIX_TEST_SUPPORT_I420_TEST_IMAGE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "video/mix_yuv.h"

namespace yuvmix_test {

class OwnedI420 {
public:
    OwnedI420(uint32_t width, uint32_t height, size_t padding);

    yuvmix::I420ImageView ConstView() const;
    yuvmix::MutableI420ImageView MutableView();

    void Fill(uint8_t y, uint8_t u, uint8_t v);
    bool ActivePixelsEqual(uint8_t y, uint8_t u, uint8_t v) const;
    bool GuardsIntact() const;
    bool PaddingEquals(uint8_t value) const;
    std::vector<uint8_t> Snapshot() const;

    uint8_t Y(uint32_t x, uint32_t y) const;
    uint8_t U(uint32_t x, uint32_t y) const;
    uint8_t V(uint32_t x, uint32_t y) const;
    void SetY(uint32_t x, uint32_t y, uint8_t value);
    void SetU(uint32_t x, uint32_t y, uint8_t value);
    void SetV(uint32_t x, uint32_t y, uint8_t value);

private:
    static const size_t kGuardSize = 16;

    struct Plane {
        std::vector<uint8_t> storage;
        size_t row_bytes;
        size_t rows;
        size_t stride;
    };

    static Plane MakePlane(size_t row_bytes, size_t rows, size_t padding);
    static uint8_t* Data(Plane* plane);
    static const uint8_t* Data(const Plane& plane);
    static void FillActive(Plane* plane, uint8_t value);
    static bool ActiveEquals(const Plane& plane, uint8_t value);
    static bool PlaneGuardsIntact(const Plane& plane);
    static bool PlanePaddingEquals(const Plane& plane, uint8_t value);

    uint32_t width_;
    uint32_t height_;
    Plane y_;
    Plane u_;
    Plane v_;
};

}  // namespace yuvmix_test

#endif  // YUVMIX_TEST_SUPPORT_I420_TEST_IMAGE_H_
