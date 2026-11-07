#pragma once
// Independent prototype: no PokeReader source code copied.
// Single-producer / single-consumer, bounded, wait-free hot path.
// The producer MUST be the emulated game execution thread, never two threads.
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
namespace rngtrace {
enum class Engine : uint8_t { Gen6Mt = 1, Gen6TinyMt = 2, Gen7Sfmt = 3, Gen7EggTinyMt = 4, Gen7SosSfmt = 5 };
enum class Kind : uint8_t { Draw = 1, FrameSample = 2, SeedInit = 3, Reset = 4 };
struct Event {
    uint32_t sequence = 0;
    uint32_t emu_frame = 0;
    uint32_t guest_pc = 0;
    uint32_t caller_lr = 0;
    uint32_t rng_index = 0;
    uint64_t output = 0;
    uint32_t context = 0;
    uint16_t flags = 0;
    Engine engine = Engine::Gen6Mt;
    Kind kind = Kind::FrameSample;
};
static_assert(std::is_trivially_copyable<Event>::value, "Event must be POD");
template <uint32_t Capacity> class Ring {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity power of two");
public:
    bool emit(Event event) noexcept {
        event.sequence = attempted_++;
        const uint32_t w = write_.load(std::memory_order_relaxed);
        const uint32_t r = read_.load(std::memory_order_acquire);
        if (static_cast<uint32_t>(w - r) >= Capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[w & (Capacity - 1)] = event;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool take(Event& out) noexcept {
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        out = slots_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    uint32_t queued() const noexcept {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
    }
    uint32_t attempted() const noexcept { return attempted_; } // producer-thread only
private:
    alignas(64) std::array<Event, Capacity> slots_{};
    alignas(64) std::atomic<uint32_t> write_{0};
    alignas(64) std::atomic<uint32_t> read_{0};
    alignas(64) std::atomic<uint32_t> dropped_{0};
    uint32_t attempted_ = 0;
};
template <uint32_t C>
inline bool on_rng_output(Ring<C>& ring, Engine engine, uint32_t frame,
                          uint32_t pc, uint32_t lr, uint32_t index,
                          uint64_t output, uint32_t context = 0) noexcept {
    Event e{}; e.kind = Kind::Draw; e.engine = engine;
    e.emu_frame = frame; e.guest_pc = pc; e.caller_lr = lr;
    e.rng_index = index; e.output = output; e.context = context;
    return ring.emit(e);
}
template <uint32_t C>
inline bool on_frame_sample(Ring<C>& ring, Engine engine, uint32_t frame,
                            uint32_t index, uint64_t sampled_word) noexcept {
    Event e{}; e.kind = Kind::FrameSample; e.engine = engine;
    e.emu_frame = frame; e.rng_index = index; e.output = sampled_word;
    return ring.emit(e);
}
} // namespace rngtrace
