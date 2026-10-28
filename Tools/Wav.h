#pragma once
#include "Core/Types.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace distortion {
struct Wav { Channels channels; unsigned sampleRate=0; };
inline Wav readWav(const std::string& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open WAV: "+path);
    const auto length=stream.tellg();
    if (length<12 || length>std::streamoff(1024)*1024*1024) throw std::runtime_error("WAV size is unsupported");
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    stream.seekg(0); stream.read(reinterpret_cast<char*>(data.data()),length);
    if (!stream || std::memcmp(data.data(),"RIFF",4)!=0 || std::memcmp(data.data()+8,"WAVE",4)!=0)
        throw std::runtime_error("Expected little-endian RIFF/WAVE (RF64 and RIFX are unsupported)");
    const auto u16=[&](std::size_t p) { return static_cast<unsigned>(data.at(p))|(static_cast<unsigned>(data.at(p+1))<<8); };
    const auto u32=[&](std::size_t p) { return static_cast<std::uint32_t>(u16(p))|(static_cast<std::uint32_t>(u16(p+2))<<16); };
    unsigned format=0,nch=0,rate=0,alignment=0,bits=0;
    std::size_t audioAt=0,audioBytes=0;
    for (std::size_t p=12;p+8<=data.size();) {
        const auto bytes=static_cast<std::size_t>(u32(p+4));
        const auto payload=p+8;
        if (bytes>data.size()-payload) throw std::runtime_error("Truncated WAV chunk");
        if (std::memcmp(data.data()+p,"fmt ",4)==0) {
            if (bytes<16) throw std::runtime_error("Invalid WAV fmt chunk");
            format=u16(payload); nch=u16(payload+2); rate=u32(payload+4); alignment=u16(payload+12); bits=u16(payload+14);
            if (format==0xfffe) {
                if (bytes<40 || u16(payload+16)<22) throw std::runtime_error("Invalid extensible WAV");
                format=u16(payload+24);
                const unsigned char tail[]{0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if (std::memcmp(data.data()+payload+26,tail,14)!=0) throw std::runtime_error("Unsupported extensible WAV subformat");
                if (u16(payload+18)!=bits) throw std::runtime_error("WAV valid-bit packing is unsupported");
            }
        } else if (std::memcmp(data.data()+p,"data",4)==0 && audioAt==0) { audioAt=payload; audioBytes=bytes; }
        p=payload+bytes+(bytes&1);
    }
    if (nch<1 || nch>2 || rate<8000 || rate>384000 || audioAt==0) throw std::runtime_error("WAV must be mono/stereo, 8–384 kHz, with a data chunk");
    const bool pcm=format==1 && (bits==16 || bits==24 || bits==32);
    const bool floating=format==3 && (bits==32 || bits==64);
    if ((!pcm && !floating) || alignment!=nch*(bits/8) || audioBytes%alignment!=0) throw std::runtime_error("WAV encoding must be PCM16/24/32 or float32/64 with complete frames");
    const auto frames=audioBytes/alignment;
    Wav wav; wav.sampleRate=rate; wav.channels.assign(nch,std::vector<double>(frames));
    for (std::size_t i=0;i<frames;++i) for (unsigned c=0;c<nch;++c) {
        const auto p=audioAt+i*alignment+c*(bits/8);
        double value=0;
        if (pcm) {
            std::uint32_t packed=0;
            for (unsigned k=0;k<bits/8;++k) packed|=static_cast<std::uint32_t>(data[p+k])<<(8*k);
            const auto sign=std::uint32_t(1)<<(bits-1);
            const auto signedValue=static_cast<std::int64_t>(packed&(~sign))-static_cast<std::int64_t>(packed&sign);
            value=static_cast<double>(signedValue)/static_cast<double>(sign);
        } else if (bits==32) {
            const auto packed=u32(p); float sample=0; std::memcpy(&sample,&packed,4); value=sample;
        } else {
            const auto packed=static_cast<std::uint64_t>(u32(p))|(static_cast<std::uint64_t>(u32(p+4))<<32);
            std::memcpy(&value,&packed,8);
        }
        if (!std::isfinite(value)) throw std::runtime_error("Non-finite WAV sample");
        wav.channels[c][i]=value;
    }
    return wav;
}
}
