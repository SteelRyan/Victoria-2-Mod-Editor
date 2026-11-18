// Grid<T>: what replaces a 2-D numpy array (PLAN.md §4.3) — height × width, one block of memory, row-major, the
// element type exactly the numpy dtype (bool arrays as uint8 0 / 1). Arithmetic on elements is written out where it
// stands, cast back to T after every operation (numpy keeps the width: uint8 + uint8 wraps).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace grid {

template <typename T>
class Grid {
public:
    Grid() = default;
    Grid(std::ptrdiff_t h, std::ptrdiff_t w, T fill = T()) : h_(h), w_(w), v_(static_cast<size_t>(h * w), fill) {}
    std::ptrdiff_t h() const { return h_; }
    std::ptrdiff_t w() const { return w_; }
    T &operator()(std::ptrdiff_t y, std::ptrdiff_t x) { return v_[static_cast<size_t>(y * w_ + x)]; }
    const T &operator()(std::ptrdiff_t y, std::ptrdiff_t x) const { return v_[static_cast<size_t>(y * w_ + x)]; }
    T *row(std::ptrdiff_t y) { return v_.data() + y * w_; }
    const T *row(std::ptrdiff_t y) const { return v_.data() + y * w_; }
    std::vector<T> &flat() { return v_; }
    const std::vector<T> &flat() const { return v_; }
    bool empty() const { return v_.empty(); }
    bool operator==(const Grid &o) const = default;

private:
    std::ptrdiff_t h_ = 0, w_ = 0;
    std::vector<T> v_;
};

}  // namespace grid
