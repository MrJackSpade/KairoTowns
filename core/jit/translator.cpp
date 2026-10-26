// See translator.h. Each guest instruction is translated from what Tsugaru's interpreter does for
// it (cpu/i486runinstruction.h), path for path: the interpreter reads and writes memory three ways
// (FetchByte/Word/Dword, GetOperandPointer*, and Push/Pop through the stack window), each with its
// own checks, and a translation takes the way the interpreter takes and makes the same checks.
// Where the interpreter would do anything but reach main RAM by a pointer (a fault, a page the
// page-table cache does not hold, memory-mapped I/O, a page boundary it splits at), the
// translation stops before the instruction, having changed nothing, and the interpreter runs it.

#include "translator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

namespace kairotowns::jit {
namespace {

using Cpu = i486DXFidelityLayer<i486DXDefaultFidelityOperation>;
using Burst = Cpu::BurstModeFetchInstructionFunctions;
using Operand = i486DXCommon::Operand;
using Instruction = i486DXCommon::Instruction;

// Scratch registers; free between instructions.
constexpr Reg V0 = 0, V1 = 1, V2 = 2, V3 = 3, OFF = 4, LIN = 5, ADDR = 6, SEFX = 7, V4 = 8, T0 = 13, T1 = 14,
              IP0 = 16, IP1 = 17;

constexpr uint32_t F_CF = 0x001, F_PF = 0x004, F_ZF = 0x040, F_DF = 0x400, F_IF = 0x200;

// Longest block, in instructions and in clocks (immediates of the limit checks).
constexpr uint32_t kMaxClocks = 3000;
// Instructions a block may have; KAIROTOWNS_JIT_BLOCK sets fewer, down to one, so that
// core/test/jit_lockstep.cpp compares the CPU after every instruction.
uint32_t max_instructions() {
    static const uint32_t most = [] {
        const char *set = std::getenv("KAIROTOWNS_JIT_BLOCK");
        const long n = set ? std::atol(set) : 64;
        return uint32_t(n < 1 ? 1 : n > 64 ? 64 : n);
    }();
    return most;
}

uint32_t state_offset(const i486DXCommon::State &state, const void *member) {
    return uint32_t(static_cast<const char *>(member) - reinterpret_cast<const char *>(&state));
}

bool is_reg(const Operand &op) {
    return op.operandType == i486DXCommon::OPER_REG32 || op.operandType == i486DXCommon::OPER_REG16 ||
           op.operandType == i486DXCommon::OPER_REG8;
}
bool is_addr(const Operand &op) { return op.operandType == i486DXCommon::OPER_ADDR; }

// The GetOperandPointer* function an instruction uses: which register operands it takes, and how
// close to the end of a page the operand may start.
enum class PtrFn { P8, P16, P32, P16OR32 };

} // namespace

struct Translator::Impl {
    Assembler &a;
    i486DXCommon &cpu;
    Memory &mem;
    const Mode &mode;
    const i486DXCommon::State &st;

    struct Exit {
        Assembler::Label label;
        uint32_t eip = 0;
        bool eip_in_reg = false;  // in V2
        uint32_t instructions = 0, clocks = 0, last_eip = 0, reason = REASON_NEXT, kind = 0;
        uint32_t why = WHY_INTERPRET;
        // Runs on into the block at eip once the dispatcher has linked it.
        bool chain = false;
        // Leaves at the block's entry, before its first instruction: the last instruction run is
        // the previous block's.
        bool entry = false;
        // Has the interpreter run the instruction at eip, then goes on to next_eip.
        bool interpret = false;
        // A jump back: asks the wait-loop skip, then runs on into the block at eip.
        bool back = false;
        uint32_t next_eip = 0;
    };
    std::vector<std::unique_ptr<Exit>> exits;

    // The lazily kept flags at this point of the code.
    uint32_t kind = 0;
    // Where the block starts, and how far into its 4 KB code window.
    uint32_t block_eip = 0, window_offset = 0;
    // Whether the block so far does I/O or a store Memory::sideEffects counts: a pass of a loop
    // through it can never be skipped as a wait loop's (wait_loop.h), so it may run on into a
    // block further back.
    bool has_effects = false;
    // The instruction being translated.
    uint32_t index = 0, eip = 0, clocks_before = 0, last_eip = 0;
    const Instruction *inst = nullptr;
    const Operand *op1 = nullptr, *op2 = nullptr;
    Exit *bail_exit = nullptr;

    Impl(Assembler &a, i486DXCommon &cpu, Memory &mem, const Mode &mode)
        : a(a), cpu(cpu), mem(mem), mode(mode), st(cpu.state) {}

    // --- State offsets ------------------------------------------------------------------------

    uint32_t reg_offset(uint32_t i) const { return state_offset(st, &st.NULL_and_reg32[1 + i]); }
    uint32_t eip_offset() const { return state_offset(st, &st.EIP); }
    uint32_t eflags_offset() const { return state_offset(st, &st.EFLAGS); }
    uint32_t seg_base(int s) const { return state_offset(st, &st.sreg[s].baseLinearAddr); }
    uint32_t seg_max(int s) const { return state_offset(st, &st.sreg[s].maxLimit); }
    uint32_t seg_min(int s) const { return state_offset(st, &st.sreg[s].minLimit); }
    uint32_t seg_min_adjust(int s) const { return state_offset(st, &st.sreg[s].minLimitAdjust); }

    // The segment registers' base and limits as the block was translated with them: its code
    // takes them as constants, and its entry checks them (prologue()) when the code generation
    // moved, which every load of a segment register does (it is the interpreter's).
    uint32_t segments_used = 0;
    bool specialize = true;
    // LIN = OFF + the segment's base.
    void add_segment_base(Reg dst, Reg off, int s) {
        if (specialize) return add_const(dst, off, segment_base(s));
        a.ldr_w(T0, host::STATE, seg_base(s));
        a.add(false, dst, T0, off);
    }
    uint32_t segment_base(int s) {
        segments_used |= 1u << s;
        return st.sreg[s].baseLinearAddr;
    }
    uint32_t segment_max(int s) {
        segments_used |= 1u << s;
        return st.sreg[s].maxLimit;
    }
    // minLimit+minLimitAdjust, in 32 bits, as GetOperandPointer adds them.
    uint32_t segment_min(int s) {
        segments_used |= 1u << s;
        return st.sreg[s].minLimit + st.sreg[s].minLimitAdjust;
    }
    uint32_t page_counter_offset() const { return state_offset(st, &st.pageTableCacheValidCounter); }
    static uint32_t ctx(size_t offset) { return uint32_t(offset); }

    static Reg gr(uint32_t i) { return host::GUEST0 + i; }

    // --- Exits --------------------------------------------------------------------------------

    Exit &new_exit(uint32_t reason, uint32_t instructions, uint32_t clocks, uint32_t last, uint32_t to) {
        exits.push_back(std::make_unique<Exit>());
        Exit &e = *exits.back();
        e.reason = reason;
        e.instructions = instructions;
        e.clocks = clocks;
        e.last_eip = last;
        e.eip = to;
        e.kind = kind;
        e.why = reason == REASON_INTERPRET ? WHY_INTERPRET : WHY_ELSEWHERE;
        return e;
    }
    // Stops before the current instruction, with nothing of it done, for the interpreter to run it:
    // inside the run (interpret_here()).
    Assembler::Label &bail() {
        if (!bail_exit || bail_exit->kind != kind) {
            bail_exit = &new_exit(REASON_INTERPRET, index, clocks_before, last_eip, eip);
            bail_exit->interpret = true;
            bail_exit->next_eip = next_eip_now;
        }
        return bail_exit->label;
    }
    uint32_t next_eip_now = 0;
    // Whether translated code may run on from the current instruction into a block at to: further
    // on (a jump back may start a pass of a wait loop, which the interpreter's loop looks for,
    // wait_loop.h) and in the same code window with a whole instruction's bytes left.
    bool chainable(uint32_t to) const {
        if (max_instructions() == 1) return false;
        if (to <= eip && !has_effects) return false;
        const int64_t at = int64_t(window_offset) + int64_t(to) - int64_t(block_eip);
        return at >= 0 && at < 4096;
    }
    // The exit after the current instruction, which took clocks, for EIP to.
    Exit &exit_to(uint32_t clocks, uint32_t to) {
        Exit &e = new_exit(REASON_NEXT, index + 1, clocks_before + clocks, eip, to);
        e.chain = chainable(to);
        if (!e.chain && to <= eip) {
            e.why = WHY_BACKWARD;
            // In the same window, the run asks the wait-loop skip itself and goes on.
            const int64_t at = int64_t(window_offset) + int64_t(to) - int64_t(block_eip);
            e.back = at >= 0 && at < 4096 && max_instructions() > 1;
        }
        return e;
    }
    // Leaves after the current instruction, which took clocks, for EIP to.
    void exit_after(uint32_t clocks, uint32_t to) { a.b(exit_to(clocks, to).label); }
    // The same, with the next EIP in V2.
    void exit_after_dynamic(uint32_t clocks) {
        Exit &e = new_exit(REASON_NEXT, index + 1, clocks_before + clocks, eip, 0);
        e.eip_in_reg = true;
        e.why = WHY_DYNAMIC;
        a.b(e.label);
    }

