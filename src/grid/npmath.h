// numpy's reductions where their arithmetic order reaches a result (PLAN.md §4.3): np.sum / ndarray.sum of a 1-D
// float array (numpy's pairwise sum — NOT Python's compensated sum, NOT a plain loop), np.quantile (method "linear", float64
// interpolation as numpy does it), np.searchsorted(side="right"), np.cumsum.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace grid {

// numpy's pairwise_sum (umath loops): straight below 8 values, 8 accumulators up to 128, halves above
template <typename T>
double pairwise_sum(const T *a, std::size_t n)
{
    if (n < 8) {
        double res = 0.;
        for (std::size_t i = 0; i < n; ++i)
            res += static_cast<double>(a[i]);
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (int j = 0; j < 8; ++j)
            r[j] = static_cast<double>(a[j]);
        std::size_t i = 8;
        for (; i < n - (n % 8); i += 8)
            for (int j = 0; j < 8; ++j)
                r[j] += static_cast<double>(a[i + j]);
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i)
            res += static_cast<double>(a[i]);
        return res;
    }
    std::size_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise_sum(a, n2) + pairwise_sum(a + n2, n - n2);
}

// a.sum() of a float64 1-D array: numpy's identity 0.0 plus the pairwise sum of all the values (measured against
// numpy 2.5.3: sum([-0.0]) is 0.0; the first value is not taken apart)
inline double np_sum(const std::vector<double> &a)
{
    return 0.0 + pairwise_sum(a.data(), a.size());
}

// a float32 array's .sum(): the same pairwise sum with float32 accumulators (numpy's FLOAT_pairwise_sum)
inline float pairwise_sum_f(const float *a, std::size_t n)
{
    if (n < 8) {
        float res = -0.0f;
        for (std::size_t i = 0; i < n; ++i)
            res += a[i];
        return res;
    }
    if (n <= 128) {
        float r[8];
        for (int j = 0; j < 8; ++j)
            r[j] = a[j];
        std::size_t i = 8;
        for (; i < n - (n % 8); i += 8)
            for (int j = 0; j < 8; ++j)
                r[j] += a[i + j];
        float res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i)
            res += a[i];
        return res;
    }
    std::size_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise_sum_f(a, n2) + pairwise_sum_f(a + n2, n - n2);
}
inline float np_sum_f(const std::vector<float> &a) { return 0.0f + pairwise_sum_f(a.data(), a.size()); }
// .mean() of a float32 array: the float32 sum over the count, in float32
inline float np_mean_f(const std::vector<float> &a) { return np_sum_f(a) / static_cast<float>(a.size()); }

inline std::vector<double> np_cumsum(const std::vector<double> &a)
{
    std::vector<double> out(a.size());
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        s = i ? s + a[i] : a[i];
        out[i] = s;
    }
    return out;
}

// np.quantile(values, qs) with method "linear" (numpy's _quantile / _lerp): the values' own type for previous / next
// (T), the interpolation in float64
template <typename T>
std::vector<double> np_quantile(std::vector<T> values, const std::vector<double> &qs)
{
    std::sort(values.begin(), values.end());
    const double n = static_cast<double>(values.size());
    const long long cnt = static_cast<long long>(values.size());
    std::vector<double> out;
    for (double q : qs) {
        const double virt = (n - 1) * q;             // _QuantileMethods["linear"]: (n - 1) * quantiles
        double prev = std::floor(virt), next = prev + 1;
        if (virt >= static_cast<double>(cnt - 1)) {
            prev = -1;
            next = -1;
        }
        if (virt < 0) {
            prev = 0;
            next = 0;
        }
        if (std::isnan(virt)) {
            prev = -1;
            next = -1;
        }
        const long long pi = static_cast<long long>(prev), ni = static_cast<long long>(next);
        const T a = values[static_cast<std::size_t>(pi < 0 ? pi + cnt : pi)];
        const T b = values[static_cast<std::size_t>(ni < 0 ? ni + cnt : ni)];
        const double t = virt - prev;                                         // _get_gamma
        const T diff_b_a = static_cast<T>(b - a);
        double r = static_cast<double>(a) + static_cast<double>(diff_b_a) * t;
        if (t >= 0.5)
            r = static_cast<double>(b) - static_cast<double>(diff_b_a) * (1 - t);
        out.push_back(r);
    }
    return out;
}

// np.searchsorted(cuts, v, side="right"): the number of cuts <= v
inline long long searchsorted_right(const std::vector<double> &cuts, double v)
{
    return static_cast<long long>(std::upper_bound(cuts.begin(), cuts.end(), v) - cuts.begin());
}

}  // namespace grid
