// Skips the passes of a guest wait loop that cannot change anything, exactly.
//
// A game that waits for an interrupt by polling a RAM flag runs the same few instructions over
// and over; on the RG DS that busy-wait costs more than a frame (Mahou Daisakusen, KairoTowns#6).
// Between two fast device pollings only the CPU runs: timers, sound, VSYNC and interrupts act at
// the polling (FMTownsCommon::RunFastDevicePollingInternal), or through I/O and memory the CPU
// touches. So when a pass of a loop starts at the same place as the last pass, with the whole CPU
// state equal and nothing in between that may have an effect (a store, an I/O access, a read from
// anything but RAM, a push that changes the stack; Memory::sideEffects and InOut::accesses count
// them), every later pass until
// the polling is the same pass again. The skip moves guest time over as many whole passes as end
// before the polling, with Tsugaru's own clock arithmetic, and the CPU then runs on from the same
// instruction with the same state and time as it would have reached. Nothing else changes.
//
// Every backward jump in a code segment may start a pass, and a loop may hold several, such as the
// return from a routine it calls: the skip keeps the last few places a pass started (kPasses), each
// compared only with itself.
//
// A pass may also read I/O ports whose reads have no effect, such as VSYNC status
// (TownsMachine::Impl::pure_read): such a pass is skipped too, while every read would return what it
// returned, which the read says (read_holds_until).
//
// The polling itself changes nothing the loop reads unless it raises an interrupt request: a
// device's write to RAM goes through Memory (DMA included) and counts in sideEffects, and taking an
// interrupt changes the CPU state. So a pass found before a polling with no interrupt request
// waiting is still the same pass after it, and skipping goes on without finding it again; with a
// request waiting the machine restarts, so the CPU takes the interrupt as soon as it would have.

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

    // What came before says nothing about what follows: an interrupt may be taken, or the machine
    // did more between instructions than the fast device polling (TownsMachine::run_guest).
    void restart() {
        for (auto &pass : passes_) pass.used = false;
    }

    // An I/O read without effect (InOut::kairoPureRead) returns what it returned until the
    // guest time [until]: no skipped pass may run into it.
    void read_holds_until(uint64_t until) {
        for (auto &pass : passes_) pass.reads_hold_until = std::min(pass.reads_hold_until, until);
    }

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

    // Translated code ran on to the instruction at CS:eip (jit/jit.h). Within a run EIP only grows,
    // so none of its instructions started a pass; the last is the one the next is compared with.
    void ran_to(uint16_t cs, uint32_t eip) {
        last_cs_ = cs;
        last_eip_ = eip;
    }

    const Counts &counts() const { return counts_; }