    void emit_exit(Exit &e) {
        a.bind(e.label);
        if (e.interpret) return interpret_here(e);
        if (e.back) return jump_back(e);
        if (e.chain) return emit_chain(e);
        if (e.reason == REASON_NEXT && !e.entry && max_instructions() > 1 &&
            (e.why == WHY_DYNAMIC || e.why == WHY_ELSEWHERE))
            return emit_dispatched(e);
        exit_tail(e.instructions, e.clocks, e.entry, e.last_eip, e.reason, e.eip_in_reg, e.eip, e.kind, e.why);
    }
    // Leaves translated code: the instructions run are those of the blocks before this one and
    // instructions more; the clocks, the static count plus what conditional instructions and the
    // blocks before took off the limit.
    void exit_tail(uint32_t instructions, uint32_t clocks, bool last_from_ctx, uint32_t last, uint32_t reason,
                   bool eip_in_reg, uint32_t to, uint32_t flag_kind_now, uint32_t why) {
        a.mov_imm32(V1, why);
        a.str_w(V1, host::CTX, ctx(offsetof(Context, why)));
        a.ldr_w(V0, host::CTX, ctx(offsetof(Context, clock_limit)));
        a.sub(false, V0, V0, host::LIMIT);
        add_const(V0, V0, clocks);
        a.str_w(V0, host::CTX, ctx(offsetof(Context, clocks)));
        a.ldr_w(V1, host::CTX, ctx(offsetof(Context, chained_instructions)));
        add_const(V1, V1, instructions);
        a.str_w(V1, host::CTX, ctx(offsetof(Context, instructions)));
        if (last_from_ctx) a.ldr_w(V1, host::CTX, ctx(offsetof(Context, chained_last_eip)));
        else a.mov_imm32(V1, last);
        a.str_w(V1, host::CTX, ctx(offsetof(Context, last_eip)));
        a.mov_imm32(V1, reason);
        a.str_w(V1, host::CTX, ctx(offsetof(Context, reason)));
        if (eip_in_reg) {
            a.str_w(V2, host::STATE, eip_offset());
        } else {
            a.mov_imm32(V1, to);
            a.str_w(V1, host::STATE, eip_offset());
        }
        flags_into(flag_kind_now, V0);
        a.str_w(V0, host::STATE, eflags_offset());
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, leave)));
        a.br(IP0);
    }
    // The interpreter runs the instruction at e.eip (kairotowns_jit_interpret), with the CPU state
    // as it has it; its clocks come off the limit. The run then goes on into the block at the
    // next instruction, which checks its code (the instruction may have stored into it), or ends
    // where the interpreter went elsewhere or left a reason to stop.
    void interpret_here(Exit &e) {
        flags_into(e.kind, host::FLAGS);
        const uint32_t regs = reg_offset(0);
        for (uint32_t i = 0; i < 8; i += 2) a.stp_w(gr(i), gr(i + 1), host::STATE, int32_t(regs + 4 * i));
        a.str_w(host::FLAGS, host::STATE, eflags_offset());
        a.mov_imm32(V0, e.eip);
        a.str_w(V0, host::STATE, eip_offset());
        a.str_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
        a.ldr_w(V1, host::CTX, ctx(offsetof(Context, clock_limit)));
        a.sub(false, V1, V1, host::LIMIT);
        add_const(V1, V1, e.clocks);
        a.mov_imm32(V2, e.eip);
        a.mov_imm32(V3, e.next_eip);
        a.mov_imm32(OFF, mode.key());
        a.mov(true, V0, host::CTX);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, interpret_helper)));
        a.blr(IP0);
        a.ldr_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
        a.ldr_w(host::TLB_TAG, host::CTX, ctx(offsetof(Context, tlb_tag)));
        for (uint32_t i = 0; i < 8; i += 2) a.ldp_w(gr(i), gr(i + 1), host::STATE, int32_t(regs + 4 * i));
        a.ldr_w(host::FLAGS, host::STATE, eflags_offset());
        a.and_imm(false, V1, V0, 0x7FFFFFFF);
        a.sub(false, host::LIMIT, host::LIMIT, V1);
        // The exits after it count it with its clocks already off the limit.
        const uint32_t saved_kind = kind;
        kind = 0;
        Exit &stop = new_exit(REASON_NEXT, e.instructions + 1, e.clocks, e.eip, 0);
        stop.eip_in_reg = true;
        stop.why = WHY_INTERPRETED;
        Exit &on = new_exit(REASON_NEXT, e.instructions + 1, e.clocks, e.eip, e.next_eip);
        on.why = WHY_INTERPRETED;
        const int64_t at = int64_t(window_offset) + int64_t(e.next_eip) - int64_t(block_eip);
        on.chain = at >= 0 && at < 4096 && max_instructions() > 1;
        kind = saved_kind;
        Assembler::Label go_on;
        a.tbz(V0, 31, go_on);
        a.ldr_w(V2, host::STATE, eip_offset());
        a.b(stop.label);
        a.bind(go_on);
        a.b(on.label);
    }

    // A jump back to e.eip: what the interpreter's loop does before that instruction, the
    // wait-loop skip (kairotowns_jit_jumped_back), with the CPU state as it has it; then on into
    // the block there, or the run ends where the skip moved guest time.
    void jump_back(Exit &e) {
        flags_into(e.kind, host::FLAGS);
        const uint32_t regs = reg_offset(0);
        for (uint32_t i = 0; i < 8; i += 2) a.stp_w(gr(i), gr(i + 1), host::STATE, int32_t(regs + 4 * i));
        a.str_w(host::FLAGS, host::STATE, eflags_offset());
        a.mov_imm32(V0, e.eip);
        a.str_w(V0, host::STATE, eip_offset());
        a.str_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
        a.ldr_w(V1, host::CTX, ctx(offsetof(Context, clock_limit)));
        a.sub(false, V1, V1, host::LIMIT);
        add_const(V1, V1, e.clocks);
        a.ldr_w(V2, host::CTX, ctx(offsetof(Context, chained_instructions)));
        add_const(V2, V2, e.instructions);
        a.mov_imm32(V3, e.last_eip);
        a.mov(true, V0, host::CTX);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, jumped_back_helper)));
        a.blr(IP0);
        // The call takes the caller-saved registers, the limit and EFLAGS among them.
        a.ldr_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
        a.ldr_w(host::FLAGS, host::STATE, eflags_offset());
        Assembler::Label go_on;
        a.cbz(false, V0, go_on);
        // The skip has seen the jump, and the instruction there as the next: the last instruction
        // for the interpreter's loop's own wait-loop bookkeeping is the one jumped to.
        exit_tail(e.instructions, e.clocks, false, e.eip, REASON_NEXT, false, e.eip, 0, WHY_BACKWARD);
        a.bind(go_on);
        Exit on = e;
        on.kind = 0;
        on.back = false;
        on.chain = true;
        emit_chain(on);
    }

    // Goes on through the dispatch in translated code (emit_dispatch), with what a chain carries.
    // A jump back, to below the jump, starts a pass of a wait loop for the interpreter's loop
    // (wait_loop.h), which sees it as it would see it from the interpreter: such a jump leaves.
    void emit_dispatched(Exit &e) {
        if (e.eip_in_reg) {
            Assembler::Label forward;
            cmp_const(V2, e.last_eip);
            a.b_cond(HI, forward);
            exit_tail(e.instructions, e.clocks, e.entry, e.last_eip, e.reason, true, 0, e.kind, WHY_BACKWARD);
            a.bind(forward);
        } else if (e.eip <= e.last_eip) {
            return exit_tail(e.instructions, e.clocks, e.entry, e.last_eip, e.reason, false, e.eip, e.kind, WHY_BACKWARD);
        }
        if (e.eip_in_reg) a.str_w(V2, host::CTX, ctx(offsetof(Context, spill_eip)));
        flags_into(e.kind, host::FLAGS);
        a.sub_imm(false, host::LIMIT, host::LIMIT, e.clocks);
        a.ldr_w(V0, host::CTX, ctx(offsetof(Context, chained_instructions)));
        add_const(V0, V0, e.instructions);
        a.str_w(V0, host::CTX, ctx(offsetof(Context, chained_instructions)));
        a.mov_imm32(V0, e.last_eip);
        a.str_w(V0, host::CTX, ctx(offsetof(Context, chained_last_eip)));
        a.str_x(ZR, host::CTX, ctx(offsetof(Context, came_from)));
        if (e.eip_in_reg) a.ldr_w(V2, host::CTX, ctx(offsetof(Context, spill_eip)));
        else a.mov_imm32(V2, e.eip);
        a.mov_imm32(V3, mode.key());
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, dispatch)));
        a.br(IP0);
    }

    // Runs on into the block at e.eip: the flags made whole, the clocks and instructions so far
    // carried over, and a branch the dispatcher points at that block. Until it does, the branch
    // leaves and asks for it.
    void emit_chain(Exit &e) {
        flags_into(e.kind, host::FLAGS);
        a.sub_imm(false, host::LIMIT, host::LIMIT, e.clocks);
        a.ldr_w(V0, host::CTX, ctx(offsetof(Context, chained_instructions)));
        add_const(V0, V0, e.instructions);
        a.str_w(V0, host::CTX, ctx(offsetof(Context, chained_instructions)));
        a.mov_imm32(V0, e.last_eip);
        a.str_w(V0, host::CTX, ctx(offsetof(Context, chained_last_eip)));
        Assembler::Label site, request;
        a.adr(V0, site);
        a.str_x(V0, host::CTX, ctx(offsetof(Context, came_from)));
        a.bind(site);
        a.b(request);
        a.bind(request);
        a.str_x(V0, host::CTX, ctx(offsetof(Context, link_site)));
        a.mov_imm32(V1, e.eip);
        a.str_w(V1, host::CTX, ctx(offsetof(Context, link_eip)));
        exit_tail(0, 0, true, 0, REASON_NEXT, false, e.eip, 0, WHY_LINK);
    }

    // dst = all of EFLAGS as the lazily kept operation k leaves them. Inline for ADD, SUB, AND, OR,
    // XOR, INC and DEC, with Tsugaru's formulas (i486.cpp AddDword and the rest, and the AF of
    // need_af()); through Tsugaru's own function otherwise, which takes the caller-saved
    // registers. dst may be FLAGS.
    void flags_into(uint32_t k, Reg dst) {
        if (k == 0) {
            if (dst != host::FLAGS) a.mov(false, dst, host::FLAGS);
            return;
        }
        const FlagOp op = flag_op(k);
        const uint32_t bits = flag_bits(k), sign = bits - 1;
        const Reg A = host::FLAG_A, B = host::FLAG_B, R = host::FLAG_R;
        switch (op) {
        case FLAGS_ADD: case FLAGS_SUB: case FLAGS_AND: case FLAGS_OR: case FLAGS_XOR: case FLAGS_INC: case FLAGS_DEC:
            break;
        default:
            // The clock limit lives in a register the call may take.
            call_spilled(k);
            if (dst != V0) a.mov(false, dst, V0);
            return;
        }
        const bool logic = op == FLAGS_AND || op == FLAGS_OR || op == FLAGS_XOR;
        const bool incdec = op == FLAGS_INC || op == FLAGS_DEC;
        // The flags the operation sets, cleared: OF SF ZF AF PF CF, without AF for the logic
        // operations and without CF for INC and DEC.
        a.mov_imm32(T1, logic ? 0x8C5u : incdec ? 0x8D4u : 0x8D5u);
        a.bic(false, V3, host::FLAGS, T1);
        // ZF, SF and PF from the result.
        a.cmp_imm(false, R, 0);
        a.cset(false, T0, EQ);
        a.orr(false, V3, V3, T0, LSL, 6);
        a.ubfx(false, T0, R, sign, 1);
        a.orr(false, V3, V3, T0, LSL, 7);
        a.and_imm(false, T0, R, 0xFF);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, parity)));
        a.ldrb_r(T0, IP0, T0);
        a.orr(false, V3, V3, T0, LSL, 2);
        if (!logic) {
            // AF: the carry out of bit 3.
            a.eor(false, T0, A, B);
            a.eor(false, T0, T0, R);
            a.and_imm(false, T0, T0, 0x10);
            a.orr(false, V3, V3, T0);
        }
        if (op == FLAGS_ADD || op == FLAGS_SUB) {
            // CF: value1<prevValue for ADD, value1>prevValue for SUB.
            a.cmp(false, R, A);
            a.cset(false, T0, op == FLAGS_ADD ? LO : HI);
            a.orr(false, V3, V3, T0);
            // OF: ADD, the operands' signs agree and the result's differs; SUB, the operands'
            // signs differ and the result's differs from the first's.
            if (op == FLAGS_ADD) a.eor(false, T0, A, R);
            else a.eor(false, T0, A, B);
            if (op == FLAGS_ADD) a.eor(false, T1, B, R);
            else a.eor(false, T1, A, R);
            a.and_(false, T0, T0, T1);
            a.ubfx(false, T0, T0, sign, 1);
            a.orr(false, V3, V3, T0, LSL, 11);
        } else if (incdec) {
            // OF: INC gives the sign bit alone, DEC the largest positive value.
            cmp_const(R, op == FLAGS_INC ? 1u << sign : (1u << sign) - 1);
            a.cset(false, T0, EQ);
            a.orr(false, V3, V3, T0, LSL, 11);
        }
        a.mov(false, dst, V3);
    }

    // w0 = EFLAGS of the lazily kept operation kind; clobbers the caller-saved registers.
    void call_materialize(uint32_t k) {
        a.mov(true, V0, host::CTX);
        a.mov_imm32(V1, k);
        a.mov(false, V2, host::FLAGS);
        a.mov(false, V3, host::FLAG_A);
        a.mov(false, OFF, host::FLAG_B);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, materialize)));
        a.blr(IP0);
    }

    // --- Small helpers ------------------------------------------------------------------------

    void add_const(Reg rd, Reg rn, uint32_t value) {
        if (value == 0) {
            if (rd != rn) a.mov(false, rd, rn);
        } else if (value < 4096) {
            a.add_imm(false, rd, rn, value);
        } else if (-value < 4096) {
            a.sub_imm(false, rd, rn, -value);
        } else {
            const Reg scratch = rn == T1 ? T0 : T1;
            a.mov_imm32(scratch, value);
            a.add(false, rd, rn, scratch);
        }
    }
    void and_const(Reg rd, Reg rn, uint32_t value) {
        if (!a.try_and_imm(false, rd, rn, value)) {
            a.mov_imm32(T1, value);
            a.and_(false, rd, rn, T1);
        }
    }
    void cmp_const(Reg rn, uint32_t value) {
        if (value < 4096) {
            a.cmp_imm(false, rn, value);
        } else {
            a.mov_imm32(T1, value);
            a.cmp(false, rn, T1);
        }
    }
    static uint32_t mask_of(uint32_t bits) { return bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1; }
    void mask(Reg rd, Reg rn, uint32_t bits) {
        if (bits == 8) a.uxtb(rd, rn);
        else if (bits == 16) a.uxth(rd, rn);
        else if (rd != rn) a.mov(false, rd, rn);
    }

    // --- Guest registers ----------------------------------------------------------------------

    // The value of a register operand, zero-extended, in dst.
    void read_reg(const Operand &op, Reg dst) {
        switch (op.operandType) {
        case i486DXCommon::OPER_REG32: a.mov(false, dst, gr(op.reg - i486DXCommon::REG_EAX)); break;
        case i486DXCommon::OPER_REG16: a.uxth(dst, gr(op.reg - i486DXCommon::REG_AX)); break;
        case i486DXCommon::OPER_REG8: read_reg8(op.reg - i486DXCommon::REG_AL, dst); break;
        default: throw std::logic_error("read_reg: not a register");
        }
    }
    void write_reg(const Operand &op, Reg src) {
        switch (op.operandType) {
        case i486DXCommon::OPER_REG32: a.mov(false, gr(op.reg - i486DXCommon::REG_EAX), src); break;
        case i486DXCommon::OPER_REG16: a.bfi(false, gr(op.reg - i486DXCommon::REG_AX), src, 0, 16); break;
        case i486DXCommon::OPER_REG8: write_reg8(op.reg - i486DXCommon::REG_AL, src); break;
        default: throw std::logic_error("write_reg: not a register");
        }
    }
    // 8-bit registers by number: AL CL DL BL AH CH DH BH.
    void read_reg8(uint32_t n, Reg dst) { a.ubfx(false, dst, gr(n & 3), n >= 4 ? 8 : 0, 8); }
    void write_reg8(uint32_t n, Reg src) { a.bfi(false, gr(n & 3), src, n >= 4 ? 8 : 0, 8); }
    // A general register by number (0-7) at an operand size, read zero-extended or written.
    void read_gpr(uint32_t n, uint32_t bits, Reg dst) {
        if (bits == 8) read_reg8(n, dst);
        else if (bits == 16) a.uxth(dst, gr(n));
        else a.mov(false, dst, gr(n));
    }
    void write_gpr(uint32_t n, uint32_t bits, Reg src) {
        if (bits == 8) write_reg8(n, src);
        else if (bits == 16) a.bfi(false, gr(n), src, 0, 16);
        else a.mov(false, gr(n), src);
    }

    // --- Addresses ----------------------------------------------------------------------------

    // The segment register an operand's address uses, as ExtractSegmentAndOffset picks it.
    int segment_of(const Operand &op) const {
        const uint32_t index = cpu.segPrefixToSregIndex[inst->segOverride];
        const i486DXCommon::SegmentRegister *seg =
            index == i486DXCommon::NUM_SEGMENT_REGISTERS ? cpu.baseRegisterToDefaultSegment[op.baseReg]
                                                         : cpu.sregIndexToSregPtrTable[index];
        return int(seg - &st.sreg[0]);
    }
    // As SegmentOverrideDefaultDS picks it.
    int segment_default_ds() const {
        const uint32_t index = cpu.segPrefixToSregIndex[inst->segOverride];
        if (index == i486DXCommon::NUM_SEGMENT_REGISTERS) return i486DXCommon::REG_DS - i486DXCommon::REG_SEGMENT_REG_BASE;
        return int(cpu.sregIndexToSregPtrTable[index] - &st.sreg[0]);
    }
    // The operand's offset, before masking to the address size, as ExtractSegmentAndOffset adds it.
    void effective_offset(const Operand &op, Reg dst) {
        const uint32_t base = op.baseReg & 15, index = op.indexReg & 15;
        if (base && index) a.add(false, dst, gr(base - 1), gr(index - 1), LSL, op.indexShift);
        else if (base) a.mov(false, dst, gr(base - 1));
        else if (index) a.lsl_imm(false, dst, gr(index - 1), op.indexShift);
        else return a.mov_imm32(dst, uint32_t(op.offset));
        add_const(dst, dst, uint32_t(op.offset));
    }

    // phys = the page-table cache's translation of lin; stops before the instruction when the
    // cache does not hold the page (the interpreter then reads the page table).
    void translate_page(Reg lin, Reg phys, Assembler::Label &fail) {
        a.lsr_imm(false, T0, lin, 12);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, page_cache)));
        a.add(true, IP0, IP0, T0, LSL, 4);
        a.ldr_w(T0, IP0, offsetof(i486DXCommon::PageTableCache, valid));
        a.ldr_w(T1, host::STATE, page_counter_offset());
        a.cmp(false, T0, T1);
        a.b_cond(LO, fail);
        a.ldr_w(T0, IP0, offsetof(i486DXCommon::PageTableCache, info) + offsetof(i486DXCommon::PageTableEntry, table));
        a.and_imm(false, phys, T0, 0xFFFFF000u);
        a.bfxil(false, phys, lin, 0, 12);
    }
    // ADDR = the host address of phys in main RAM; stops when the slot is not main RAM's.
    void main_ram(Reg phys, Assembler::Label &fail) {
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, access)));
        a.lsr_imm(false, T0, phys, 12);
        a.ldr_x_r(IP0, IP0, T0, UXTW, true);
        a.ldr_x(IP1, host::CTX, ctx(offsetof(Context, main_ram)));
        a.cmp(true, IP0, IP1);
        a.b_cond(NE, fail);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, ram)));
        a.add_ext(true, ADDR, IP0, phys, UXTW);
    }
    // ADDR = where main RAM has linear address lin, by the TLB; on a miss, by the page-table cache
    // and Memory's slots as the interpreter finds it, which fills the TLB, or the instruction
    // stops before it when the interpreter would not reach main RAM by a pointer there.
    void ram_address(Reg lin) {
        Assembler::Label *fail = &bail();
        cold_labels.push_back(std::make_unique<Assembler::Label>());
        Assembler::Label *miss = cold_labels.back().get();
        cold_labels.push_back(std::make_unique<Assembler::Label>());
        Assembler::Label *back = cold_labels.back().get();
        a.lsr_imm(false, T0, lin, 12);
        a.add(false, T1, T0, host::TLB_TAG);
        a.and_imm(false, T0, T0, kTlbEntries - 1);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, tlb)));
        a.add(true, IP0, IP0, T0, LSL, 4);
        a.ldr_w(IP1, IP0, offsetof(TlbEntry, tag));
        a.cmp(false, IP1, T1);
        a.b_cond(NE, *miss);
        a.ldr_x(IP1, IP0, offsetof(TlbEntry, addend));
        a.add_ext(true, ADDR, IP1, lin, UXTW);
        a.bind(*back);
        const bool paging = mode.paging;
        cold.push_back([this, lin, fail, miss, back, paging] {
            a.bind(*miss);
            if (paging) {
                translate_page(lin, V4, *fail);
                main_ram(V4, *fail);
            } else {
                main_ram(lin, *fail);
            }
            a.lsr_imm(false, T0, lin, 12);
            a.add(false, T1, T0, host::TLB_TAG);
            a.and_imm(false, T0, T0, kTlbEntries - 1);
            a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, tlb)));
            a.add(true, IP0, IP0, T0, LSL, 4);
            a.str_w(T1, IP0, offsetof(TlbEntry, tag));
            a.sub_ext(true, IP1, ADDR, lin, UXTW);
            a.str_x(IP1, IP0, offsetof(TlbEntry, addend));
            a.b(*back);
        });
    }
    // Code emitted after the block's exits: the slow ways of ram_address().
    std::vector<std::function<void()>> cold;
    std::vector<std::unique_ptr<Assembler::Label>> cold_labels;

    // Stops before a store of bytes at ADDR into the running block's own code.
    void code_check(uint32_t bytes) {
        a.ldp_x(IP0, IP1, host::CTX, int32_t(offsetof(Context, code_start)));
        a.sub(true, IP0, ADDR, IP0);
        a.add_imm(true, IP0, IP0, bytes - 1);
        a.add_imm(true, IP1, IP1, bytes - 1);
        a.cmp(true, IP0, IP1);
        a.b_cond(LO, bail());
    }
    // If lin is in the stack window (and, for more than a byte, the access fits its page, as
    // FetchWord/StoreWord check), ADDR = its host address and a branch to hit; else falls through.
    void stack_window(Reg lin, uint32_t bytes, Assembler::Label &hit) {
        Assembler::Label miss;
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, stack_window)));
        a.ldr_w(T0, IP0, uint32_t(MemoryAccess::MemoryWindow::KairoLinearBaseAddrOffset()));
        a.and_imm(false, T1, lin, 0xFFFFF000u);
        a.cmp(false, T1, T0);
        a.b_cond(NE, miss);
        a.and_imm(false, T1, lin, 0xFFF);
        if (bytes > 1) {
            a.cmp_imm(false, T1, 4096 - bytes);
            a.b_cond(HI, miss);
        }
        a.ldr_x(IP1, IP0, offsetof(MemoryAccess::MemoryWindow, ptr));
        a.add_ext(true, ADDR, IP1, T1, UXTW);
        a.b(hit);
        a.bind(miss);
    }

    // FetchByte/Word/Dword(addressSize, sreg[seg], OFF): the value in dst. OFF is masked.
    void fetch(uint32_t bytes, int seg, Reg dst) {
        Assembler::Label at;
        if (inst->addressSize == 16) a.uxth(OFF, OFF);
        // SegmentReadException: an offset beyond the limit raises #GP.
        if (specialize) {
            const uint32_t max = segment_max(seg);
            if (max != 0xFFFFFFFFu) {
                cmp_const(OFF, max);
                a.b_cond(HI, bail());
            }
        } else {
            a.ldr_w(T0, host::STATE, seg_max(seg));
            a.cmp(false, OFF, T0);
            a.b_cond(HI, bail());
        }
        add_segment_base(LIN, OFF, seg);
        stack_window(LIN, bytes, at);
        if (mode.paging) {
            // FetchWord and FetchDword read a value that may cross the page byte by byte; the
            // physical address's low 12 bits are the linear address's.
            if (bytes == 2) page_end_check(LIN, 0xFFC);
            if (bytes == 4) page_end_check(LIN, 0xFF8);
        } else if (bytes > 1) {
            page_end_check(LIN, 0x1000 - bytes);
        }
        ram_address(LIN);
        a.bind(at);
        load(bytes, dst, ADDR);
    }
    // Stops when the low 12 bits of r are above limit.
    void page_end_check(Reg r, uint32_t limit) {
        a.and_imm(false, T0, r, 0xFFF);
        a.cmp_imm(false, T0, limit);
        a.b_cond(HI, bail());
    }
    void load(uint32_t bytes, Reg dst, Reg addr) {
        if (bytes == 1) a.ldrb(dst, addr);
        else if (bytes == 2) a.ldrh(dst, addr);
        else a.ldr_w(dst, addr);
    }
    void store(uint32_t bytes, Reg src, Reg addr) {
        if (bytes == 1) a.strb(src, addr);
        else if (bytes == 2) a.strh(src, addr);
        else a.str_w(src, addr);
    }

    // StoreByte/Word/Dword(addressSize, sreg[seg], OFF), its checks: ADDR = where, SEFX = what it
    // adds to Memory::sideEffects. commit_store() stores.
    void prepare_store(uint32_t bytes, int seg) {
        Assembler::Label window, done;
        if (inst->addressSize == 16) a.uxth(OFF, OFF);
        add_segment_base(LIN, OFF, seg);
        stack_window(LIN, bytes, window);
        if (mode.paging) {
            if (bytes == 2) page_end_check(LIN, 0xFFE);
            if (bytes == 4) page_end_check(LIN, 0xFFC);
        } else if (bytes > 1) {
            page_end_check(LIN, 0x1000 - bytes);
        }
        ram_address(LIN);
        // Store* counts the store, and Memory::Store* counts it again.
        a.movz(false, SEFX, 2, 0);
        a.b(done);
        a.bind(window);
        a.movz(false, SEFX, 1, 0);
        a.bind(done);
        code_check(bytes);
    }
    void commit_store(uint32_t bytes, Reg src) {
        store(bytes, src, ADDR);
        add_side_effects(SEFX);
        step_code_generation();
    }
    void step_code_generation() {
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, code_generation)));
        a.add_imm(true, IP0, IP0, 1);
        a.str_x(IP0, host::CTX, ctx(offsetof(Context, code_generation)));
    }
    void add_side_effects(Reg count) {
        has_effects = true;
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, side_effects)));
        a.ldr_x(IP1, IP0);
        a.add_ext(true, IP1, IP1, count, UXTW);
        a.str_x(IP1, IP0);
    }
    void add_side_effect() {
        a.movz(false, SEFX, 1, 0);
        add_side_effects(SEFX);
    }

    // GetOperandPointer*(addressSize, sreg[seg], OFF): ADDR = the operand in main RAM; stops
    // where the function returns nullptr (the interpreter then evaluates and stores the operand
    // instead). The store through it and the side effect are the caller's.
    void pointer(PtrFn fn, int seg, bool for_write) {
        if (inst->addressSize == 16) a.uxth(OFF, OFF);
        // offset<minLimit+minLimitAdjust || maxLimit<offset+3, in 32 bits.
        if (specialize) {
            const uint32_t min = segment_min(seg), max = segment_max(seg);
            if (min != 0) {
                cmp_const(OFF, min);
                a.b_cond(LO, bail());
            }
            if (max != 0xFFFFFFFFu) {
                a.add_imm(false, T1, OFF, 3);
                a.mov_imm32(T0, max);
                a.cmp(false, T0, T1);
                a.b_cond(LO, bail());
            }
        } else {
            a.ldr_w(T0, host::STATE, seg_min(seg));
            a.ldr_w(T1, host::STATE, seg_min_adjust(seg));
            a.add(false, T0, T0, T1);
            a.cmp(false, OFF, T0);
            a.b_cond(LO, bail());
            a.ldr_w(T0, host::STATE, seg_max(seg));
            a.add_imm(false, T1, OFF, 3);
            a.cmp(false, T0, T1);
            a.b_cond(LO, bail());
        }
        add_segment_base(LIN, OFF, seg);
        if (fn == PtrFn::P16) page_end_check(LIN, 0xFFE);
        else if (fn != PtrFn::P8) page_end_check(LIN, 0xFFC);
        ram_address(LIN);
        if (for_write) code_check(fn == PtrFn::P8 ? 1 : fn == PtrFn::P16 ? 2 : 4);
    }

    // Push16/Push32 through the stack window: checks, then commit_push() stores and moves ESP.
    void prepare_push(uint32_t bytes) {
        const Reg esp = gr(4);
        a.sub_imm(false, V4, esp, bytes);
        stack_address(V4);
        // GetStackAccessPointer
        a.and_imm(false, T1, LIN, 0xFFF);
        a.cmp_imm(false, T1, 0x1000 - bytes);
        a.b_cond(HI, bail());
        stack_window_or_bail(LIN);
        code_check(bytes);
    }
    void commit_push(uint32_t bytes, Reg src) {
        store(bytes, src, ADDR);
        step_code_generation();
        a.mov(false, gr(4), V4);
    }
    // Pop16/Pop32: the value in dst, ESP moved on.
    void pop(uint32_t bytes, Reg dst) {
        const Reg esp = gr(4);
        stack_address(esp);
        a.and_imm(false, T1, LIN, 0xFFF);
        a.cmp_imm(false, T1, 0x1000 - bytes);
        a.b_cond(HI, bail());
        stack_window_or_bail(LIN);
        load(bytes, dst, ADDR);
        a.add_imm(false, esp, esp, bytes);
    }
    // LIN = SS base + the stack offset in esp, masked to the stack's address size.
    void stack_address(Reg esp) {
        const int ss = i486DXCommon::REG_SS - i486DXCommon::REG_SEGMENT_REG_BASE;
        if (mode.stack_address_size == 16) {
            a.uxth(T1, esp);
            add_segment_base(LIN, T1, ss);
        } else {
            add_segment_base(LIN, esp, ss);
        }
    }
    // ADDR = lin in the stack window; stops when the window is elsewhere (the interpreter moves it).
    void stack_window_or_bail(Reg lin) {
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, stack_window)));
        a.ldr_x(IP1, IP0, offsetof(MemoryAccess::MemoryWindow, ptr));
        a.cbz(true, IP1, bail());
        a.ldr_w(T0, IP0, uint32_t(MemoryAccess::MemoryWindow::KairoLinearBaseAddrOffset()));
        a.and_imm(false, T1, lin, 0xFFFFF000u);
        a.cmp(false, T1, T0);
        a.b_cond(NE, bail());
        a.and_imm(false, T1, lin, 0xFFF);
        a.add_ext(true, ADDR, IP1, T1, UXTW);
    }

    // --- Operands -----------------------------------------------------------------------------

    // EvaluateOperandRegOrMem8/16/32 or EvaluateOperand of a register or memory operand: dst.
    void read_rm(const Operand &op, uint32_t bytes, Reg dst) {
        if (is_reg(op)) return read_reg(op, dst);
        effective_offset(op, OFF);
        fetch(bytes, segment_of(op), dst);
    }
    // The checks of StoreOperandValue* to a register or memory operand; commit_rm() stores.
    void prepare_rm_store(const Operand &op, uint32_t bytes) {
        if (is_reg(op)) return;
        effective_offset(op, OFF);
        prepare_store(bytes, segment_of(op));
    }
    void commit_rm(const Operand &op, uint32_t bytes, Reg src) {
        if (is_reg(op)) return write_reg(op, src);
        commit_store(bytes, src);
    }
    // GetOperandPointer* of a register or memory operand, its checks; read_ptr/write_ptr use it.
    bool pointer_ok(PtrFn fn, const Operand &op) const {
        switch (op.operandType) {
        case i486DXCommon::OPER_REG8: return fn == PtrFn::P8;
        case i486DXCommon::OPER_REG16: return fn == PtrFn::P16 || fn == PtrFn::P16OR32;
        case i486DXCommon::OPER_REG32: return fn == PtrFn::P32 || fn == PtrFn::P16OR32;
        case i486DXCommon::OPER_ADDR: return true;
        default: return false;
        }
    }
    void prepare_ptr(PtrFn fn, const Operand &op, bool for_write) {
        if (is_reg(op)) return;
        effective_offset(op, OFF);
        pointer(fn, segment_of(op), for_write);
    }
    void read_ptr(const Operand &op, uint32_t bytes, Reg dst) {
        if (is_reg(op)) return read_reg(op, dst);
        load(bytes, dst, ADDR);
    }
    // A store through the pointer: a register, or memory with GetOperandPointer's side effect.
    void write_ptr(const Operand &op, uint32_t bytes, Reg src, bool counted) {
        if (is_reg(op)) return write_reg(op, src);
        store(bytes, src, ADDR);
        if (counted) add_side_effect();
        step_code_generation();
    }

    // --- Flags --------------------------------------------------------------------------------

    void call_spilled(uint32_t k) {
        a.str_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
        call_materialize(k);
        a.ldr_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
    }
    // FLAGS = all of EFLAGS.
    void need_all() {
        if (kind == 0) return;
        switch (flag_op(kind)) {
        case FLAGS_ADD: case FLAGS_SUB: case FLAGS_AND: case FLAGS_OR: case FLAGS_XOR: case FLAGS_INC: case FLAGS_DEC:
            flags_into(kind, host::FLAGS);
            break;
        default:
            call_spilled(kind);
            a.mov(false, host::FLAGS, V0);
            break;
        }
        kind = 0;
    }
    // FLAGS holds CF as it is now.
    void need_cf() {
        const uint32_t bits = flag_bits(kind);
        switch (flag_op(kind)) {
        case FLAGS_NONE: case FLAGS_INC: case FLAGS_DEC: return;
        case FLAGS_ADD:
            // value1<prevValue
            a.cmp(false, host::FLAG_R, host::FLAG_A);
            a.cset(false, T0, LO);
            a.bfi(false, host::FLAGS, T0, 0, 1);
            return;
        case FLAGS_SUB:
            // value1>prevValue, which is prevValue < value2 at the operand size
            mask(T1, host::FLAG_B, bits);
            a.cmp(false, host::FLAG_A, T1);
            a.cset(false, T0, LO);
            a.bfi(false, host::FLAGS, T0, 0, 1);
            return;
        case FLAGS_AND: case FLAGS_OR: case FLAGS_XOR:
            a.and_imm(false, host::FLAGS, host::FLAGS, ~F_CF);
            return;
        default:
            need_all();
            return;
        }
    }
    // FLAGS holds AF as it is now.
    void need_af() {
        switch (flag_op(kind)) {
        case FLAGS_NONE: case FLAGS_AND: case FLAGS_OR: case FLAGS_XOR:
        case FLAGS_ROL: case FLAGS_ROR: case FLAGS_SHL: case FLAGS_SHR: case FLAGS_SAR: return;
        case FLAGS_ADD: case FLAGS_SUB: case FLAGS_INC: case FLAGS_DEC:
            // The carry out of bit 3, which is how Tsugaru's AF works out for these.
            a.eor(false, T0, host::FLAG_A, host::FLAG_B);
            a.eor(false, T0, T0, host::FLAG_R);
            a.lsr_imm(false, T0, T0, 4);
            a.bfi(false, host::FLAGS, T0, 4, 1);
            return;
        default:
            need_all();
            return;
        }
    }
    // The flags are now op's on a and b, with result r (at the operand size).
    void set_flags(FlagOp op, uint32_t bits, Reg a_, Reg b_, Reg r_) {
        if (a_ != host::FLAG_A) a.mov(false, host::FLAG_A, a_);
        if (b_ != host::FLAG_B) a.mov(false, host::FLAG_B, b_);
        if (r_ != host::FLAG_R) a.mov(false, host::FLAG_R, r_);
        kind = flag_kind(op, bits);
    }

    static FlagOp alu_op(uint32_t n) {
        static const FlagOp ops[8] = {FLAGS_ADD, FLAGS_OR, FLAGS_ADC, FLAGS_SBB, FLAGS_AND, FLAGS_SUB, FLAGS_XOR, FLAGS_SUB};
        return ops[n];
    }
    // What the ALU operation n needs of the flags before it, made ready. It may call the
    // materializer, which takes the scratch registers, so an instruction does it first.
    void prep_alu(uint32_t n) {
        const FlagOp op = alu_op(n);
        if (op == FLAGS_ADC || op == FLAGS_SBB) need_all();
        else if (op == FLAGS_AND || op == FLAGS_OR || op == FLAGS_XOR) need_af();
    }
    // The ALU operation n (ADD OR ADC SBB AND SUB XOR CMP) at bits on x (zero-extended) and y (as
    // the interpreter passes it): result in dst (masked), flags kept. prep_alu(n) came first.
    void alu(uint32_t n, uint32_t bits, Reg x, Reg y, Reg dst) {
        const FlagOp op = alu_op(n);
        // x and y may be FLAG registers the record is about to take; compute into a temporary.
        switch (op) {
        case FLAGS_ADD: a.add(false, T1, x, y); break;
        case FLAGS_SUB: a.sub(false, T1, x, y); break;
        case FLAGS_AND: a.and_(false, T1, x, y); break;
        case FLAGS_OR: a.orr(false, T1, x, y); break;
        case FLAGS_XOR: a.eor(false, T1, x, y); break;
        case FLAGS_ADC:
            a.and_imm(false, T0, host::FLAGS, F_CF);
            a.add(false, T1, x, y);
            a.add(false, T1, T1, T0);
            break;
        case FLAGS_SBB:
            a.and_imm(false, T0, host::FLAGS, F_CF);
            a.sub(false, T1, x, y);
            a.sub(false, T1, T1, T0);
            break;
        default: throw std::logic_error("alu");
        }
        mask(T1, T1, bits);
        // The record takes x (already masked by its reader) and y as passed.
        if (x == host::FLAG_B || y == host::FLAG_A) {
            a.mov(false, T0, x);
            a.mov(false, host::FLAG_B, y);
            a.mov(false, host::FLAG_A, T0);
            a.mov(false, host::FLAG_R, T1);
            kind = flag_kind(op, bits);
        } else {
            set_flags(op, bits, x, y, T1);
        }
        if (dst != host::FLAG_R) a.mov(false, dst, T1);
    }
    // INC (dec false) or DEC at bits on x: result in dst. need_cf() came first.
    void inc_dec(bool dec, uint32_t bits, Reg x, Reg dst) {
        if (dec) a.sub_imm(false, T1, x, 1);
        else a.add_imm(false, T1, x, 1);
        mask(T1, T1, bits);
        a.mov(false, host::FLAG_A, x);
        a.movz(false, host::FLAG_B, 1, 0);
        a.mov(false, host::FLAG_R, T1);
        kind = flag_kind(dec ? FLAGS_DEC : FLAGS_INC, bits);
        if (dst != host::FLAG_R) a.mov(false, dst, T1);
    }

    // Conditions by the low nibble of Jcc/SETcc: O NO B AE E NE BE A S NS P NP L GE LE G.
    // prepare_condition() materializes what branch_if() cannot read from the record.
    void prepare_condition(uint32_t cc) {
        switch (flag_op(kind)) {
        case FLAGS_NONE: case FLAGS_SUB: case FLAGS_AND: case FLAGS_OR: case FLAGS_XOR: return;
        case FLAGS_ADD:
            if (cc == 0 || cc == 1 || cc == 2 || cc == 3 || cc == 4 || cc == 5 || cc == 8 || cc == 9 || cc == 10 || cc == 11) return;
            return need_all();
        case FLAGS_INC: case FLAGS_DEC:
            if (cc <= 11) return;
            return need_all();
        case FLAGS_ROL: case FLAGS_ROR: case FLAGS_SHL: case FLAGS_SHR: case FLAGS_SAR:
            // ZF, SF and PF: the shifts' from the result, the rotates' kept in the base.
            if (cc == 4 || cc == 5 || cc == 8 || cc == 9 || cc == 10 || cc == 11) return;
            return need_all();
        default:
            return need_all();
        }
    }
    void branch_parity(bool even, Assembler::Label &target) {
        a.and_imm(false, T0, host::FLAG_R, 0xFF);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, parity)));
        a.ldrb_r(T0, IP0, T0);
        if (even) a.cbnz(false, T0, target);
        else a.cbz(false, T0, target);
    }
    void branch_if(uint32_t cc, Assembler::Label &target) {
        const uint32_t bits = flag_bits(kind);
        const uint32_t sign = bits - 1;
        switch (flag_op(kind)) {
        case FLAGS_NONE: return branch_eflags(cc, target);
        case FLAGS_SUB: {
            if (cc == 10 || cc == 11) return branch_parity(cc == 10, target);
            if (bits == 32) {
                a.cmp(false, host::FLAG_A, host::FLAG_B);
            } else {
                a.lsl_imm(false, T0, host::FLAG_A, 32 - bits);
                a.lsl_imm(false, T1, host::FLAG_B, 32 - bits);
                a.cmp(false, T0, T1);
            }
            static const Cond map[16] = {VS, VC, LO, HS, EQ, NE, LS, HI, MI, PL, AL, AL, LT, GE, LE, GT};
            return a.b_cond(map[cc], target);
        }
        case FLAGS_AND: case FLAGS_OR: case FLAGS_XOR: {
            // CF and OF clear; SF, ZF and PF from the result.
            Assembler::Label skip;
            switch (cc) {
            case 0: case 2: return;                             // O, B: never
            case 1: case 3: return a.b(target);                 // NO, AE: always
            case 4: case 6: return a.cbz(false, host::FLAG_R, target);   // E, BE
            case 5: case 7: return a.cbnz(false, host::FLAG_R, target);  // NE, A
            case 8: case 12: return a.tbnz(host::FLAG_R, sign, target);  // S, L
            case 9: case 13: return a.tbz(host::FLAG_R, sign, target);   // NS, GE
            case 10: case 11: return branch_parity(cc == 10, target);
            case 14:                                            // LE: ZF or SF
                a.cbz(false, host::FLAG_R, target);
                return a.tbnz(host::FLAG_R, sign, target);
            case 15:                                            // G
                a.cbz(false, host::FLAG_R, skip);
                a.tbnz(host::FLAG_R, sign, skip);
                a.b(target);
                return a.bind(skip);
            }
            break;
        }
        case FLAGS_ADD:
            switch (cc) {
            case 0: case 1:  // OF: the operands' signs agree and the result's differs
                a.eor(false, T0, host::FLAG_A, host::FLAG_R);
                a.eor(false, T1, host::FLAG_B, host::FLAG_R);
                a.and_(false, T0, T0, T1);
                return cc == 0 ? a.tbnz(T0, sign, target) : a.tbz(T0, sign, target);
            case 2: case 3:  // CF: value1<prevValue
                a.cmp(false, host::FLAG_R, host::FLAG_A);
                return a.b_cond(cc == 2 ? LO : HS, target);
            case 4: return a.cbz(false, host::FLAG_R, target);
            case 5: return a.cbnz(false, host::FLAG_R, target);
            case 8: return a.tbnz(host::FLAG_R, sign, target);
            case 9: return a.tbz(host::FLAG_R, sign, target);
            case 10: case 11: return branch_parity(cc == 10, target);
            }
            break;
        case FLAGS_SHL: case FLAGS_SHR: case FLAGS_SAR:
            switch (cc) {
            case 4: return a.cbz(false, host::FLAG_R, target);
            case 5: return a.cbnz(false, host::FLAG_R, target);
            case 8: return a.tbnz(host::FLAG_R, sign, target);
            case 9: return a.tbz(host::FLAG_R, sign, target);
            case 10: case 11: return branch_parity(cc == 10, target);
            }
            break;
        case FLAGS_ROL: case FLAGS_ROR:
            if (cc == 4 || cc == 5 || cc == 8 || cc == 9 || cc == 10 || cc == 11) return branch_eflags(cc, target);
            break;
        case FLAGS_INC: case FLAGS_DEC: {
            Assembler::Label skip;
            const bool dec = flag_op(kind) == FLAGS_DEC;
            const uint32_t overflow = dec ? (1u << sign) - 1 : 1u << sign;
            switch (cc) {
            case 0: case 1:
                cmp_const(host::FLAG_R, overflow);
                return a.b_cond(cc == 0 ? EQ : NE, target);
            case 2: return a.tbnz(host::FLAGS, 0, target);
            case 3: return a.tbz(host::FLAGS, 0, target);
            case 4: return a.cbz(false, host::FLAG_R, target);
            case 5: return a.cbnz(false, host::FLAG_R, target);
            case 6:  // BE: CF or ZF
                a.tbnz(host::FLAGS, 0, target);
                return a.cbz(false, host::FLAG_R, target);
            case 7:  // A
                a.tbnz(host::FLAGS, 0, skip);
                a.cbz(false, host::FLAG_R, skip);
                a.b(target);
                return a.bind(skip);
            case 8: return a.tbnz(host::FLAG_R, sign, target);
            case 9: return a.tbz(host::FLAG_R, sign, target);
            case 10: case 11: return branch_parity(cc == 10, target);
            }
            break;
        }
        default: break;
        }
        throw std::logic_error("branch_if: condition not prepared");
    }
    void branch_eflags(uint32_t cc, Assembler::Label &target) {
        const Reg f = host::FLAGS;
        switch (cc) {
        case 0: return a.tbnz(f, 11, target);
        case 1: return a.tbz(f, 11, target);
        case 2: return a.tbnz(f, 0, target);
        case 3: return a.tbz(f, 0, target);
        case 4: return a.tbnz(f, 6, target);
        case 5: return a.tbz(f, 6, target);
        case 6: a.mov_imm32(T0, F_CF | F_ZF); a.tst(false, f, T0); return a.b_cond(NE, target);
        case 7: a.mov_imm32(T0, F_CF | F_ZF); a.tst(false, f, T0); return a.b_cond(EQ, target);
        case 8: return a.tbnz(f, 7, target);
        case 9: return a.tbz(f, 7, target);
        case 10: return a.tbnz(f, 2, target);
        case 11: return a.tbz(f, 2, target);
        case 12: case 13:  // SF != OF
            a.eor(false, T0, f, f, LSR, 4);
            return cc == 12 ? a.tbnz(T0, 7, target) : a.tbz(T0, 7, target);
        case 14: case 15:  // ZF or SF != OF
            a.eor(false, T0, f, f, LSR, 4);
            a.and_imm(false, T0, T0, 0x80);
            a.and_imm(false, T1, f, F_ZF);
            a.orr(false, T0, T0, T1);
            return cc == 14 ? a.cbnz(false, T0, target) : a.cbz(false, T0, target);
        }
    }
    // V0 = the condition, 0 or 1.
    void condition_value(uint32_t cc) {
        Assembler::Label yes, done;
        branch_if(cc, yes);
        a.movz(false, V0, 0, 0);
        a.b(done);
        a.bind(yes);
        a.movz(false, V0, 1, 0);
        a.bind(done);
    }

    // ROL ROR SHL SHR SAR (n of the group's REG: 0 1 4 5 7) at bits on x by count (0-31): the
    // result, masked, in dst, as Tsugaru's Rol/Ror/Shl/Shr/Sar compute it; the flags kept with the
    // whole EFLAGS before as base (need_all() came first).
    void shift(uint32_t n, uint32_t bits, Reg x, Reg count, Reg dst) {
        switch (n) {
        case 0: case 1:
            // A rotate of the operand repeated across 32 bits rotates the operand.
            if (bits == 8) {
                a.orr(false, T1, x, x, LSL, 8);
                a.orr(false, T1, T1, T1, LSL, 16);
            } else if (bits == 16) {
                a.orr(false, T1, x, x, LSL, 16);
            } else {
                a.mov(false, T1, x);
            }
            if (n == 0) {
                a.neg(false, T0, count);
                a.rorv(false, T1, T1, T0);
            } else {
                a.rorv(false, T1, T1, count);
            }
            break;
        case 4:
            a.lslv(false, T1, x, count);
            break;
        case 5:
            a.lsrv(false, T1, x, count);
            break;
        case 7:
            if (bits == 32) {
                a.asrv(false, T1, x, count);
            } else if (bits == 16) {
                // Sign-extended to 32 bits and shifted as unsigned.
                a.sxth(T1, x);
                a.lsrv(false, T1, T1, count);
            } else {
                // Sign-extended to 16 bits only, and shifted as unsigned.
                a.sxtb(T1, x);
                a.uxth(T1, T1);
                a.lsrv(false, T1, T1, count);
            }
            break;
        default: throw std::logic_error("shift");
        }
        mask(T1, T1, bits);
        static const FlagOp ops[8] = {FLAGS_ROL, FLAGS_ROR, FLAGS_NONE, FLAGS_NONE, FLAGS_SHL, FLAGS_SHR, FLAGS_NONE, FLAGS_SAR};
        a.mov(false, host::FLAG_A, x);
        a.mov(false, host::FLAG_B, count);
        a.mov(false, host::FLAG_R, T1);
        kind = flag_kind(ops[n], bits);
        if (dst != host::FLAG_R) a.mov(false, dst, T1);
    }

    // --- Instructions -------------------------------------------------------------------------
    // Each returns false, having emitted nothing, for an instruction it does not translate;
    // clocks is what the instruction takes when it does not leave the block.

    enum class Next { Continue, End };

    bool translate_one(uint32_t &clocks, Next &next, uint32_t next_eip);
    // Whether the interpreter may run an instruction inside a run (interpret_here()): not one
    // that waits (HLT), nor a REP string instruction, which the interpreter runs in bundles,
    // coming back to the same instruction.
    static bool interpretable(const Instruction &in) {
        const uint32_t op = in.RealOpCode();
        if (op == 0xF4) return false;
        const bool string = (op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF) || (op >= 0x6C && op <= 0x6F);
        if (string && in.instPrefix != 0) return false;
        return true;
    }

    // 64-bit constants the block loads, placed after it.
    struct Literal {
        Assembler::Label label;
        uint64_t value = 0;
    };
    std::vector<std::unique_ptr<Literal>> literals;
    Assembler::Label &literal(uint64_t value) {
        literals.push_back(std::make_unique<Literal>());
        literals.back()->value = value;
        return literals.back()->label;
    }

    void prologue(const uint8_t *code, uint32_t bytes);
    uint64_t *stamp = nullptr;
    // The segments the first pass found the block using (translate()).
    uint32_t check_segments = 0;
    // The block at code (avail bytes to the end of its code window), the guest's CS:start; with
    // check_bytes, it checks that many bytes of code at its entry.
    Result translate_block(const uint8_t *code, uint32_t avail, uint32_t start, uint32_t check_bytes);
};

