#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace distortion {
constexpr double pi = 3.1415926535897932384626433832795;
using Complex = std::complex<double>;
inline std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Worker-thread FFT. Plans and scratch storage never reach the audio callback.
class FFT {
public:
    explicit FFT(std::size_t size) : n(size), reversed(size), roots(size / 2) {
        if (n < 2 || (n & (n - 1)) != 0) throw std::invalid_argument("FFT size");
        for (std::size_t i = 0, j = 0; i < n; ++i) {
            reversed[i] = j;
            std::size_t bit = n >> 1;
            for (; bit && (j & bit); bit >>= 1) j ^= bit;
            j ^= bit;
        }
        for (std::size_t i = 0; i < n / 2; ++i)
            roots[i] = std::polar(1.0, -2.0 * pi * static_cast<double>(i) / static_cast<double>(n));
    }
    void transform(std::vector<Complex>& a, bool inverse = false) const {
        if (a.size() != n) throw std::invalid_argument("FFT buffer");
        for (std::size_t i = 0; i < n; ++i)
            if (i < reversed[i]) std::swap(a[i], a[reversed[i]]);
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const auto half = len >> 1, stride = n / len;
            for (std::size_t i = 0; i < n; i += len)
                for (std::size_t j = 0; j < half; ++j) {
                    const auto w = inverse ? std::conj(roots[j * stride]) : roots[j * stride];
                    const auto v = a[i + j + half] * w, u = a[i + j];
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
        if (inverse) for (auto& v : a) v /= static_cast<double>(n);
    }
    const std::size_t n;
private:
    std::vector<std::size_t> reversed;
    std::vector<Complex> roots;
};
}
