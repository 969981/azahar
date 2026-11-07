#pragma once
// Contract-only A32 MT return adapter; NO guest patch installed.
// Enabled only by an external integrator AFTER real runtime site/ABI validation.
#include "rng_event_ring.hpp"
#include <cstdint>
namespace rngtrace {
struct Gen6MtProfile {
  uint32_t entry, ret, object_ptr, mapping_gen, title_tag;
};
struct MtEntry {uint32_t pc,r0_object,lr,index_before;};
struct MtExit {uint32_t pc,lr,r0_result,index_after,frame;};
template<uint32_t Capacity> class MtReturnAdapter {
public:
  MtReturnAdapter(Ring<Capacity>& q,Gen6MtProfile p):q_(q),p_(p){}
  void set_runtime_evidence(bool checked,uint32_t mapping,uint32_t title) noexcept {
    enabled_=checked && mapping==p_.mapping_gen && title==p_.title_tag;
    pending_=false;
  }
  void invalidate() noexcept {enabled_=false;pending_=false;}
  bool enabled() const noexcept {return enabled_;}
  uint32_t rejected() const noexcept {return rejected_;}
  bool on_entry(MtEntry e) noexcept {
    if(!enabled_||pending_){++rejected_;return false;}
    if((e.pc!=p_.entry&&e.pc!=p_.entry+4) ||
      e.r0_object!=p_.object_ptr||e.index_before>=624||(e.lr&3u)!=0) {
      ++rejected_;return false;
    }
    entry_=e;pending_=true;return true;
  }
  bool on_return(MtExit e) noexcept {
    if(!enabled_||!pending_){++rejected_;return false;}
    pending_=false;
    if(e.pc!=p_.ret||e.lr!=entry_.lr||
       e.index_after!=(entry_.index_before+1)%624) {
      ++rejected_;return false;
    }
    return on_rng_output(q_,Engine::Gen6Mt,e.frame,entry_.pc,entry_.lr,
                         e.index_after,e.r0_result,entry_.r0_object);
  }
private:
  Ring<Capacity>& q_;
  Gen6MtProfile p_;
  MtEntry entry_{};
  bool enabled_=false,pending_=false;
  uint32_t rejected_=0;
};
}