namespace {
uint32_t simm8(const Instruction &inst) { return uint32_t(int32_t(int8_t(inst.EvalUimm8()))); }
} // namespace

bool Translator::Impl::translate_one(uint32_t &clocks, Next &next, uint32_t next_eip) {
    const Instruction &in = *inst;
    const Operand &o1 = *op1, &o2 = *op2;
    const uint32_t op = in.RealOpCode();
    const uint32_t osize = in.operandSize, obytes = osize / 8;
    next = Next::Continue;
    if (in.instPrefix != 0) return false;
    if (in.fwait) return false;

    // ALU r/m and register forms: ADD OR ADC SBB AND SUB XOR CMP.
    if (op < 0x40 && (op & 7) < 6) {
        const uint32_t n = op >> 3, form = op & 7;
        const bool update = n != 7;
        switch (form) {
        case 0: {  // RM8, R8: BINARYOP_RM8_R8
            if (!pointer_ok(PtrFn::P8, o1)) return false;
            clocks = (is_addr(o1) || is_addr(o2)) ? 3 : 1;
            prep_alu(n);
            prepare_ptr(PtrFn::P8, o1, update);
            read_ptr(o1, 1, V0);
            read_reg8(in.GetREG(), V1);
            alu(n, 8, V0, V1, V2);
            if (update) write_ptr(o1, 1, V2, true);
            return true;
        }
        case 1: {  // RM, R: BINARYOP_RM_FROM_R
            const PtrFn fn = osize == 16 ? PtrFn::P16 : PtrFn::P32;
            if (!pointer_ok(fn, o1)) return false;
            clocks = (is_addr(o1) || is_addr(o2)) ? 3 : 1;
            prep_alu(n);
            prepare_ptr(fn, o1, update);
            read_ptr(o1, obytes, V0);
            read_gpr(in.GetREG(), osize, V1);
            alu(n, osize, V0, V1, V2);
            if (update) write_ptr(o1, obytes, V2, true);
            return true;
        }
        case 2: {  // R8, RM8: BINARYOP_R8_RM8
            if (o1.operandType != i486DXCommon::OPER_REG8 || !(is_reg(o2) || is_addr(o2))) return false;
            clocks = (is_addr(o1) || is_addr(o2)) ? 3 : 1;
            prep_alu(n);
            read_rm(o2, 1, V1);
            read_reg8(in.GetREG(), V0);
            alu(n, 8, V0, V1, V2);
            if (update) write_reg8(in.GetREG(), V2);
            return true;
        }
        case 3: {  // R, RM: BINARYOP_R_FROM_RM
            if (!(is_reg(o2) || is_addr(o2))) return false;
            clocks = is_addr(o2) ? 3 : 1;
            prep_alu(n);
            read_rm(o2, obytes, V1);
            if (osize == 16) a.uxth(V1, V1);
            read_gpr(in.GetREG(), osize, V0);
            alu(n, osize, V0, V1, V2);
            if (update) write_gpr(in.GetREG(), osize, V2);
            return true;
        }
        case 4: {  // AL, imm8
            clocks = 1;
            prep_alu(n);
            read_reg8(0, V0);
            a.mov_imm32(V1, in.EvalUimm8());
            alu(n, 8, V0, V1, V2);
            if (update) write_reg8(0, V2);
            return true;
        }
        case 5: {  // eAX, imm
            clocks = 1;
            prep_alu(n);
            read_gpr(0, osize, V0);
            a.mov_imm32(V1, osize == 16 ? in.EvalUimm16() : in.EvalUimm32());
            alu(n, osize, V0, V1, V2);
            if (update) write_gpr(0, osize, V2);
            return true;
        }
        }
    }

    switch (op) {
    // --- TEST ---
    case 0x84:  // BINARYOP_RM8_R8(AndByte,2,false)
        if (!pointer_ok(PtrFn::P8, o1)) return false;
        clocks = (is_addr(o1) || is_addr(o2)) ? 2 : 1;
        prep_alu(4);
        prepare_ptr(PtrFn::P8, o1, false);
        read_ptr(o1, 1, V0);
        read_reg8(in.GetREG(), V1);
        alu(4, 8, V0, V1, V2);
        return true;
    case 0x85: {  // BINARYOP_RM_FROM_R(AndWord,AndDword,1,false)
        const PtrFn fn = osize == 16 ? PtrFn::P16 : PtrFn::P32;
        if (!pointer_ok(fn, o1)) return false;
        clocks = 1;
        prep_alu(4);
        prepare_ptr(fn, o1, false);
        read_ptr(o1, obytes, V0);
        read_gpr(in.GetREG(), osize, V1);
        alu(4, osize, V0, V1, V2);
        return true;
    }
    case 0xA8:
        clocks = 1;
        prep_alu(4);
        read_reg8(0, V0);
        a.mov_imm32(V1, in.EvalUimm8());
        alu(4, 8, V0, V1, V2);
        return true;
    case 0xA9:
        clocks = 1;
        prep_alu(4);
        read_gpr(0, osize, V0);
        a.mov_imm32(V1, osize == 16 ? in.EvalUimm16() : in.EvalUimm32());
        alu(4, osize, V0, V1, V2);
        return true;

    // --- ALU r/m, imm ---
    case 0x80: case 0x82: {
        const uint32_t n = in.GetREG();
        if (!pointer_ok(PtrFn::P8, o1)) return false;
        clocks = (is_addr(o1) || is_addr(o2)) ? 3 : 1;
        prep_alu(n);
        prepare_ptr(PtrFn::P8, o1, n != 7);
        read_ptr(o1, 1, V0);
        a.mov_imm32(V1, in.EvalUimm8());
        alu(n, 8, V0, V1, V2);
        if (n != 7) write_ptr(o1, 1, V2, true);
        return true;
    }
    case 0x81: case 0x83: {
        const uint32_t n = in.GetREG();
        const PtrFn fn = osize == 16 ? PtrFn::P16 : PtrFn::P32;
        if (!pointer_ok(fn, o1)) return false;
        clocks = (is_addr(o1) || is_addr(o2)) ? 3 : 1;
        prep_alu(n);
        prepare_ptr(fn, o1, n != 7);
        read_ptr(o1, obytes, V0);
        const uint32_t imm = op == 0x81 ? (osize == 16 ? in.EvalUimm16() : in.EvalUimm32()) : simm8(in);
        a.mov_imm32(V1, imm);
        alu(n, osize, V0, V1, V2);
        if (n != 7) write_ptr(o1, obytes, V2, true);
        return true;
    }

    // --- INC/DEC ---
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
        clocks = 1;
        need_cf();
        const uint32_t r = op & 7;
        read_gpr(r, osize, V0);
        inc_dec(op >= 0x48, osize, V0, V1);
        write_gpr(r, osize, V1);
        return true;
    }
    case 0xFE: {
        const uint32_t n = in.GetREG();
        if (n > 1 || !pointer_ok(PtrFn::P8, o1)) return false;
        clocks = is_addr(o1) ? 3 : 1;
        need_cf();
        prepare_ptr(PtrFn::P8, o1, true);
        read_ptr(o1, 1, V0);
        inc_dec(n == 1, 8, V0, V1);
        write_ptr(o1, 1, V1, true);
        return true;
    }
    case 0xFF: {
        const uint32_t n = in.GetREG();
        if (n <= 1) {
            const PtrFn fn = osize == 16 ? PtrFn::P16 : PtrFn::P32;
            if (!pointer_ok(fn, o1)) return false;
            clocks = is_addr(o1) ? 3 : 1;
            need_cf();
            prepare_ptr(fn, o1, true);
            read_ptr(o1, obytes, V0);
            inc_dec(n == 1, osize, V0, V1);
            write_ptr(o1, obytes, V1, true);
            return true;
        }
        if (n == 2 || n == 4) {  // CALL / JMP indirect
            if (!(is_reg(o1) || is_addr(o1))) return false;
            if (n == 2 && cpu.enableCallStack) return false;
            read_rm(o1, obytes, V3);
            if (n == 2) {
                prepare_push(obytes);
                a.mov_imm32(V1, next_eip);
                commit_push(obytes, V1);
            }
            if (osize == 16) a.uxth(V2, V3);
            else a.mov(false, V2, V3);
            exit_after_dynamic(5);
            next = Next::End;
            return true;
        }
        if (n == 6) {  // PUSH r/m
            if (!(is_reg(o1) || is_addr(o1))) return false;
            clocks = 4;
            read_rm(o1, obytes, V3);
            prepare_push(obytes);
            commit_push(obytes, V3);
            return true;
        }
        return false;
    }

    // --- Shifts and rotates: ROL_ROR_RCL_RCR_SAL_SAR_SHL_SHR_RM8 and _RM ---
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        const uint32_t n = in.GetREG();
        if (n == 2 || n == 3 || n == 6) return false;
        const uint32_t bits = (op & 1) ? osize : 8, bytes = bits / 8;
        const PtrFn fn = bits == 8 ? PtrFn::P8 : bits == 16 ? PtrFn::P16 : PtrFn::P32;
        if (!pointer_ok(fn, o1)) return false;
        clocks = is_addr(o1) ? 4 : 2;
        need_all();
        prepare_ptr(fn, o1, true);
        read_ptr(o1, bytes, V0);
        if (op == 0xC0 || op == 0xC1) {
            a.mov_imm32(V1, in.EvalUimm8() & 31);
        } else if (op == 0xD0 || op == 0xD1) {
            a.movz(false, V1, 1, 0);
        } else {
            read_reg8(1, V1);
            a.and_imm(false, V1, V1, 31);
        }
        shift(n, bits, V0, V1, V2);
        write_ptr(o1, bytes, V2, true);
        return true;
    }

    // --- TEST/NOT/NEG r/m, imm ---
    case 0xF6: case 0xF7: {
        const uint32_t n = in.GetREG();
        const uint32_t bits = op == 0xF6 ? 8 : osize, bytes = bits / 8;
        if (n == 0) {
            if (!(is_reg(o1) || is_addr(o1))) return false;
            clocks = is_addr(o1) ? 2 : 1;
            prep_alu(4);
            read_rm(o1, bytes, V0);
            a.mov_imm32(V1, bits == 8 ? in.EvalUimm8() : bits == 16 ? in.EvalUimm16() : in.EvalUimm32());
            alu(4, bits, V0, V1, V2);
            return true;
        }
        if (n == 2 || n == 3) {
            const PtrFn fn = op == 0xF6 ? PtrFn::P8 : PtrFn::P16OR32;
            if (!pointer_ok(fn, o1)) return false;
            clocks = is_addr(o1) ? (op == 0xF7 && n == 2 ? 2 : 3) : 1;
            prepare_ptr(fn, o1, true);
            read_ptr(o1, bytes, V0);
            if (n == 2) {
                a.mvn(false, V2, V0);
                mask(V2, V2, bits);
            } else {
                a.movz(false, V3, 0, 0);
                alu(5, bits, V3, V0, V2);
            }
            write_ptr(o1, bytes, V2, true);
            return true;
        }
        return false;
    }

    // --- MOV ---
    case 0x88: {  // StoreOperandValueRegOrMem8
        if (!(is_reg(o1) || is_addr(o1))) return false;
        clocks = 1;
        prepare_rm_store(o1, 1);
        read_reg8(in.GetREG(), V0);
        commit_rm(o1, 1, V0);
        return true;
    }
    case 0x89: {  // StoreOperandValueRegOrMem16/32
        if (!(is_reg(o1) || is_addr(o1))) return false;
        clocks = 1;
        prepare_rm_store(o1, obytes);
        read_gpr(in.GetREG(), osize, V0);
        if (is_reg(o1) && o1.operandType == i486DXCommon::OPER_REG32 && osize == 16) {
            // StoreOperandValueRegOrMem16 to a 32-bit register sets all of it.
            a.mov(false, gr(o1.reg - i486DXCommon::REG_EAX), V0);
            return true;
        }
        commit_rm(o1, obytes, V0);
        return true;
    }
    case 0x8A:
        if (!(is_reg(o2) || is_addr(o2))) return false;
        clocks = 1;
        read_rm(o2, 1, V0);
        write_reg8(in.GetREG(), V0);
        return true;
    case 0x8B:
        if (!(is_reg(o2) || is_addr(o2))) return false;
        clocks = 1;
        read_rm(o2, obytes, V0);
        write_gpr(in.GetREG(), osize, V0);
        return true;
    case 0xC6:
        if (!(is_reg(o1) || is_addr(o1))) return false;
        clocks = 1;
        prepare_rm_store(o1, 1);
        a.mov_imm32(V0, in.EvalUimm8());
        commit_rm(o1, 1, V0);
        return true;
    case 0xC7:
        if (!(is_reg(o1) || is_addr(o1))) return false;
        clocks = 1;
        prepare_rm_store(o1, obytes);
        a.mov_imm32(V0, osize == 16 ? in.EvalUimm16() : in.EvalUimm32());
        commit_rm(o1, obytes, V0);
        return true;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        clocks = 1;
        a.mov_imm32(V0, in.EvalUimm8());
        write_reg8(op & 7, V0);
        return true;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        clocks = 1;
        a.mov_imm32(V0, osize == 16 ? in.EvalUimm16() : in.EvalUimm32());
        write_gpr(op & 7, osize, V0);
        return true;
    case 0xA0: case 0xA1: {
        clocks = 1;
        a.mov_imm32(OFF, in.EvalUimm32());
        const uint32_t bytes = op == 0xA0 ? 1 : obytes;
        fetch(bytes, segment_default_ds(), V0);
        write_gpr(0, bytes * 8, V0);
        return true;
    }
    case 0xA2: case 0xA3: {
        clocks = 1;
        a.mov_imm32(OFF, in.EvalUimm32());
        const uint32_t bytes = op == 0xA2 ? 1 : obytes;
        prepare_store(bytes, segment_default_ds());
        read_gpr(0, bytes * 8, V0);
        commit_store(bytes, V0);
        return true;
    }

    // --- LEA, MOVZX, MOVSX ---
    case 0x8D:
        if (!is_addr(o2) || !(o1.operandType == i486DXCommon::OPER_REG32 || o1.operandType == i486DXCommon::OPER_REG16)) return false;
        clocks = 1;
        effective_offset(o2, V0);
        if (o1.operandType == i486DXCommon::OPER_REG32) {
            if (in.addressSize == 16) a.uxth(V0, V0);
            a.mov(false, gr(o1.reg - i486DXCommon::REG_EAX), V0);
        } else {
            a.bfi(false, gr(o1.reg - i486DXCommon::REG_AX), V0, 0, 16);
        }
        return true;
    case 0xFB6: case 0xFBE:
        if (!(is_reg(o2) || is_addr(o2))) return false;
        clocks = 3;
        read_rm(o2, 1, V0);
        if (op == 0xFBE) a.sxtb(V0, V0);
        write_gpr(in.GetREG(), osize, V0);
        return true;
    case 0xFB7: case 0xFBF:
        // EvaluateOperand(op2, 2), written to the whole 32-bit register at either operand size.
        if (!(is_reg(o2) || is_addr(o2))) return false;
        clocks = 3;
        read_rm(o2, 2, V0);
        if (op == 0xFBF) a.sxth(V0, V0);
        else a.uxth(V0, V0);
        a.mov(false, gr(in.GetREG()), V0);
        return true;

    // --- Stack ---
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        clocks = 1;
        a.mov(false, V3, gr(op & 7));
        prepare_push(obytes);
        commit_push(obytes, V3);
        return true;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        clocks = 4;
        pop(obytes, V0);
        write_gpr(op & 7, osize, V0);
        return true;
    case 0x6A: case 0x68:
        clocks = 1;
        prepare_push(obytes);
        a.mov_imm32(V3, op == 0x6A ? simm8(in) : (osize == 16 ? uint32_t(int32_t(int16_t(in.EvalUimm16()))) : in.EvalUimm32()));
        commit_push(obytes, V3);
        return true;

    case 0x9C:  // PUSHF: Push(operandSize, EFLAGS)
        clocks = 4;
        need_all();
        prepare_push(obytes);
        commit_push(obytes, host::FLAGS);
        return true;
    case 0x9D: {  // POPF: the bits POPF can change, from the stack
        need_all();
        pop(obytes, V0);
        a.mov_imm32(T0, 0b1001000111111111010101);
        a.and_(false, V0, V0, T0);
        a.bic(false, V1, host::FLAGS, T0);
        a.orr(false, V1, V1, V0);
        if (osize == 16) a.bfi(false, host::FLAGS, V1, 0, 16);
        else a.mov(false, host::FLAGS, V1);
        // It may set IF, and an interrupt waiting would then be taken after it, as the PIC
        // decides: the run ends there.
        clocks = mode.cpu_mode == i486DXCommon::MODE_REAL ? 9 : 6;
        Exit &e = exit_to(clocks, next_eip);
        e.chain = false;
        e.why = WHY_POPF;
        Assembler::Label go_on;
        a.tbz(host::FLAGS, 9, go_on);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, irq_waiting)));
        a.ldrb(T0, IP0);
        a.cbnz(false, T0, e.label);
        a.bind(go_on);
        return true;
        next = Next::End;
        return true;
    }

    // --- I/O: IN and OUT, through the interpreter's own functions (kairotowns_jit_io) ---
    case 0xE4: case 0xE5: case 0xEC: case 0xED: case 0xE6: case 0xE7: case 0xEE: case 0xEF: {
        // Virtual-8086 mode checks the TSS's I/O permission map; that is the interpreter's.
        if (mode.cpu_mode == i486DXCommon::MODE_VM86) return false;
        const bool out = op >= 0xE6 && op != 0xEC && op != 0xED;
        const bool dx = op == 0xEC || op == 0xED || op == 0xEE || op == 0xEF;
        const uint32_t bytes = (op & 1) ? obytes : 1;
        const bool real = mode.cpu_mode == i486DXCommon::MODE_REAL;
        clocks = !out ? (real ? 14 : 8) : real ? 16 : dx ? 10 : 11;
        need_all();
        // The CPU state as the interpreter has it while the instruction runs.
        const uint32_t regs = reg_offset(0);
        for (uint32_t i = 0; i < 8; i += 2) a.stp_w(gr(i), gr(i + 1), host::STATE, int32_t(regs + 4 * i));
        a.str_w(host::FLAGS, host::STATE, eflags_offset());
        a.mov_imm32(V0, eip);
        a.str_w(V0, host::STATE, eip_offset());
        a.str_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
        a.mov_imm32(V1, (out ? 1u : 0u) | bytes << 8 | uint32_t(in.numBytes) << 16);
        if (dx) a.uxth(V2, gr(2));
        else a.mov_imm32(V2, in.EvalUimm8());
        a.mov(false, V3, gr(0));
        a.ldr_w(OFF, host::CTX, ctx(offsetof(Context, clock_limit)));
        a.sub(false, OFF, OFF, host::LIMIT);
        add_const(OFF, OFF, clocks_before);
        a.mov_imm32(LIN, next_eip);
        a.mov(true, V0, host::CTX);
        a.ldr_x(IP0, host::CTX, ctx(offsetof(Context, io_helper)));
        a.blr(IP0);
        a.ldr_w(host::LIMIT, host::CTX, ctx(offsetof(Context, spill)));
        a.ldr_w(host::TLB_TAG, host::CTX, ctx(offsetof(Context, tlb_tag)));
        for (uint32_t i = 0; i < 8; i += 2) a.ldp_w(gr(i), gr(i + 1), host::STATE, int32_t(regs + 4 * i));
        a.ldr_w(host::FLAGS, host::STATE, eflags_offset());
        Assembler::Label go_on;
        a.cbz(false, V0, go_on);
        a.ldr_w(V2, host::STATE, eip_offset());
        exit_after_dynamic(clocks);
        exits.back()->why = WHY_IO;
        a.bind(go_on);
        has_effects = true;
        return true;
    }

    // --- Jumps ---
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        uint32_t to = eip + simm8(in) + in.numBytes;
        if (osize == 16) to &= 0xFFFF;
        prepare_condition(op & 15);
        Exit &e = exit_to(3, to);
        branch_if(op & 15, e.label);
        clocks = 1;
        return true;
    }
    case 0xF80: case 0xF81: case 0xF82: case 0xF83: case 0xF84: case 0xF85: case 0xF86: case 0xF87:
    case 0xF88: case 0xF89: case 0xF8A: case 0xF8B: case 0xF8C: case 0xF8D: case 0xF8E: case 0xF8F: {
        uint32_t to;
        if (osize == 16) to = (eip + uint32_t(int32_t(int16_t(in.EvalUimm16()))) + in.numBytes) & 0xFFFF;
        else to = eip + in.EvalUimm32() + in.numBytes;
        prepare_condition(op & 15);
        Exit &e = exit_to(3, to);
        branch_if(op & 15, e.label);
        clocks = 1;
        return true;
    }
    case 0xEB: {
        uint32_t to = eip + simm8(in) + in.numBytes;
        if (osize == 16) to &= 0xFFFF;
        exit_after(3, to);
        next = Next::End;
        return true;
    }
    case 0xE9: {
        uint32_t to;
        if (osize == 16) to = (eip + uint32_t(int32_t(int16_t(in.EvalUimm16()))) + in.numBytes) & 0xFFFF;
        else to = eip + in.EvalUimm32() + in.numBytes;
        exit_after(3, to);
        next = Next::End;
        return true;
    }
    case 0xE2: case 0xE3: {  // LOOP, JECXZ
        uint32_t to = eip + simm8(in) + in.numBytes;
        if (osize == 16) to &= 0xFFFF;
        const Reg ecx = gr(1);
        Exit &e = exit_to(3, to);
        if (op == 0xE2) {
            if (in.addressSize == 32) {
                a.sub_imm(false, ecx, ecx, 1);
                a.cbnz(false, ecx, e.label);
            } else {
                a.sub_imm(false, T0, ecx, 1);
                a.bfi(false, ecx, T0, 0, 16);
                a.tst_imm(false, ecx, 0xFFFF);
                a.b_cond(NE, e.label);
            }
        } else {
            if (in.addressSize == 32) {
                a.cbz(false, ecx, e.label);
            } else {
                a.tst_imm(false, ecx, 0xFFFF);
                a.b_cond(EQ, e.label);
            }
        }
        clocks = 1;
        return true;
    }
    case 0xE8: {
        if (cpu.enableCallStack) return false;
        uint32_t to;
        if (osize == 16) to = (eip + uint32_t(int32_t(int16_t(in.EvalUimm16()))) + in.numBytes) & 0xFFFF;
        else to = eip + in.EvalUimm32() + in.numBytes;
        prepare_push(obytes);
        a.mov_imm32(V1, next_eip);
        commit_push(obytes, V1);
        exit_after(3, to);
        next = Next::End;
        return true;
    }
    case 0xC3: {
        if (cpu.enableCallStack) return false;
        pop(obytes, V0);
        if (osize == 16) {
            // SetIP keeps the high word of EIP.
            a.mov_imm32(V2, eip);
            a.bfi(false, V2, V0, 0, 16);
        } else {
            a.mov(false, V2, V0);
        }
        exit_after_dynamic(5);
        next = Next::End;
        return true;
    }

    // --- SETcc ---
    case 0xF90: case 0xF91: case 0xF92: case 0xF93: case 0xF94: case 0xF95: case 0xF96: case 0xF97:
    case 0xF98: case 0xF99: case 0xF9A: case 0xF9B: case 0xF9C: case 0xF9D: case 0xF9E: case 0xF9F:
        if (!(o1.operandType == i486DXCommon::OPER_REG8 || is_addr(o1))) return false;
        prepare_condition(op & 15);
        prepare_rm_store(o1, 1);
        condition_value(op & 15);
        // 4 clocks when set, 3 when not: the extra clock comes off the limit.
        a.sub(false, host::LIMIT, host::LIMIT, V0);
        clocks = 3;
        commit_rm(o1, 1, V0);
        return true;

    // --- Others ---
    case 0x90:
        clocks = 1;
        return true;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        clocks = 3;
        read_gpr(0, osize, V0);
        read_gpr(op & 7, osize, V1);
        write_gpr(0, osize, V1);
        write_gpr(op & 7, osize, V0);
        return true;
    case 0x86: {
        if (!pointer_ok(PtrFn::P8, o1)) return false;
        clocks = is_addr(o1) ? 5 : 3;
        prepare_ptr(PtrFn::P8, o1, true);
        read_ptr(o1, 1, V0);
        read_reg8(in.GetREG(), V1);
        write_ptr(o1, 1, V1, true);
        if (is_reg(o1)) {
            // std::swap through the two pointers: the register is written last.
            write_reg8(in.GetREG(), V0);
        } else {
            write_reg8(in.GetREG(), V0);
        }
        return true;
    }
    case 0x87: {
        if (!pointer_ok(PtrFn::P16OR32, o1)) return false;
        clocks = is_addr(o1) ? 5 : 3;
        prepare_ptr(PtrFn::P16OR32, o1, true);
        read_ptr(o1, obytes, V0);
        read_gpr(in.GetREG(), osize, V1);
        write_gpr(in.GetREG(), osize, V0);
        write_ptr(o1, obytes, V1, true);
        return true;
    }
    case 0x98:
        clocks = 3;
        if (osize == 16) {
            read_reg8(0, V0);
            a.sxtb(V0, V0);
            a.bfi(false, gr(0), V0, 0, 16);
        } else {
            a.sxth(gr(0), gr(0));
        }
        return true;
    case 0x99:
        clocks = 3;
        if (osize == 16) {
            a.sbfx(false, V0, gr(0), 15, 1);
            a.bfi(false, gr(2), V0, 0, 16);
        } else {
            a.asr_imm(false, gr(2), gr(0), 31);
        }
        return true;
    case 0xF8:  // CLC
        clocks = 2;
        need_all();
        a.and_imm(false, host::FLAGS, host::FLAGS, ~F_CF);
        return true;
    case 0xF9:  // STC
        clocks = 2;
        need_all();
        a.orr_imm(false, host::FLAGS, host::FLAGS, F_CF);
        return true;
    case 0xF5:  // CMC
        clocks = 2;
        need_all();
        a.eor_imm(false, host::FLAGS, host::FLAGS, F_CF);
        return true;
    case 0xFC:  // CLD
        clocks = 2;
        a.and_imm(false, host::FLAGS, host::FLAGS, ~F_DF);
        return true;
    case 0xFD:  // STD
        clocks = 2;
        a.orr_imm(false, host::FLAGS, host::FLAGS, F_DF);
        return true;
    case 0xFA:  // CLI
        clocks = 2;
        a.and_imm(false, host::FLAGS, host::FLAGS, ~F_IF);
        return true;
    }
    return false;
}

