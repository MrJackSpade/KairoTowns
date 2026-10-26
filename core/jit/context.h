// What translated code and the JIT's C++ share (KairoTowns#19): the context a block runs with, how
// it says what it did, and the lazily kept flags.

#pragma once

#include <cstddef>
#include <cstdint>

#include "i486.h"
#include "inout.h"
#include "ramrom.h"

namespace kairotowns::jit {

// What made a run of translated code end, for measuring (KAIROTOWNS_JIT_STATS, jit.cpp).
enum ExitWhy : uint32_t {
    WHY_INTERPRET = 0,  // an instruction the translation leaves to the interpreter
    WHY_DYNAMIC,        // a jump whose target is in a register (RET, indirect)
    WHY_BACKWARD,       // a jump back, for the wait-loop skip
    WHY_ELSEWHERE,      // a jump out of the code window, or the end of the block
    WHY_LIMIT,          // the clock limit
    WHY_ENTRY,          // stopped or changed at a block's entry
    WHY_LINK,           // asking for a link
    WHY_IO,             // after I/O
    WHY_POPF,           // after POPF
    WHY_INTERPRETED,    // after an instruction the interpreter ran inside the run
    WHY_DISPATCH,       // a jump the dispatch in translated code found no block for
    WHY_COUNT
};
inline const char *exit_why_name(uint32_t why) {
    static const char *names[WHY_COUNT] = {"interpret", "dynamic", "backward", "elsewhere", "limit", "entry", "link", "io", "popf",
                                           "interpreted", "dispatch"};
    return why < WHY_COUNT ? names[why] : "?";
}

// Why a block stopped.
enum Reason : uint32_t {
    // Translated code may go on from the place it stopped (a jump, the end of the block, or the
    // clock limit).
    REASON_NEXT = 0,
    // The interpreter runs the next instruction: the translation does not cover it, or it would
    // fault, or it touches something other than main RAM.
    REASON_INTERPRET = 1,
    // The block's code is not what it was translated from any more; it ran nothing. The
    // dispatcher translates the code again.
    REASON_MISMATCH = 2,
    // The segment registers are not those the block was specialized on; it ran nothing. The
    // dispatcher translates the code again, reading them as it runs.
    REASON_SEGMENTS = 3,
};

// The operation whose flags translated code keeps lazily: its operands are in host registers, and
// Tsugaru's own function for it computes EFLAGS from them when they are needed.
enum FlagOp : uint32_t {
    FLAGS_NONE = 0, FLAGS_ADD, FLAGS_ADC, FLAGS_SUB, FLAGS_SBB, FLAGS_AND, FLAGS_OR, FLAGS_XOR, FLAGS_INC, FLAGS_DEC,
    // Shifts and rotates, with b the count; EFLAGS before them is whole in the base (they keep
    // some flags, and Tsugaru's set some even for a count of 0).
    FLAGS_ROL, FLAGS_ROR, FLAGS_SHL, FLAGS_SHR, FLAGS_SAR
};

// A FlagOp at an operand size: op * 4 + 0 (8 bits), 1 (16) or 2 (32). FLAGS_NONE is kind 0.
inline uint32_t flag_kind(FlagOp op, uint32_t bits) { return op == FLAGS_NONE ? 0 : uint32_t(op) * 4 + (bits == 8 ? 0 : bits == 16 ? 1 : 2); }
inline FlagOp flag_op(uint32_t kind) { return FlagOp(kind / 4); }
inline uint32_t flag_bits(uint32_t kind) { return 8u << (kind % 4); }

struct Context;
// EFLAGS after the operation kind on a and b, with base the EFLAGS before it, as Tsugaru computes
// them; translated code calls it through Context::materialize.
extern "C" uint32_t kairotowns_jit_materialize(Context *ctx, uint32_t kind, uint32_t base, uint32_t a, uint32_t b);
// An IN or OUT as the interpreter runs it (what: 1 for OUT, the operand bytes << 8, the
// instruction's bytes << 16), after the run's clocks_before clocks so far are added to guest
// time. The guest registers, EFLAGS and EIP are in the CPU state. Non-zero when the run must end
// after it (jit.cpp).
extern "C" uint32_t kairotowns_jit_io(Context *ctx, uint32_t what, uint32_t port, uint32_t value, uint32_t clocks_before,
                                      uint32_t next_eip);

// The instruction at eip run by the interpreter (RunOneInstruction), after the run's
// clocks_before clocks so far are added to guest time, with the guest registers, EFLAGS and EIP
// in the CPU state. Returns its clocks, with bit 31 set when the run must end after it: it went
// anywhere but next_eip, changed the CPU's mode from mode, or left the interpreter's loop a reason
// to stop (jit.cpp).
extern "C" uint32_t kairotowns_jit_interpret(Context *ctx, uint32_t clocks_before, uint32_t eip, uint32_t next_eip,
                                             uint32_t mode);

// A jump back from the instruction at jump_eip, with instructions and clocks run so far: guest
// time brought up to it, and Host::jumped_back asked. Non-zero when the run must end (jit.cpp).
extern "C" uint32_t kairotowns_jit_jumped_back(Context *ctx, uint32_t clocks_before, uint32_t instructions, uint32_t jump_eip);

// The machine around the CPU, for I/O in translated code: guest time and the devices are its.
struct Host {
    void *owner = nullptr;
    // Adds clocks of CPU time to guest time, as each instruction the interpreter runs does.
    void (*advance)(void *owner, uint32_t clocks) = nullptr;
    // Whether the interpreter's loop would stop running instructions now: an interrupt to take,
    // a stop, or the next fast device polling moved.
    bool (*must_stop)(void *owner) = nullptr;
    // A jump back from the instruction at jump_eip in code segment cs, to the instruction at the
    // CPU's EIP, with instructions and clocks more run since the run started: what the
    // interpreter's loop does before that instruction (the wait-loop skip, wait_loop.h). True when
    // it moved guest time on, which ends the run.
    bool (*jumped_back)(void *owner, uint16_t cs, uint32_t jump_eip, uint32_t instructions, uint32_t clocks) = nullptr;
};

// Where main RAM is for a guest linear page, as the page-table cache and Memory's slots put it
// (translator.cpp ram_address()): host address = addend + linear address. tag is the page plus
// the TLB's generation << 20 (Context::tlb_tag, never 0), so stepping the generation drops every
// entry when what they come from may have changed.
struct TlbEntry {
    uint32_t tag = 0, unused = 0;
    uint64_t addend = 0;
};
constexpr uint32_t kTlbEntries = 4096;

// A block the dispatch in translated code finds for a jump (Translator::emit_dispatch): the block
// at code (the guest code's host address) and eip, translated for mode, enters at entry.
struct DispatchEntry {
    const uint8_t *code = nullptr;
    uint32_t eip = 0, mode = 0;
    const void *entry = nullptr;
    uint64_t unused = 0;
};
constexpr uint32_t kDispatchBits = 12;

// The context a block runs with; x19 holds it while translated code runs. Translated code reads
// and writes it at offsetof() offsets.
struct Context {
    // Set by the dispatcher before each run.
    i486DXCommon::State *state = nullptr;
    MemoryAccess *const *access = nullptr;  // Memory's 4 KB slots
    const MemoryAccess *main_ram = nullptr;
    uint8_t *ram = nullptr;
    uint64_t *side_effects = nullptr;
    const bool *parity = nullptr;
    i486DXCommon::PageTableCache *page_cache = nullptr;
    MemoryAccess::MemoryWindow *stack_window = nullptr;
    i486DXCommon *cpu = nullptr;
    void *materialize = nullptr;
    void *leave = nullptr;
    void *io_helper = nullptr;
    void *interpret_helper = nullptr;
    void *jumped_back_helper = nullptr;
    // The routines blocks share (Translator::emit_stub), entered with blr and the record of the
    // place they are called from after the call.
    void *exit_stub = nullptr;
    void *interpret_stub = nullptr;
    void *jumped_back_stub = nullptr;
    void *link_stub = nullptr;
    void *compare_stub = nullptr;
    void *tlb_miss_flat = nullptr;
    void *tlb_miss_paged = nullptr;
    Memory *io_memory = nullptr;  // the machine's Memory, for the interpreter
    // The PIC's IRR0_OR_IRR1_Cache: non-zero while an interrupt waits that the CPU takes when IF
    // is set (towns/pic/pic.h).
    const unsigned char *irq_waiting = nullptr;
    InOut *io = nullptr;
    const Host *host = nullptr;
    TlbEntry *tlb = nullptr;
    DispatchEntry *dispatch_table = nullptr;
    // Steps whenever memory may have changed by anything but translated code's checked stores,
    // and at every store translated code makes: a block whose code check saw this generation
    // need not check its code again (Translator prologue()).
    uint64_t code_generation = 1;
    void *dispatch = nullptr;  // Translator::emit_dispatch's code
    const MemoryAccess::ConstMemoryWindow *code_window = nullptr;
    // The TLB's generation << 20; translated code keeps it in host::TLB_TAG.
    uint32_t tlb_tag = 1u << 20;
    // Drops the TLB where what it comes from changed (jit.cpp), with jit the dispatcher.
    void (*refresh_tlb)(void *jit) = nullptr;
    void *jit = nullptr;
    // The host bytes of the running block's guest code: a store into them stops the block before it.
    const uint8_t *code_start = nullptr;
    uint64_t code_bytes = 0;
    // Translated code runs on from one block into the next where it can (jit.h): the branch the
    // running block was entered through, if it was; and the branch of an exit that asks to be
    // pointed at the block at link_eip.
    void *came_from = nullptr;
    void *link_site = nullptr;
    // The most clocks the instructions run before an instruction may have taken for it to run.
    int32_t clock_limit = 0;
    uint32_t spill = 0;
    // The instructions the blocks before the running one ran, and the EIP of the last of them.
    uint32_t chained_instructions = 0, chained_last_eip = 0;
    uint32_t link_eip = 0;
    // Clocks of this run I/O has added to guest time already.
    uint32_t advanced = 0;
    // Whether the instruction the interpreter ran last in the run left interrupts held for one
    // instruction (an SS load or STI): the dispatcher's caller then keeps them held.
    uint32_t keep_hold_irq = 0;
    uint32_t spill_eip = 0;
    uint64_t spill_record = 0;

    // Written by the exit of a block.
    uint32_t instructions = 0;  // run
    uint32_t clocks = 0;        // they took
    uint32_t last_eip = 0;      // the EIP of the last one run
    uint32_t reason = REASON_NEXT;
    uint32_t why = WHY_INTERPRET;
};

} // namespace kairotowns::jit
