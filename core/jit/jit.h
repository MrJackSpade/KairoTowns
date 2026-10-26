// A JIT for Tsugaru's i486 (KairoTowns#19): runs translated AArch64 code for runs of guest
// instructions in place of the interpreter, with exactly the interpreter's results.
//
// How it stays exact:
// - A block is translated from the guest code bytes at CS:EIP, decoded by Tsugaru's own decoder,
//   for one set of modes (Mode). Its bytes are compared with the code each time it runs.
// - Each instruction is translated path for path from the interpreter's code for it
//   (translator.cpp). Anything the translation does not cover, and anything at run time that
//   would leave main RAM (a fault, a page the page-table cache does not hold, memory-mapped I/O,
//   a page boundary the interpreter splits at, a store into the block's own code), stops the
//   block before that instruction, with nothing of it done; the interpreter then runs it.
// - Translated code touches nothing but the CPU's registers and main RAM, so between the
//   instructions of a block nothing the interpreter's loop checks can change: no interrupt
//   request, stop flag or device. The caller runs a block only when the interpreter would run
//   its first instruction with no interrupt to take after it, and gives it the clock budget left
//   before the next fast device polling: the block stops before an instruction the interpreter's
//   loop would not start.
// - Clocks and instructions are counted as the interpreter counts them; adding a block's clocks
//   to Tsugaru's time at once gives the time adding them one instruction at a time gives.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "context.h"
#include "i486.h"
#include "ramrom.h"

namespace kairotowns::jit {

// Whether this host runs translated code (AArch64 Linux and Android).
bool supported();

class Jit {
public:
    // The CPU, memory and I/O of a machine, its main RAM and the memory-access object that serves
    // it, and the machine's part in I/O (context.h).
    Jit(i486DXCommon &cpu, Memory &mem, InOut &io, const MemoryAccess *main_ram, std::vector<unsigned char> &ram,
        const Host &host, const unsigned char *irq_waiting);
    ~Jit();
    Jit(const Jit &) = delete;
    Jit &operator=(const Jit &) = delete;

    struct Run {
        // Instructions run and the clocks they took; 0 when none ran.
        uint32_t instructions = 0, clocks = 0;
        // The EIP of the last instruction run.
        uint32_t last_eip = 0;
        // Of the clocks, those I/O added to guest time already (Host::advance).
        uint32_t advanced = 0;
        // Whether the last instruction left interrupts held for the next (State::holdIRQ).
        bool keep_hold_irq = false;
        // Whether the interpreter runs the next instruction (translated code may not).
        bool interpret_next = true;
        // Whether the machine did what follows the run already: it ended where translated code
        // did the polling (Host::poll) or went on after the wait-loop skip (Host::go_on).
        bool ended = false;
    };
    // Runs translated code from CS:EIP. clock_limit is the most clocks the instructions run before
    // an instruction may have taken for the interpreter's loop to start it.
    Run run(int32_t clock_limit);

    struct Stats {
        uint64_t runs = 0, instructions = 0, translations = 0, refused = 0, flushes = 0;
        // Blocks linked to the next, and blocks found changed at their entry.
        uint64_t links = 0, mismatches = 0, tlb_flushes = 0;
        // Host time spent translating, and translated code's bytes.
        uint64_t translate_ns = 0, code_bytes = 0;
        // Blocks the dispatch in translated code found through the dispatcher.
        uint64_t found = 0;
        // Runs by what ended them (ExitWhy, context.h).
        uint64_t exits[WHY_COUNT] = {};
    };
    const Stats &stats() const;

    // Blocks check their code and the segment registers they take as constants only where they
    // may have changed (Translator prologue()). Stores into main RAM count by line, translated
    // or not (Memory::kairoLines); whatever else may write memory or load a segment register
    // between runs (an instruction the interpreter runs, an interrupt taken, the devices) is
    // watched: a Mark before it, and after() once it is done. Anything else the machine does,
    // changed().
    struct Mark {
        uint64_t writes = 0, other_writes = 0;  // Memory::kairoWrites, kairoOtherWrites
        // The segment registers whole: a change to any of their bytes counts.
        i486DXCommon::SegmentRegister segments[6];
    };
    Mark mark() const;
    void after(const Mark &mark);
    void changed();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace kairotowns::jit