Translator::Translator(i486DXCommon &cpu, Memory &mem) : cpu_(cpu), mem_(mem) {}

// Its entry: stops before the first instruction when the clock limit has run out (a block entered
// from another), and when the code is no longer the bytes it was translated from; then it tells
// translated code where its code is, for code_check().
void Translator::Impl::prologue(const uint8_t *code, uint32_t bytes) {
    Exit &stop = new_exit(REASON_NEXT, 0, 0, 0, block_eip);
    stop.entry = true;
    stop.why = WHY_ENTRY;
    Exit &changed = new_exit(REASON_MISMATCH, 0, 0, 0, block_eip);
    changed.entry = true;
    changed.why = WHY_ENTRY;
    Exit &segments = new_exit(REASON_SEGMENTS, 0, 0, 0, block_eip);
    segments.entry = true;
    segments.why = WHY_ENTRY;
    a.cmp_imm(false, host::LIMIT, 0);
    a.b_cond(LT, stop.label);
    // Checked since memory last changed: the code is as it was.
    Assembler::Label checked;
    a.ldr_x_literal(IP0, literal(reinterpret_cast<uintptr_t>(stamp)));
    a.ldr_x(IP1, IP0);
    a.ldr_x(V0, host::CTX, ctx(offsetof(Context, code_generation)));
    a.cmp(true, IP1, V0);
    a.b_cond(EQ, checked);
    a.ldr_x_literal(IP0, literal(reinterpret_cast<uintptr_t>(code)));
    auto word = [&](uint32_t at, uint32_t size) {
        uint64_t value = 0;
        std::memcpy(&value, code + at, size);
        return value;
    };
    auto compare = [&](Reg loaded, uint32_t at, uint32_t size) {
        if (size == 8) {
            a.ldr_x_literal(V0, literal(word(at, 8)));
            a.cmp(true, loaded, V0);
        } else {
            a.mov_imm32(V0, uint32_t(word(at, size)));
            a.cmp(false, loaded, V0);
        }
        a.b_cond(NE, changed.label);
    };
    if (bytes >= 8) {
        uint32_t at = 0;
        for (; at + 8 <= bytes; at += 8) {
            a.ldr_x(IP1, IP0, at);
            compare(IP1, at, 8);
        }
        if (at < bytes) {
            // The last 8 bytes, overlapping the ones before.
            a.add_imm(true, IP1, IP0, bytes - 8);
            a.ldr_x(IP1, IP1);
            compare(IP1, bytes - 8, 8);
        }
    } else if (bytes >= 4) {
        a.ldr_w(IP1, IP0);
        compare(IP1, 0, 4);
        a.ldur_w(IP1, IP0, int32_t(bytes - 4));
        compare(IP1, bytes - 4, 4);
    } else if (bytes >= 2) {
        a.ldrh(IP1, IP0);
        compare(IP1, 0, 2);
        a.ldurh(IP1, IP0, int32_t(bytes - 2));
        compare(IP1, bytes - 2, 2);
    } else {
        a.ldrb(IP1, IP0);
        compare(IP1, 0, 1);
    }
    // The segment registers as the block was translated with them.
    for (int s = 0; s < 6; ++s) {
        if (!(check_segments & 1u << s)) continue;
        const auto &seg = st.sreg[s];
        const std::pair<uint32_t, uint32_t> fields[] = {{seg_base(s), seg.baseLinearAddr}, {seg_max(s), seg.maxLimit},
                                                        {seg_min(s), seg.minLimit}, {seg_min_adjust(s), seg.minLimitAdjust}};
        for (const auto &field : fields) {
            a.ldr_w(IP1, host::STATE, field.first);
            a.mov_imm32(V0, field.second);
            a.cmp(false, IP1, V0);
            a.b_cond(NE, segments.label);
        }
    }
    a.ldr_x_literal(IP0, literal(reinterpret_cast<uintptr_t>(stamp)));
    a.ldr_x(V0, host::CTX, ctx(offsetof(Context, code_generation)));
    a.str_x(V0, IP0);
    a.bind(checked);
    a.ldr_x_literal(IP0, literal(reinterpret_cast<uintptr_t>(code)));
    a.str_x(IP0, host::CTX, ctx(offsetof(Context, code_start)));
    a.mov_imm32(IP1, bytes);
    a.str_x(IP1, host::CTX, ctx(offsetof(Context, code_bytes)));
}

