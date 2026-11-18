// layertools.py — Pixel helpers for painting terrain.bmp / rivers.bmp: deterministic noise, ring depth, 4-connected
// lines, discs. The noise is float32 as numpy computes it (Python floats taken as float32: numpy 2's weak scalars).
#pragma once
#include "grid/grid.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace layertools {

std::vector<float> _hash01(const std::vector<std::int64_t> &gx, const std::vector<std::int64_t> &gy, long long seed);
std::vector<float> value_noise(const std::vector<std::int64_t> &ys, const std::vector<std::int64_t> &xs, double scale,
                               long long seed);
std::vector<float> patch_noise(const std::vector<std::int64_t> &ys, const std::vector<std::int64_t> &xs, long long seed,
                               double scale = 9.0);
grid::Grid<std::int32_t> depth(const grid::Grid<std::uint8_t> &mask);
std::vector<std::int64_t> ring_levels(const std::vector<std::int32_t> &d, long long n, std::optional<long long> width = {},
                                      const std::vector<float> *wobble = nullptr);
std::vector<std::int64_t> weighted_levels(const std::vector<float> &values, const std::vector<double> &weights);
std::vector<std::pair<long long, long long>> line4(long long x0, long long y0, long long x1, long long y1);
std::vector<std::int64_t> disc(double x, double y, double size, long long w, long long h);

}  // namespace layertools