private:
    __attribute__((noinline)) void at_pass_start(TownsType &towns, uint16_t cs, uint32_t eip, uint64_t instructions,
                                                 uint64_t clocks, uint64_t deadline) {
        const auto &state = towns.CPU().state;
        const uint64_t effects = towns.mem.sideEffects + towns.io.accesses;
        Pass *found = nullptr;
        for (auto &pass : passes_)
            if (pass.used && pass.cs == cs && pass.eip == eip) found = &pass;
        if (found == nullptr) {
            // A new place: it takes the place of the one found longest ago.
            found = &passes_[next_];
            next_ = (next_ + 1) % kPasses;
            found->used = true;
            found->candidate = false;
            found->cs = cs;
            found->eip = eip;
            found->effects = ~effects;
            found->reads_hold_until = kForever;
        }
        Pass &pass = *found;
        Snapshot &pass_state = states_[found - passes_];
        // Only a pass that starts where one did, with nothing done in between, can be a repeat;
        // the state is captured for such a pass alone, so code that only loops now and then costs
        // no copies.
        const bool again = effects == pass.effects;
        // The pass's reads without effect hold until this time; a skipped pass runs before it.
        const uint64_t reads_hold_until = pass.reads_hold_until;
        pass.reads_hold_until = kForever;
        if (again && pass.candidate) {
            if (pass_state.matches(state)) {
                skip(towns, instructions - pass.instructions, clocks - pass.clocks,
                     reads_hold_until == kForever ? deadline : std::min<uint64_t>(deadline, reads_hold_until - 1));
                // The time left is less than a pass; the CPU runs it, across the polling, and the
                // pass after it, with the same state and nothing done between, is the same pass:
                // it is counted from here. Passes counted from elsewhere span the skip: they
                // start again.
                for (auto &other : passes_)
                    if (&other != &pass) other.used = false;
                pass.instructions = instructions;
                pass.clocks = clocks;
                return;
            }
            pass_state.take(state);
        } else if (again) {
            pass_state.take(state);
            pass.candidate = true;
        } else {
            pass.candidate = false;
        }
        pass.effects = effects;
        pass.instructions = instructions;
        pass.clocks = clocks;
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

        // Stores [state] here.
        void take(const CpuState &state) {
            size_t used = 0;
            fields(state, [&](const void *value, size_t size) {
                std::memcpy(bytes + used, value, size);
                used += size;
                return true;
            });
        }
        // Whether [state] is the state stored here, field by field: the registers first, which a
        // loop that is not waiting changes, so such a loop is told apart at its first field.
        bool matches(const CpuState &state) const {
            size_t used = 0;
            return fields(state, [&](const void *value, size_t size) {
                const bool same = std::memcmp(bytes + used, value, size) == 0;
                used += size;
                return same;
            });
        }

    private:
        // Calls [field] with each part of the state in turn while it returns true; true when every
        // call did.
        template <class Field> static bool fields(const CpuState &state, Field &&field) {
            const unsigned int cr[4] = {static_cast<unsigned int>(state.GetCR(0)), static_cast<unsigned int>(state.GetCR(1)),
                                        static_cast<unsigned int>(state.GetCR(2)), static_cast<unsigned int>(state.GetCR(3))};
            return field(&state.NULL_and_reg32, sizeof state.NULL_and_reg32) && field(&state.mode, sizeof state.mode) &&
                   field(&state.EIP, sizeof state.EIP) && field(&state.EFLAGS, sizeof state.EFLAGS) &&
                   field(&state.sreg, sizeof state.sreg) && field(&state.GDTR, sizeof state.GDTR) &&
                   field(&state.IDTR, sizeof state.IDTR) && field(&state.LDTR, sizeof state.LDTR) &&
                   field(&state.TR, sizeof state.TR) && field(cr, sizeof cr) && field(&state.DR, sizeof state.DR) &&
                   field(&state.TEST, sizeof state.TEST) && field(&state.fpuState, sizeof state.fpuState) &&
                   field(&state.halt, sizeof state.halt) && field(&state.holdIRQ, sizeof state.holdIRQ) &&
                   field(&state.exception, sizeof state.exception) &&
                   field(&state.exceptionCode, sizeof state.exceptionCode) &&
                   field(&state.exceptionType, sizeof state.exceptionType) &&
                   field(&state.exceptionLinearAddr, sizeof state.exceptionLinearAddr);
        }
    };

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

    static constexpr uint64_t kForever = ~uint64_t(0);
    // A place a pass started, and what was so when it last did.
    struct Pass {
        bool used = false;
        // Whether the last pass from here did nothing; its start's state is in states_.
        bool candidate = false;
        uint16_t cs = 0;
        uint32_t eip = 0;
        uint64_t effects = 0, instructions = 0, clocks = 0;
        // The earliest time a read without effect since the pass started returns something else.
        uint64_t reads_hold_until = kForever;
    };
    static constexpr unsigned int kPasses = 4;
    Pass passes_[kPasses];
    // The state each place's last pass started with, apart, so looking a place up stays small.
    Snapshot states_[kPasses];
    unsigned int next_ = 0;
    uint32_t last_eip_ = 0;
    uint16_t last_cs_ = 0;
    Counts counts_;
};

} // namespace kairotowns