Translator::Result Translator::Impl::translate_block(const uint8_t *code, uint32_t avail, uint32_t start, uint32_t check_bytes) {
    auto &cpu_ = static_cast<Cpu &>(cpu);
    Result result;
    uint32_t pos = 0;
    eip = block_eip = start;
    window_offset = 4096 - avail;
    if (check_bytes) prologue(code, check_bytes);
    for (index = 0;; ++index) {
        // A block ends at the end of its code window: past it the interpreter moves its window.
        const bool room = pos < avail;
        if (!room || index >= max_instructions() || clocks_before > kMaxClocks) {
            if (index == 0) return {};
            Exit &e = new_exit(REASON_NEXT, index, clocks_before, last_eip, eip);
            e.chain = room && max_instructions() > 1;
            a.b(e.label);
            break;
        }
        i486DXCommon::InstructionAndOperand decoded;
        std::memset(&decoded, 0, sizeof decoded);
        auto &in = decoded.inst;
        in.Clear();
        in.operandSize = mode.operand_size;
        in.addressSize = mode.address_size;
        in.codeAddressSize = mode.address_size;
        // Decoded from a copy of the bytes left in the window, as many as an instruction can
        // have: near the window's end the interpreter decodes byte by byte from the window, and
        // reads past it only for an instruction that crosses it, which is the interpreter's.
        uint8_t bytes[i486DXCommon::MAX_INSTRUCTION_LENGTH + 1] = {};
        std::memcpy(bytes, code + pos, std::min<uint32_t>(avail - pos, i486DXCommon::MAX_INSTRUCTION_LENGTH));
        MemoryAccess::ConstPointer ptr;
        ptr.ptr = bytes;
        ptr.length = i486DXCommon::MAX_INSTRUCTION_LENGTH;
        in.opCode = Burst::FetchInstructionByte(cpu_, ptr, in.codeAddressSize, cpu_.state.CS(), eip, mem);
        Cpu::template KairoFetchOperandLoop<Cpu, Memory, Burst>(cpu_, decoded, ptr, cpu_.state.CS(), eip + 1, mem,
                                                                mode.operand_size, mode.address_size);

        inst = &in;
        op1 = &decoded.op1;
        op2 = &decoded.op2;
        bail_exit = nullptr;
        if (in.numBytes > i486DXCommon::MAX_INSTRUCTION_LENGTH || pos + in.numBytes > avail) {
            if (index == 0) return {};
            Exit &e = new_exit(REASON_INTERPRET, index, clocks_before, last_eip, eip);
            a.b(e.label);
            break;
        }
        // A 16-bit EIP that would pass 0xFFFF goes to the interpreter.
        const uint32_t next_eip = eip + in.numBytes;
        if (mode.operand_size == 16 && next_eip > 0xFFFF) {
            if (index == 0) return {};
            Exit &e = new_exit(REASON_INTERPRET, index, clocks_before, last_eip, eip);
            a.b(e.label);
            break;
        }
        if (index > 0) {
            // The interpreter's loop runs an instruction only while guest time has not passed
            // the next fast device polling.
            Exit &stop = new_exit(REASON_NEXT, index, clocks_before, last_eip, eip);
            stop.why = WHY_LIMIT;
            a.cmp_imm(false, host::LIMIT, clocks_before);
            a.b_cond(LT, stop.label);
        }
        uint32_t clocks = 0;
        Next next = Next::Continue;
        const size_t before = a.size();
        next_eip_now = next_eip;
        if (!translate_one(clocks, next, next_eip)) {
            if (a.size() != before) throw std::logic_error("an instruction emitted code before it was refused");
            if (!interpretable(in)) {
                if (index == 0) return {};
                Exit &e = new_exit(REASON_INTERPRET, index, clocks_before, last_eip, eip);
                a.b(e.label);
                break;
            }
            Exit &e = new_exit(REASON_INTERPRET, index, clocks_before, last_eip, eip);
            e.interpret = true;
            e.next_eip = next_eip;
            a.b(e.label);
            pos += in.numBytes;
            ++index;
            break;
        }
        pos += in.numBytes;
        if (next == Next::End) {
            ++index;
            break;
        }
        clocks_before += clocks;
        last_eip = eip;
        eip = next_eip;
    }
    for (size_t i = 0; i < exits.size(); ++i) emit_exit(*exits[i]);
    for (auto &emit : cold) emit();
    // The literals, 8-byte aligned in a block that starts 16-byte aligned.
    if (a.size() % 8) a.emit(0xD503201Fu);
    for (auto &l : literals) {
        a.bind(l->label);
        a.emit64(l->value);
    }
    result.instructions = index;
    result.bytes = pos;
    return result;
}

