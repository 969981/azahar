#pragma once
// EXPERIMENTAL: read-only Gen6 X 1.5 MT output JIT observer.
// Nothing is active unless both environment variables are set.
// No guest code writes; guest callback only queues fixed-size records.
#include "mt_return_adapter.hpp"
#include "rng_trace_io.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
namespace Core::NativeMtTrace {
constexpr std::uint32_t kBody=0x00119F88, kReturn=0x0011A010;
constexpr std::uint32_t kObject=0x08C52848;
constexpr std::array<std::pair<std::uint32_t,std::uint32_t>,10> kCodeWords{{
    {0x0010869C,0x52563068}, {0x001086A0,0x59324271},
    {0x001086A4,0x33716745}, {0x001086A8,0x4F767A39},
    {0x00119F84,0xE1A00000}, {kBody,0xE92D0030},
    {kReturn,0xE12FFF1E}, {0x0011A014,0x9908B0DF},
    {0x0011A018,0x9D2C56FF}, {0x0011A01C,0xEFC67FFF}
}};
class Bridge {
public:
    explicit Bridge(std::uint32_t core)
        : adapter_(ring_,{0x00119F84,kReturn,kObject,1,0x5815}) {
        const char* p=std::getenv("AZAHAR_RNG_TRACE_PROFILE");
        const char* f=std::getenv("AZAHAR_RNG_TRACE_FILE");
        requested_=p && std::strcmp(p,"X15")==0 && f && *f;
        if(requested_) path_=std::string(f)+".core"+std::to_string(core)+".rngtrace";
    }
    ~Bridge() { Drain(true); }
    Bridge(const Bridge&)=delete;
    Bridge& operator=(const Bridge&)=delete;
    bool Requested() const noexcept {return requested_;}
    bool Verified() const noexcept {return verified_;}
    template<class Read32> bool Verify(Read32&& read32) {
        if(!requested_) return false;
        for(const auto& [pc,opcode]:kCodeWords) {
            if(read32(pc)!=opcode) {
                verified_=false; pending_=false;adapter_.invalidate();return false;
            }
        }
        verified_=true;
        adapter_.set_runtime_evidence(true,1,0x5815); // opt-in research gate + live code-word check
        return true;
    }
    void MappingChanged() noexcept {
        pending_=false;verified_=false;adapter_.invalidate();
    }
    // Called from Dynarmic generated host code, bounded and wait-free.
    void Entry(std::uint32_t obj,std::uint32_t lr,std::uint32_t index) noexcept {
        if(!verified_ || obj!=kObject || pending_) return;
        pending_=adapter_.on_entry({kBody,obj,lr,index});
    }
    void Return(std::uint32_t result,std::uint32_t lr,std::uint32_t index) noexcept {
        if(!verified_ || !pending_) return;
        pending_=false;
        adapter_.on_return({kReturn,lr,result,index,0}); // frame unavailable; field = 0
    }
    // Run only when the Dynarmic JIT returns to its emulator host loop.
    void Drain(bool force=false) {
        if(!requested_ || (!verified_ && !stream_.is_open()))return;
        const auto now=std::chrono::steady_clock::now();
        if(!force && ring_.queued()<128 && now-last_flush_<std::chrono::milliseconds(150))return;
        if(!stream_.is_open()) {
            stream_.open(path_,std::ios::out|std::ios::binary|std::ios::trunc);
            if(!stream_) {requested_=false;return;}
            WriteHeader();
        }
        rngtrace::Event e;
        while(ring_.take(e)) {rngtrace::save_event(stream_,e);++written_;}
        if(force || now-last_flush_>=std::chrono::milliseconds(150)) {
            WriteHeader();stream_.flush();last_flush_=now;
        }
    }
    std::uint32_t Dropped() const noexcept{return ring_.dropped();}
    std::uint32_t Written() const noexcept{return written_;}
    std::uint32_t Rejected() const noexcept{return adapter_.rejected();}
private:
    void WriteHeader() {
        stream_.seekp(0,std::ios::beg);
        stream_.write("RNGTRACE",8);
        rngtrace::put16(stream_,1);rngtrace::put16(stream_,36);
        rngtrace::put32(stream_,written_);
        rngtrace::put32(stream_,ring_.attempted());
        rngtrace::put32(stream_,ring_.dropped());
        stream_.seekp(0,std::ios::end);
    }
    rngtrace::Ring<8192> ring_{};
    rngtrace::MtReturnAdapter<8192> adapter_;
    std::ofstream stream_{};
    std::string path_{};
    std::chrono::steady_clock::time_point last_flush_=std::chrono::steady_clock::now();
    bool requested_=false,verified_=false,pending_=false;
    std::uint32_t written_=0;
};
} // namespace Core::NativeMtTrace
