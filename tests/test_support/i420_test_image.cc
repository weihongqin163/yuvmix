#include "test_support/i420_test_image.h"

#include <algorithm>

namespace yuvmix_test {

OwnedI420::Plane OwnedI420::MakePlane(size_t row_bytes,
                                      size_t rows,
                                      size_t padding) {
    Plane plane;
    plane.row_bytes = row_bytes;
    plane.rows = rows;
    plane.stride = row_bytes + padding;
    plane.storage.assign(kGuardSize + plane.stride * rows + kGuardSize,
                         0xA5);
    for (size_t row = 0; row < rows; ++row) {
        std::fill(Data(&plane) + row * plane.stride,
                  Data(&plane) + row * plane.stride + plane.stride,
                  0xCC);
    }
    return plane;
}

uint8_t* OwnedI420::Data(Plane* plane) {
    return plane->storage.data() + kGuardSize;
}

const uint8_t* OwnedI420::Data(const Plane& plane) {
    return plane.storage.data() + kGuardSize;
}

void OwnedI420::FillActive(Plane* plane, uint8_t value) {
    for (size_t row = 0; row < plane->rows; ++row) {
        std::fill(Data(plane) + row * plane->stride,
                  Data(plane) + row * plane->stride + plane->row_bytes,
                  value);
    }
}

bool OwnedI420::ActiveEquals(const Plane& plane, uint8_t value) {
    for (size_t row = 0; row < plane.rows; ++row) {
        const uint8_t* begin = Data(plane) + row * plane.stride;
        if (!std::all_of(begin, begin + plane.row_bytes,
                         [value](uint8_t actual) { return actual == value; })) {
            return false;
        }
    }
    return true;
}

bool OwnedI420::PlaneGuardsIntact(const Plane& plane) {
    return std::all_of(plane.storage.begin(),
                       plane.storage.begin() + kGuardSize,
                       [](uint8_t value) { return value == 0xA5; }) &&
           std::all_of(plane.storage.end() - kGuardSize,
                       plane.storage.end(),
                       [](uint8_t value) { return value == 0xA5; });
}

bool OwnedI420::PlanePaddingEquals(const Plane& plane, uint8_t value) {
    for (size_t row = 0; row < plane.rows; ++row) {
        const uint8_t* begin = Data(plane) + row * plane.stride + plane.row_bytes;
        if (!std::all_of(begin, Data(plane) + (row + 1) * plane.stride,
                         [value](uint8_t actual) { return actual == value; })) {
            return false;
        }
    }
    return true;
}

OwnedI420::OwnedI420(uint32_t width, uint32_t height, size_t padding)
    : width_(width),
      height_(height),
      y_(MakePlane(width, height, padding)),
      u_(MakePlane(width / 2, height / 2, padding)),
      v_(MakePlane(width / 2, height / 2, padding)) {}

yuvmix::I420ImageView OwnedI420::ConstView() const {
    yuvmix::I420ImageView view = {};
    view.y = {Data(y_), static_cast<int>(y_.stride), y_.stride * y_.rows};
    view.u = {Data(u_), static_cast<int>(u_.stride), u_.stride * u_.rows};
    view.v = {Data(v_), static_cast<int>(v_.stride), v_.stride * v_.rows};
    view.width = width_;
    view.height = height_;
    return view;
}

yuvmix::MutableI420ImageView OwnedI420::MutableView() {
    yuvmix::MutableI420ImageView view = {};
    view.y = {Data(&y_), static_cast<int>(y_.stride), y_.stride * y_.rows};
    view.u = {Data(&u_), static_cast<int>(u_.stride), u_.stride * u_.rows};
    view.v = {Data(&v_), static_cast<int>(v_.stride), v_.stride * v_.rows};
    view.width = width_;
    view.height = height_;
    return view;
}

void OwnedI420::Fill(uint8_t y, uint8_t u, uint8_t v) {
    FillActive(&y_, y);
    FillActive(&u_, u);
    FillActive(&v_, v);
}

bool OwnedI420::ActivePixelsEqual(uint8_t y, uint8_t u, uint8_t v) const {
    return ActiveEquals(y_, y) && ActiveEquals(u_, u) && ActiveEquals(v_, v);
}

bool OwnedI420::GuardsIntact() const {
    return PlaneGuardsIntact(y_) && PlaneGuardsIntact(u_) &&
           PlaneGuardsIntact(v_);
}

bool OwnedI420::PaddingEquals(uint8_t value) const {
    return PlanePaddingEquals(y_, value) && PlanePaddingEquals(u_, value) &&
           PlanePaddingEquals(v_, value);
}

std::vector<uint8_t> OwnedI420::Snapshot() const {
    std::vector<uint8_t> result;
    result.reserve(y_.storage.size() + u_.storage.size() + v_.storage.size());
    result.insert(result.end(), y_.storage.begin(), y_.storage.end());
    result.insert(result.end(), u_.storage.begin(), u_.storage.end());
    result.insert(result.end(), v_.storage.begin(), v_.storage.end());
    return result;
}

uint8_t OwnedI420::Y(uint32_t x, uint32_t y) const {
    return Data(y_)[static_cast<size_t>(y) * y_.stride + x];
}

uint8_t OwnedI420::U(uint32_t x, uint32_t y) const {
    return Data(u_)[static_cast<size_t>(y) * u_.stride + x];
}

uint8_t OwnedI420::V(uint32_t x, uint32_t y) const {
    return Data(v_)[static_cast<size_t>(y) * v_.stride + x];
}

void OwnedI420::SetY(uint32_t x, uint32_t y, uint8_t value) {
    Data(&y_)[static_cast<size_t>(y) * y_.stride + x] = value;
}

void OwnedI420::SetU(uint32_t x, uint32_t y, uint8_t value) {
    Data(&u_)[static_cast<size_t>(y) * u_.stride + x] = value;
}

void OwnedI420::SetV(uint32_t x, uint32_t y, uint8_t value) {
    Data(&v_)[static_cast<size_t>(y) * v_.stride + x] = value;
}

}  // namespace yuvmix_test