Translator::Result Translator::translate(Assembler &a, const uint8_t *code, uint32_t avail, uint32_t eip, const Mode &mode,
                                         uint64_t *stamp, bool specialize) {
    // Twice: the code a block checks at its entry is the code its translation covers, known once
    // it is translated. Translating is deterministic, so the second covers the same code.
    Assembler first;
    Impl pass(first, cpu_, mem_, mode);
    pass.specialize = specialize;
    const Result covered = pass.translate_block(code, avail, eip, 0);
    if (covered.instructions == 0) return {};
    Impl second(a, cpu_, mem_, mode);
    second.specialize = specialize;
    second.stamp = stamp;
    second.check_segments = pass.segments_used;
    const Result result = second.translate_block(code, avail, eip, covered.bytes);
    if (result.instructions != covered.instructions || result.bytes != covered.bytes ||
        second.segments_used != pass.segments_used)
        throw std::logic_error("translating the same code twice covered different code");
    return result;
}

void Translator::emit_enter(Assembler &a, const i486DXCommon &cpu) {
    const auto &st = cpu.state;
    const uint32_t regs = state_offset(st, &st.NULL_and_reg32[1]);
    a.stp_x_pre(29, 30, SP, -96);
    a.stp_x(19, 20, SP, 16);
    a.stp_x(21, 22, SP, 32);
    a.stp_x(23, 24, SP, 48);
    a.stp_x(25, 26, SP, 64);
    a.stp_x(27, 28, SP, 80);
    a.mov(true, host::CTX, 0);
    a.ldr_x(host::STATE, host::CTX, offsetof(Context, state));
    for (uint32_t i = 0; i < 8; i += 2) a.ldp_w(host::GUEST0 + i, host::GUEST0 + i + 1, host::STATE, int32_t(regs + 4 * i));
    a.ldr_w(host::FLAGS, host::STATE, state_offset(st, &st.EFLAGS));
    a.ldr_w(host::LIMIT, host::CTX, offsetof(Context, clock_limit));
    a.ldr_w(host::TLB_TAG, host::CTX, offsetof(Context, tlb_tag));
    a.br(1);
}

