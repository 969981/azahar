// Native trace bridge: deterministic host tests, no ROM in CI.
#include "core/arm/dynarmic/experimental_rng/mt_jit_trace_bridge.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace Core::NativeMtTrace;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
#ifdef _WIN32
    _putenv_s("AZAHAR_RNG_TRACE_PROFILE","X15");
    _putenv_s("AZAHAR_RNG_TRACE_FILE",argv[1]);
#else
    setenv("AZAHAR_RNG_TRACE_PROFILE","X15",1);
    setenv("AZAHAR_RNG_TRACE_FILE",argv[1],1);
#endif
    const auto read32=[](std::uint32_t a) -> std::uint32_t {
        for (auto [addr,opcode]:kCodeWords) if(a==addr) return opcode;
        return 0;
    };
    {
        Bridge b(0);
        assert(b.Requested()&&!b.Verified());
        assert(!b.Verify([&](std::uint32_t a){ return a==kBody?0:read32(a); }));
        assert(!b.Verified());
        assert(b.Verify(read32)&&b.Verified());
        b.Entry(kObject+4,0x300010,0);b.Return(17,0x300010,1);
        for (std::uint32_t i=0;i<800;i++) {
            b.Entry(kObject,0x300010,i%624);
            b.Return(i*17,0x300010,(i+1)%624);
        }
        b.Entry(kObject,0x300010,12);
        b.Return(100,0x300010,99); // index mismatch; reject
        b.Drain(true);
        assert(b.Written()==800&&b.Dropped()==0&&b.Rejected()>=1);
        b.MappingChanged();
        b.Entry(kObject,0x300010,2);b.Return(50,0x300010,3);
        b.Drain(true);
        assert(b.Written()==800);
    }
    std::ifstream f(std::string(argv[1])+".core0.rngtrace",std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(f)),{});
    assert(bytes.size()==24+36*800);
    assert(bytes.substr(0,8)=="RNGTRACE");
    std::cout<<"PASS native bridge 800 events; verified code signature and mapping invalidation\n";
}
