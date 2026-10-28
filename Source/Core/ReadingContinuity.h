#pragma once
#include "Types.h"
#include <chrono>
#include <cstdint>

namespace distortion {
class ReadingContinuity {
public:
    using Clock=std::chrono::steady_clock;
    Result update(const Result& result,std::uint64_t epoch,Clock::time_point now=Clock::now()) {
        if (result.valid()) { previous=result; previousEpoch=epoch; acquired=now; return result; }
        const bool retry=result.status==Status::measuring || result.status==Status::insufficient;
        if (retry && previous.valid() && previousEpoch==epoch && now-acquired<=std::chrono::seconds(2))
            return previous;
        previous=Result{};
        return result;
    }
private:
    Result previous;
    std::uint64_t previousEpoch=0;
    Clock::time_point acquired{};
};
}
