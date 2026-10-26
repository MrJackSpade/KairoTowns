// Translates a run of guest i486 instructions into AArch64 code that does exactly what Tsugaru's
// interpreter does for them (KairoTowns#19). See jit.h for how blocks run.

#pragma once

#include <cstdint>
#include <vector>

#include "a64_assembler.h"
#include "context.h"
#include "i486.h"

namespace kairotowns::jit {

// What a translation depends on besides its code bytes.
struct Mode {
    uint32_t operand_size = 32, address_size = 32;  // the code segment's defaults
    uint32_t stack_address_size = 32;
    bool paging = false;
    uint32_t cpu_mode = 0;  // i486DXCommon::State::mode

    uint32_t key() const {
        return (operand_size == 32 ? 1u : 0u) | (address_size == 32 ? 2u : 0u) | (stack_address_size == 32 ? 4u : 0u) |
               (paging ? 8u : 0u) | cpu_mode << 4;
    }
};

// Host registers of translated code.
namespace host {
constexpr Reg CTX = 19, STATE = 20;
// Guest EAX, ECX, EDX, EBX, ESP, EBP, ESI and EDI.
constexpr Reg GUEST0 = 21;
// The lazily kept flags: the operation's operands, its result and EFLAGS before it.
constexpr Reg FLAG_A = 9, FLAG_B = 10, FLAG_R = 11, FLAGS = 12;
// Clocks still allowed before the next instruction may start; signed.
constexpr Reg LIMIT = 15;
// Context::tlb_tag, reloaded after calls that may step it.
constexpr Reg TLB_TAG = 29;
} // namespace host

class Translator {
public:
    Translator(i486DXCommon &cpu, Memory &mem);

    struct Result {
        uint32_t instructions = 0;  // translated
        uint32_t bytes = 0;         // of guest code they cover
    };
    // Translates the code at host pointer code (at least MAX_INSTRUCTION_LENGTH readable bytes
    // up to the end of its 4 KB page, avail in all), which is the guest's CS:eip, into a.
    // instructions is 0 when the first instruction cannot be translated; a holds nothing then.
    // stamp is where the block keeps the code generation its code check last saw.
    // specialize: the block takes the segment registers' base and limits as constants, checked
    // at its entry; else it reads them as it runs.
    Result translate(Assembler &a, const uint8_t *code, uint32_t avail, uint32_t eip, const Mode &mode, uint64_t *stamp,
                     bool specialize);

    // The trampolines: enter(Context *, const void *block) runs a block; blocks leave through
    // leave. Written once at the start of the code buffer.
    static void emit_enter(Assembler &a, const i486DXCommon &cpu);
    static void emit_leave(Assembler &a, const i486DXCommon &cpu);
    // Dispatch in translated code: with the next EIP in w2 and the mode key in w3, and the run's
    // instructions, clocks and last EIP carried in the context as a chain carries them, enters
    // the block for that code when Context::dispatch_table has one in the current code window;
    // else leaves for the dispatcher.
    static void emit_dispatch(Assembler &a, const i486DXCommon &cpu);

    // The routines every block's exits and slow ways share, written once after the trampolines;
    // Context has where each is.
    enum class Stub {
        Exit,        // leaves translated code, as the exit record after the call says
        Interpret,   // the interpreter runs an instruction inside the run
        JumpedBack,  // the wait-loop skip asked at a jump back
        Link,        // leaves asking for the branch in V0 to be linked
        Compare,     // compares a block's code with its copy of it
        TlbMissFlat, TlbMissPaged,  // ram_address() where the TLB misses, without or with paging
    };
    void emit_stub(Assembler &a, Stub stub);

private:
    struct Impl;
    i486DXCommon &cpu_;
    Memory &mem_;
};

} // namespace kairotowns::jit
