#pragma once
#include <cmath>
#include <cstdint>

namespace distortion {
// Some hosts round positions, or update their timestamp less often than audio.
// Neither is a seek. Accumulate the predicted position across repeated stamps.
class TransportTracker {
public:
    void reset() noexcept { known=false; wasPlaying=false; expected=last=0; }
    bool update(std::int64_t position,bool playing,int samples) noexcept {
        bool discontinuity=known && playing!=wasPlaying;
        if (known && playing && wasPlaying && position!=last)
            discontinuity=std::abs(static_cast<long double>(position)-expected)>2.0L;
        if (known && playing && wasPlaying && position==last) expected+=samples;
        else expected=position+samples;
        last=position; wasPlaying=playing; known=true;
        return discontinuity;
    }
private:
    bool known=false,wasPlaying=false;
    std::int64_t expected=0,last=0;
};
}