void Translator::emit_dispatch(Assembler &a, const i486DXCommon &cpu) {
    const auto &st = cpu.state;
    const int cs = i486DXCommon::REG_CS - i486DXCommon::REG_SEGMENT_REG_BASE;
    Assembler::Label miss, multiplier;
    // The code's host address, where the code window holds it, as current() finds it.
    a.ldr_w(V0, host::STATE, state_offset(st, &st.sreg[cs].baseLinearAddr));
    a.add(false, V0, V0, V2);
    a.ldr_x(IP0, host::CTX, offsetof(Context, code_window));
    a.ldr_w(V1, IP0, uint32_t(MemoryAccess::ConstMemoryWindow::KairoLinearBaseAddrOffset()));
    a.and_imm(false, IP1, V0, 0xFFFFF000u);
    a.cmp(false, IP1, V1);
    a.b_cond(NE, miss);
    a.ldr_x(IP1, IP0, offsetof(MemoryAccess::ConstMemoryWindow, ptr));
    a.cbz(true, IP1, miss);
    a.and_imm(false, V1, V0, 0xFFF);
    a.add_ext(true, OFF, IP1, V1, UXTW);
    // Jit::Impl::dispatch_slot()
    a.eor(true, LIN, OFF, V2, LSL, 20);
    a.ldr_x_literal(ADDR, multiplier);
    a.mul(true, LIN, LIN, ADDR);
    a.lsr_imm(true, LIN, LIN, 64 - kDispatchBits);
    a.ldr_x(IP0, host::CTX, offsetof(Context, dispatch_table));
    a.add(true, IP0, IP0, LIN, LSL, 5);
    a.ldr_x(IP1, IP0, offsetof(DispatchEntry, code));
    a.cmp(true, IP1, OFF);
    a.b_cond(NE, miss);
    a.ldp_w(SEFX, V4, IP0, int32_t(offsetof(DispatchEntry, eip)));
    a.cmp(false, SEFX, V2);
    a.b_cond(NE, miss);
    a.cmp(false, V4, V3);
    a.b_cond(NE, miss);
    a.ldr_x(IP0, IP0, offsetof(DispatchEntry, entry));
    a.br(IP0);
    // No block: the run ends there, as a chain's request does.
    a.bind(miss);
    a.ldr_w(V0, host::CTX, offsetof(Context, clock_limit));
    a.sub(false, V0, V0, host::LIMIT);
    a.str_w(V0, host::CTX, offsetof(Context, clocks));
    a.ldr_w(V1, host::CTX, offsetof(Context, chained_instructions));
    a.str_w(V1, host::CTX, offsetof(Context, instructions));
    a.ldr_w(V1, host::CTX, offsetof(Context, chained_last_eip));
    a.str_w(V1, host::CTX, offsetof(Context, last_eip));
    a.movz(false, V1, REASON_NEXT, 0);
    a.str_w(V1, host::CTX, offsetof(Context, reason));
    a.movz(false, V1, WHY_DISPATCH, 0);
    a.str_w(V1, host::CTX, offsetof(Context, why));
    a.str_w(V2, host::STATE, state_offset(st, &st.EIP));
    a.str_w(host::FLAGS, host::STATE, state_offset(st, &st.EFLAGS));
    a.ldr_x(IP0, host::CTX, offsetof(Context, leave));
    a.br(IP0);
    if (a.size() % 8) a.emit(0xD503201Fu);
    a.bind(multiplier);
    a.emit64(0x9E3779B97F4A7C15ull);
}

void Translator::emit_leave(Assembler &a, const i486DXCommon &cpu) {
    const auto &st = cpu.state;
    const uint32_t regs = state_offset(st, &st.NULL_and_reg32[1]);
    for (uint32_t i = 0; i < 8; i += 2) a.stp_w(host::GUEST0 + i, host::GUEST0 + i + 1, host::STATE, int32_t(regs + 4 * i));
    a.ldp_x(19, 20, SP, 16);
    a.ldp_x(21, 22, SP, 32);
    a.ldp_x(23, 24, SP, 48);
    a.ldp_x(25, 26, SP, 64);
    a.ldp_x(27, 28, SP, 80);
    a.ldp_x_post(29, 30, SP, 96);
    a.ret();
}

} // namespace kairotowns::jit
