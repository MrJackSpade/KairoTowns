// Skips the passes of a guest wait loop that cannot change anything, exactly.
//
// A game that waits for an interrupt by polling a RAM flag runs the same few instructions over
// and over; on the RG DS that busy-wait costs more than a frame (Mahou Daisakusen, KairoTowns#6).
// Between two fast device pollings only the CPU runs: timers, sound, VSYNC and interrupts act at
// the polling (FMTownsCommon::RunFastDevicePollingInternal), or through I/O and memory the CPU
// touches. So when a pass of a loop starts at the same place as the last pass, with the whole CPU
// state equal and nothing in between that may have an effect (a store, an I/O access, a read from
// anything but RAM; Memory::sideEffects and InOut::accesses count them), every later pass until
// the polling is the same pass again. The skip moves guest time over as many whole passes as end
// before the polling, with Tsugaru's own clock arithmetic, and the CPU then runs on from the same
// instruction with the same state and time as it would have reached. Nothing else changes.

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "towns.h"

namespace kairotowns {

template <class TownsType> class WaitLoopSkip {
public:
    // What the skip did since the machine started.
    struct Counts {
        uint64_t skips = 0, passes = 0, instructions = 0;
    };

    // A new stretch of CPU-only time begins: what came before it says nothing about what follows.
    void restart() { candidate_ = false; }

    // Before the CPU runs the instruction at CS:EIP, after `instructions` instructions and `clocks`
    // clocks in all. May move guest time on over whole repeated passes, never past `deadline`.
    // Inline, as it runs before every instruction: only a backward jump in the same code segment
    // starts a pass.
    inline __attribute__((always_inline)) void before_instruction(TownsType &towns, uint64_t instructions,
                                                                  uint64_t clocks, uint64_t deadline) {
        const auto &state = towns.CPU().state;
        const uint32_t eip = state.EIP;
        const uint16_t cs = state.CS().value;
        const bool pass_start = cs == last_cs_ && eip < last_eip_;
        last_cs_ = cs;
        last_eip_ = eip;
        if (pass_start) at_pass_start(towns, cs, eip, instructions, clocks, deadline);
    }

    const Counts &counts() const { return counts_; }

private:
    __attribute__((noinline)) void at_pass_start(TownsType &towns, uint16_t cs, uint32_t eip, uint64_t instructions,
                                                 uint64_t clocks, uint64_t deadline) {
        const auto &state = towns.CPU().state;
        const uint64_t effects = towns.mem.sideEffects + towns.io.accesses;
        // Only a pass that starts where the last one did, with nothing done in between, can be a
        // repeat; the state is captured for such a pass alone, so code that only loops now and then
        // costs no copies.
        const bool again = cs == pass_cs_ && eip == pass_eip_ && effects == pass_effects_;
        if (again && candidate_) {
            snapshot(state, scratch_);
            if (scratch_ == pass_state_) {
                skip(towns, instructions - pass_instructions_, clocks - pass_clocks_, deadline);
                // The time left is less than a pass; the CPU runs it.
                candidate_ = false;
                return;
            }
            std::swap(pass_state_, scratch_);
        } else if (again) {
            snapshot(state, pass_state_);
            candidate_ = true;
        } else {
            candidate_ = false;
        }
        pass_cs_ = cs;
        pass_eip_ = eip;
        pass_effects_ = effects;
        pass_instructions_ = instructions;
        pass_clocks_ = clocks;
    }

    using CpuState = typename std::remove_reference<decltype(std::declval<TownsType &>().CPU().state)>::type;

    // Every part of the CPU state an instruction can read, as bytes. Bytes that differ while the
    // values are equal (padding) only cost a skip, never make a wrong one.
    struct Snapshot {
        static constexpr size_t kBytes = sizeof(CpuState::NULL_and_reg32) + sizeof(CpuState::mode) +
            sizeof(CpuState::EIP) + sizeof(CpuState::EFLAGS) + sizeof(CpuState::sreg) + sizeof(CpuState::GDTR) +
            sizeof(CpuState::IDTR) + sizeof(CpuState::LDTR) + sizeof(CpuState::TR) + 4 * sizeof(unsigned int) +
            sizeof(CpuState::DR) + sizeof(CpuState::TEST) + sizeof(CpuState::fpuState) + sizeof(CpuState::halt) +
            sizeof(CpuState::holdIRQ) + sizeof(CpuState::exception) + sizeof(CpuState::exceptionCode) +
            sizeof(CpuState::exceptionType) + sizeof(CpuState::exceptionLinearAddr);
        uint8_t bytes[kBytes];
        size_t used = 0;

        template <class T> void put(const T &value) {
            std::memcpy(bytes + used, &value, sizeof(T));
            used += sizeof(T);
        }
        bool operator==(const Snapshot &other) const { return std::memcmp(bytes, other.bytes, kBytes) == 0; }
    };

    static void snapshot(const CpuState &state, Snapshot &out) {
        out.used = 0;
        out.put(state.NULL_and_reg32);
        out.put(state.mode);
        out.put(state.EIP);
        out.put(state.EFLAGS);
        out.put(state.sreg);
        out.put(state.GDTR);
        out.put(state.IDTR);
        out.put(state.LDTR);
        out.put(state.TR);
        for (unsigned i = 0; i < 4; ++i) out.put(static_cast<unsigned int>(state.GetCR(i)));
        out.put(state.DR);
        out.put(state.TEST);
        out.put(state.fpuState);
        out.put(state.halt);
        out.put(state.holdIRQ);
        out.put(state.exception);
        out.put(state.exceptionCode);
        out.put(state.exceptionType);
        out.put(state.exceptionLinearAddr);
    }

    // Moves guest time over the whole passes of `clocks` clocks that end by `deadline`, as
    // FMTownsTemplate::RunOneInstruction would count them one instruction at a time: its
    // remainder carries from instruction to instruction, so whole passes add up exactly.
    void skip(TownsType &towns, uint64_t instructions, uint64_t clocks, uint64_t deadline) {
        auto &state = towns.state;
        if (clocks == 0 || state.townsTime > deadline) return;
        const uint64_t frequency = state.currentFreq;
        const uint64_t balance = state.clockBalance;
        // The most passes k with townsTime + (balance + k * clocks * 1000) / frequency <= deadline.
        const uint64_t room = (deadline - state.townsTime + 1) * frequency;
        if (room <= balance) return;
        const uint64_t passes = (room - 1 - balance) / (clocks * 1000);
        if (passes == 0) return;
        const uint64_t total = balance + passes * clocks * 1000;
        state.townsTime += total / frequency;
        state.clockBalance = total % frequency;
        ++counts_.skips;
        counts_.passes += passes;
        counts_.instructions += passes * instructions;
    }

    uint32_t last_eip_ = 0;
    uint16_t last_cs_ = 0;
    bool candidate_ = false;
    uint16_t pass_cs_ = 0;
    uint32_t pass_eip_ = 0;
    uint64_t pass_effects_ = 0, pass_instructions_ = 0, pass_clocks_ = 0;
    Snapshot pass_state_, scratch_;
    Counts counts_;
};

} // namespace kairotowns
