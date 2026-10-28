#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace distortion {
using Channels = std::vector<std::vector<double>>;
enum class Status : int { measuring, valid, noReference, silence, insufficient, unrelated, overload, invalid };
enum class Method : int { residual, harmonicTHD };
inline const char* statusName(Status s) {
    switch (s) {
        case Status::measuring: return "measuring";
        case Status::valid: return "valid";
        case Status::noReference: return "no_reference";
        case Status::silence: return "silence";
        case Status::insufficient: return "insufficient_excitation";
        case Status::unrelated: return "unrelated_or_unmodelled";
        case Status::overload: return "capture_overflow";
        case Status::invalid: return "invalid_input";
    }
    return "invalid_input";
}
struct Result {
    Status status = Status::measuring;
    Method method = Method::residual;
    double percent = 0.0;
    double errorRms = 0.0, outputRms = 0.0;
    double trainingPercent = 0.0, correlation = 0.0, convergence = 0.0;
    double predictionCorrelationSquared = 0.0, frequencyHz = 0.0;
    int delaySamples = 0, harmonics = 0, iterations = 0, inputRank = 0;
    bool valid() const { return status == Status::valid && std::isfinite(percent); }
};
struct Config {
    explicit Config(double rate = 48000.0) : sampleRate(rate) {
        if (!std::isfinite(rate) || rate < 8000.0 || rate > 384000.0) sampleRate = 48000.0;
        taps = 512;
        while (static_cast<double>(taps) < 2048.0 * sampleRate / 48000.0) taps <<= 1;
        train = 16384;
        while (static_cast<double>(train) < 0.68 * sampleRate) train <<= 1;
        validation = train / 2;
        maxDelay = static_cast<int>(std::min(32768.0, 0.25 * sampleRate));
    }
    std::size_t requiredSamples() const { return train + validation + 3 * taps + 2 * static_cast<std::size_t>(maxDelay) + 32; }
    double sampleRate;
    std::size_t taps = 1024, train = 32768, validation = 16384;
    int maxDelay = 12000;
    int maxIterations = 40;
};
}
