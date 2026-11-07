#pragma once
// Trace snapshot writer; call ONLY from a non-game execution / offline exporter.
// Version 1: 24-byte header, 36-byte little-endian records.
#include "rng_event_ring.hpp"
#include <ostream>
#include <cstdint>
namespace rngtrace {
inline void put16(std::ostream& o, uint16_t v) { for (int i=0;i<2;++i) o.put(char(v >> (8*i))); }
inline void put32(std::ostream& o, uint32_t v) { for (int i=0;i<4;++i) o.put(char(v >> (8*i))); }
inline void put64(std::ostream& o, uint64_t v) { for (int i=0;i<8;++i) o.put(char(v >> (8*i))); }
inline void save_event(std::ostream& o, const Event& e) {
    put32(o,e.sequence); put32(o,e.emu_frame); put32(o,e.guest_pc);
    put32(o,e.caller_lr); put32(o,e.rng_index); put64(o,e.output);
    put32(o,e.context); put16(o,e.flags); o.put(char(e.engine)); o.put(char(e.kind));
}
// Pause the producer or use a consumer-owned draining exporter.
template<uint32_t C>
uint32_t save_snapshot(Ring<C>& ring, std::ostream& o) {
    uint32_t count = ring.queued();
    o.write("RNGTRACE", 8);
    put16(o,1); put16(o,36); put32(o,count);
    put32(o,ring.attempted()); put32(o,ring.dropped());
    for(uint32_t i=0; i<count; ++i) {
        Event e;
        if (!ring.take(e)) return i;
        save_event(o,e);
    }
    return count;
}
}
