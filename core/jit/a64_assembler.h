// A small AArch64 assembler for the JIT (KairoTowns#19): encodes the instructions the translator
// emits into a byte buffer, with labels for branches inside a block.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace kairotowns::jit {

// A register number, 0-30, or 31 for the zero register or the stack pointer as the instruction
// reads it.
using Reg = uint32_t;
constexpr Reg ZR = 31, SP = 31;

enum Cond : uint32_t { EQ = 0, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL };
inline Cond invert(Cond c) { return Cond(uint32_t(c) ^ 1); }

enum Shift : uint32_t { LSL = 0, LSR = 1, ASR = 2, ROR = 3 };
// Register extensions for the extended-register forms and register-offset loads.
enum Extend : uint32_t { UXTB = 0, UXTH = 1, UXTW = 2, UXTX = 3, SXTB = 4, SXTH = 5, SXTW = 6, SXTX = 7 };

class Assembler {
public:
    class Label {
        friend class Assembler;
        int64_t at_ = -1;                 // offset in the buffer once bound
        std::vector<size_t> branches_;     // offsets of branches waiting for it
    };

    size_t size() const { return code_.size() * 4; }
    const uint32_t *code() const { return code_.data(); }
    void clear() {
        code_.clear();
        kinds_.clear();
    }

    void bind(Label &label) {
        if (label.at_ >= 0) throw std::logic_error("label bound twice");
        label.at_ = int64_t(code_.size());
        for (size_t branch : label.branches_) patch(branch, label.at_);
        label.branches_.clear();
    }
    bool bound(const Label &label) const { return label.at_ >= 0; }

    // --- Moves --------------------------------------------------------------------------------

    void movz(bool x, Reg rd, uint32_t imm16, uint32_t shift) { emit((x ? 0xD2800000u : 0x52800000u) | (shift / 16) << 21 | (imm16 & 0xFFFF) << 5 | rd); }
    void movk(bool x, Reg rd, uint32_t imm16, uint32_t shift) { emit((x ? 0xF2800000u : 0x72800000u) | (shift / 16) << 21 | (imm16 & 0xFFFF) << 5 | rd); }
    void movn(bool x, Reg rd, uint32_t imm16, uint32_t shift) { emit((x ? 0x92800000u : 0x12800000u) | (shift / 16) << 21 | (imm16 & 0xFFFF) << 5 | rd); }

    void mov_imm32(Reg rd, uint32_t value) {
        if ((value & 0xFFFF0000u) == 0) return movz(false, rd, value, 0);
        if ((value & 0xFFFFu) == 0) return movz(false, rd, value >> 16, 16);
        if ((~value & 0xFFFF0000u) == 0) return movn(false, rd, ~value & 0xFFFF, 0);
        if ((~value & 0xFFFFu) == 0) return movn(false, rd, (~value >> 16) & 0xFFFF, 16);
        uint32_t n, immr, imms;
        if (logical_immediate(value, 32, n, immr, imms)) return orr_imm_enc(false, rd, ZR, n, immr, imms);
        movz(false, rd, value & 0xFFFF, 0);
        movk(false, rd, value >> 16, 16);
    }
    void mov_imm64(Reg rd, uint64_t value) {
        if (value >> 32 == 0) return mov_imm32(rd, uint32_t(value));
        bool first = true;
        for (uint32_t shift = 0; shift < 64; shift += 16) {
            const uint32_t part = uint32_t(value >> shift) & 0xFFFF;
            if (part == 0) continue;
            if (first) movz(true, rd, part, shift);
            else movk(true, rd, part, shift);
            first = false;
        }
    }
    // mov rd, rm (orr rd, zr, rm)
    void mov(bool x, Reg rd, Reg rm) { logical_reg(x, 0x2A000000u, rd, ZR, rm, LSL, 0); }
    // mov to or from sp (add rd, rn, #0)
    void mov_sp(Reg rd, Reg rn) { add_imm(true, rd, rn, 0); }

    // --- Arithmetic ---------------------------------------------------------------------------

    void add_imm(bool x, Reg rd, Reg rn, uint32_t imm12, bool shift12 = false) { arith_imm(x, 0x11000000u, rd, rn, imm12, shift12); }
    void adds_imm(bool x, Reg rd, Reg rn, uint32_t imm12, bool shift12 = false) { arith_imm(x, 0x31000000u, rd, rn, imm12, shift12); }
    void sub_imm(bool x, Reg rd, Reg rn, uint32_t imm12, bool shift12 = false) { arith_imm(x, 0x51000000u, rd, rn, imm12, shift12); }
    void subs_imm(bool x, Reg rd, Reg rn, uint32_t imm12, bool shift12 = false) { arith_imm(x, 0x71000000u, rd, rn, imm12, shift12); }
    void cmp_imm(bool x, Reg rn, uint32_t imm12) { subs_imm(x, ZR, rn, imm12); }

    void add(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { arith_reg(x, 0x0B000000u, rd, rn, rm, s, amount); }
    void adds(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { arith_reg(x, 0x2B000000u, rd, rn, rm, s, amount); }
    void sub(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { arith_reg(x, 0x4B000000u, rd, rn, rm, s, amount); }
    void subs(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { arith_reg(x, 0x6B000000u, rd, rn, rm, s, amount); }
    void cmp(bool x, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { subs(x, ZR, rn, rm, s, amount); }
    void cmn(bool x, Reg rn, Reg rm) { adds(x, ZR, rn, rm); }
    void neg(bool x, Reg rd, Reg rm) { sub(x, rd, ZR, rm); }
    // add xd, xn, wm, uxtw #amount
    void add_ext(bool x, Reg rd, Reg rn, Reg rm, Extend e, uint32_t amount = 0) {
        emit((x ? 0x8B200000u : 0x0B200000u) | rm << 16 | uint32_t(e) << 13 | amount << 10 | rn << 5 | rd);
    }
    void sub_ext(bool x, Reg rd, Reg rn, Reg rm, Extend e, uint32_t amount = 0) {
        emit((x ? 0xCB200000u : 0x4B200000u) | rm << 16 | uint32_t(e) << 13 | amount << 10 | rn << 5 | rd);
    }
    void adc(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9A000000u : 0x1A000000u) | rm << 16 | rn << 5 | rd); }
    void sbc(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0xDA000000u : 0x5A000000u) | rm << 16 | rn << 5 | rd); }

    void mul(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9B000000u : 0x1B000000u) | rm << 16 | ZR << 10 | rn << 5 | rd); }
    void smull(Reg xd, Reg wn, Reg wm) { emit(0x9B200000u | wm << 16 | ZR << 10 | wn << 5 | xd); }
    void umull(Reg xd, Reg wn, Reg wm) { emit(0x9BA00000u | wm << 16 | ZR << 10 | wn << 5 | xd); }
    void udiv(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9AC00800u : 0x1AC00800u) | rm << 16 | rn << 5 | rd); }
    void sdiv(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9AC00C00u : 0x1AC00C00u) | rm << 16 | rn << 5 | rd); }

    // --- Logic --------------------------------------------------------------------------------

    void and_(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { logical_reg(x, 0x0A000000u, rd, rn, rm, s, amount); }
    void ands(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { logical_reg(x, 0x6A000000u, rd, rn, rm, s, amount); }
    void orr(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { logical_reg(x, 0x2A000000u, rd, rn, rm, s, amount); }
    void eor(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { logical_reg(x, 0x4A000000u, rd, rn, rm, s, amount); }
    void bic(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { logical_reg(x, 0x0A200000u, rd, rn, rm, s, amount); }
    void orn(bool x, Reg rd, Reg rn, Reg rm, Shift s = LSL, uint32_t amount = 0) { logical_reg(x, 0x2A200000u, rd, rn, rm, s, amount); }
    void tst(bool x, Reg rn, Reg rm) { ands(x, ZR, rn, rm); }
    void mvn(bool x, Reg rd, Reg rm) { orn(x, rd, ZR, rm); }

    // Logical immediates. The value must have an encoding; try_and_imm says whether it has one and
    // emits nothing when it does not (the caller then uses a register).
    void and_imm(bool x, Reg rd, Reg rn, uint64_t imm) { must(logical_imm(x, 0x12000000u, rd, rn, imm)); }
    void ands_imm(bool x, Reg rd, Reg rn, uint64_t imm) { must(logical_imm(x, 0x72000000u, rd, rn, imm)); }
    void orr_imm(bool x, Reg rd, Reg rn, uint64_t imm) { must(logical_imm(x, 0x32000000u, rd, rn, imm)); }
    void eor_imm(bool x, Reg rd, Reg rn, uint64_t imm) { must(logical_imm(x, 0x52000000u, rd, rn, imm)); }
    void tst_imm(bool x, Reg rn, uint64_t imm) { ands_imm(x, ZR, rn, imm); }
    bool try_and_imm(bool x, Reg rd, Reg rn, uint64_t imm) { return logical_imm(x, 0x12000000u, rd, rn, imm); }

    // --- Bitfields and shifts -----------------------------------------------------------------

    void ubfm(bool x, Reg rd, Reg rn, uint32_t immr, uint32_t imms) { emit((x ? 0xD3400000u : 0x53000000u) | immr << 16 | imms << 10 | rn << 5 | rd); }
    void sbfm(bool x, Reg rd, Reg rn, uint32_t immr, uint32_t imms) { emit((x ? 0x93400000u : 0x13000000u) | immr << 16 | imms << 10 | rn << 5 | rd); }
    void bfm(bool x, Reg rd, Reg rn, uint32_t immr, uint32_t imms) { emit((x ? 0xB3400000u : 0x33000000u) | immr << 16 | imms << 10 | rn << 5 | rd); }
    void lsl_imm(bool x, Reg rd, Reg rn, uint32_t s) { const uint32_t w = x ? 64 : 32; ubfm(x, rd, rn, (w - s) % w, w - 1 - s); }
    void lsr_imm(bool x, Reg rd, Reg rn, uint32_t s) { ubfm(x, rd, rn, s, x ? 63 : 31); }
    void asr_imm(bool x, Reg rd, Reg rn, uint32_t s) { sbfm(x, rd, rn, s, x ? 63 : 31); }
    void ror_imm(bool x, Reg rd, Reg rn, uint32_t s) { emit((x ? 0x93C00000u : 0x13800000u) | rn << 16 | s << 10 | rn << 5 | rd); }
    void ubfx(bool x, Reg rd, Reg rn, uint32_t lsb, uint32_t width) { ubfm(x, rd, rn, lsb, lsb + width - 1); }
    void sbfx(bool x, Reg rd, Reg rn, uint32_t lsb, uint32_t width) { sbfm(x, rd, rn, lsb, lsb + width - 1); }
    void bfi(bool x, Reg rd, Reg rn, uint32_t lsb, uint32_t width) { const uint32_t w = x ? 64 : 32; bfm(x, rd, rn, (w - lsb) % w, width - 1); }
    void bfxil(bool x, Reg rd, Reg rn, uint32_t lsb, uint32_t width) { bfm(x, rd, rn, lsb, lsb + width - 1); }
    void uxtb(Reg rd, Reg rn) { ubfm(false, rd, rn, 0, 7); }
    void uxth(Reg rd, Reg rn) { ubfm(false, rd, rn, 0, 15); }
    void sxtb(Reg rd, Reg rn) { sbfm(false, rd, rn, 0, 7); }
    void sxth(Reg rd, Reg rn) { sbfm(false, rd, rn, 0, 15); }
    void lslv(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9AC02000u : 0x1AC02000u) | rm << 16 | rn << 5 | rd); }
    void lsrv(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9AC02400u : 0x1AC02400u) | rm << 16 | rn << 5 | rd); }
    void asrv(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9AC02800u : 0x1AC02800u) | rm << 16 | rn << 5 | rd); }
    void rorv(bool x, Reg rd, Reg rn, Reg rm) { emit((x ? 0x9AC02C00u : 0x1AC02C00u) | rm << 16 | rn << 5 | rd); }
    void rev(Reg wd, Reg wn) { emit(0x5AC00800u | wn << 5 | wd); }
    void rev16(Reg wd, Reg wn) { emit(0x5AC00400u | wn << 5 | wd); }
    void clz(bool x, Reg rd, Reg rn) { emit((x ? 0xDAC01000u : 0x5AC01000u) | rn << 5 | rd); }
    void rbit(bool x, Reg rd, Reg rn) { emit((x ? 0xDAC00000u : 0x5AC00000u) | rn << 5 | rd); }

    // --- Conditional select -------------------------------------------------------------------

    void csel(bool x, Reg rd, Reg rn, Reg rm, Cond c) { emit((x ? 0x9A800000u : 0x1A800000u) | rm << 16 | c << 12 | rn << 5 | rd); }
    void csinc(bool x, Reg rd, Reg rn, Reg rm, Cond c) { emit((x ? 0x9A800400u : 0x1A800400u) | rm << 16 | c << 12 | rn << 5 | rd); }
    void cset(bool x, Reg rd, Cond c) { csinc(x, rd, ZR, ZR, invert(c)); }
    // ccmp rn, rm, #nzcv, cond
    void ccmp(bool x, Reg rn, Reg rm, uint32_t nzcv, Cond c) { emit((x ? 0xFA400000u : 0x7A400000u) | rm << 16 | c << 12 | rn << 5 | nzcv); }
    void ccmp_imm(bool x, Reg rn, uint32_t imm5, uint32_t nzcv, Cond c) { emit((x ? 0xFA400800u : 0x7A400800u) | imm5 << 16 | c << 12 | rn << 5 | nzcv); }

    // --- Loads and stores ---------------------------------------------------------------------
    // Unsigned scaled offsets; the offset must be a multiple of the access size.

    void ldrb(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0x39400000u, 1, wt, xn, off); }
    void ldrsb(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0x39C00000u, 1, wt, xn, off); }
    void ldrh(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0x79400000u, 2, wt, xn, off); }
    void ldrsh(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0x79C00000u, 2, wt, xn, off); }
    void ldr_w(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0xB9400000u, 4, wt, xn, off); }
    void ldr_x(Reg xt, Reg xn, uint32_t off = 0) { ls_uimm(0xF9400000u, 8, xt, xn, off); }
    void strb(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0x39000000u, 1, wt, xn, off); }
    void strh(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0x79000000u, 2, wt, xn, off); }
    void str_w(Reg wt, Reg xn, uint32_t off = 0) { ls_uimm(0xB9000000u, 4, wt, xn, off); }
    void str_x(Reg xt, Reg xn, uint32_t off = 0) { ls_uimm(0xF9000000u, 8, xt, xn, off); }
    // Register offsets: [xn, wm, uxtw] or [xn, xm, lsl #scale] (scale 0 or the access size).
    void ldrb_r(Reg wt, Reg xn, Reg rm, Extend e = UXTW) { ls_reg(0x38600800u, wt, xn, rm, e, false); }
    void ldrh_r(Reg wt, Reg xn, Reg rm, Extend e = UXTW) { ls_reg(0x78600800u, wt, xn, rm, e, false); }
    void ldr_w_r(Reg wt, Reg xn, Reg rm, Extend e = UXTW) { ls_reg(0xB8600800u, wt, xn, rm, e, false); }
    void ldr_x_r(Reg xt, Reg xn, Reg rm, Extend e = UXTX, bool scaled = false) { ls_reg(0xF8600800u, xt, xn, rm, e, scaled); }
    void strb_r(Reg wt, Reg xn, Reg rm, Extend e = UXTW) { ls_reg(0x38200800u, wt, xn, rm, e, false); }
    void strh_r(Reg wt, Reg xn, Reg rm, Extend e = UXTW) { ls_reg(0x78200800u, wt, xn, rm, e, false); }
    void str_w_r(Reg wt, Reg xn, Reg rm, Extend e = UXTW) { ls_reg(0xB8200800u, wt, xn, rm, e, false); }
    // Pairs; offsets are multiples of the register size, signed seven bits of them.
    void ldp_w(Reg t1, Reg t2, Reg xn, int32_t off) { pair(0x29400000u, 4, t1, t2, xn, off); }
    void stp_w(Reg t1, Reg t2, Reg xn, int32_t off) { pair(0x29000000u, 4, t1, t2, xn, off); }
    void ldp_x(Reg t1, Reg t2, Reg xn, int32_t off) { pair(0xA9400000u, 8, t1, t2, xn, off); }
    void stp_x(Reg t1, Reg t2, Reg xn, int32_t off) { pair(0xA9000000u, 8, t1, t2, xn, off); }
    void stp_x_pre(Reg t1, Reg t2, Reg xn, int32_t off) { pair(0xA9800000u, 8, t1, t2, xn, off); }
    void ldp_x_post(Reg t1, Reg t2, Reg xn, int32_t off) { pair(0xA8C00000u, 8, t1, t2, xn, off); }

    // --- Branches -----------------------------------------------------------------------------

    void b(Label &l) { branch(0x14000000u, l, Kind::B26); }
    void bl(Label &l) { branch(0x94000000u, l, Kind::B26); }
    void b_cond(Cond c, Label &l) { branch(0x54000000u | c, l, Kind::B19); }
    void cbz(bool x, Reg rt, Label &l) { branch((x ? 0xB4000000u : 0x34000000u) | rt, l, Kind::B19); }
    void cbnz(bool x, Reg rt, Label &l) { branch((x ? 0xB5000000u : 0x35000000u) | rt, l, Kind::B19); }
    void tbz(Reg rt, uint32_t bit, Label &l) { branch(0x36000000u | (bit >> 5) << 31 | (bit & 31) << 19 | rt, l, Kind::B14); }
    void tbnz(Reg rt, uint32_t bit, Label &l) { branch(0x37000000u | (bit >> 5) << 31 | (bit & 31) << 19 | rt, l, Kind::B14); }
    void br(Reg xn) { emit(0xD61F0000u | xn << 5); }
    void blr(Reg xn) { emit(0xD63F0000u | xn << 5); }
    void ret() { emit(0xD65F03C0u); }
    // A branch to an absolute place in code already written, by its byte distance from here.
    void b_to(int64_t byte_offset) {
        if (byte_offset % 4 || byte_offset < -(int64_t(1) << 27) || byte_offset >= (int64_t(1) << 27))
            throw std::logic_error("branch out of reach");
        emit(0x14000000u | (uint32_t(byte_offset / 4) & 0x3FFFFFF));
    }
    void brk(uint32_t imm16) { emit(0xD4200000u | (imm16 & 0xFFFF) << 5); }
    // xd = the address of a label.
    void adr(Reg xd, Label &l) { branch(0x10000000u | xd, l, Kind::ADR); }
    // Loads from a label, a literal emitted with emit64.
    void ldr_x_literal(Reg xt, Label &l) { branch(0x58000000u | xt, l, Kind::B19); }
    void emit64(uint64_t value) {
        emit(uint32_t(value));
        emit(uint32_t(value >> 32));
    }
    // Unscaled 9-bit signed offsets.
    void ldur_x(Reg xt, Reg xn, int32_t off) { emit(0xF8400000u | (uint32_t(off) & 0x1FF) << 12 | xn << 5 | xt); }
    void ldur_w(Reg wt, Reg xn, int32_t off) { emit(0xB8400000u | (uint32_t(off) & 0x1FF) << 12 | xn << 5 | wt); }
    void ldurh(Reg wt, Reg xn, int32_t off) { emit(0x78400000u | (uint32_t(off) & 0x1FF) << 12 | xn << 5 | wt); }

    // Whether imm has a logical-immediate encoding at this width.
    static bool logical_immediate(uint64_t imm, uint32_t width, uint32_t &n, uint32_t &immr, uint32_t &imms) {
        if (width == 32) imm &= 0xFFFFFFFFull;
        const uint64_t all = width == 64 ? ~0ull : (1ull << width) - 1;
        if (imm == 0 || imm == all) return false;
        uint32_t size = width;
        while (size > 2) {
            const uint32_t half = size / 2;
            const uint64_t mask = (1ull << half) - 1;
            if ((imm & mask) != ((imm >> half) & mask)) break;
            size = half;
        }
        const uint64_t mask = size == 64 ? ~0ull : (1ull << size) - 1;
        uint64_t pattern = imm & mask;
        // Rotate the pattern right until its ones are at the bottom, counting the rotation.
        uint32_t rotation = 0;
        auto rotr = [&](uint64_t v) { return size == 64 ? (v >> 1) | (v << 63) : ((v >> 1) | (v << (size - 1))) & mask; };
        // First bring a zero to the top, then rotate until the bottom bit is one and the top zero.
        while (!((pattern & 1) && !(pattern >> (size - 1) & 1))) {
            pattern = rotr(pattern);
            ++rotation;
            if (rotation > size) return false;
        }
        // pattern must now be 0...01...1
        if ((pattern & (pattern + 1)) != 0) return false;
        uint32_t ones = 0;
        while (pattern >> ones & 1) ++ones;
        immr = (size - rotation) % size;
        imms = ((~(size - 1) << 1) | (ones - 1)) & 0x3F;
        n = size == 64 ? 1 : 0;
        return true;
    }

    void emit(uint32_t word) { code_.push_back(word); }

private:
    enum class Kind { B26, B19, B14, ADR };

    std::vector<uint32_t> code_;
    std::vector<Kind> kinds_;

    static void must(bool encoded) {
        if (!encoded) throw std::logic_error("logical immediate has no encoding");
    }

    void arith_imm(bool x, uint32_t op, Reg rd, Reg rn, uint32_t imm12, bool shift12) {
        if (imm12 > 0xFFF) throw std::logic_error("arithmetic immediate out of range");
        emit((x ? 0x80000000u : 0) | op | (shift12 ? 1u : 0u) << 22 | imm12 << 10 | rn << 5 | rd);
    }
    void arith_reg(bool x, uint32_t op, Reg rd, Reg rn, Reg rm, Shift s, uint32_t amount) {
        emit((x ? 0x80000000u : 0) | op | uint32_t(s) << 22 | rm << 16 | amount << 10 | rn << 5 | rd);
    }
    void logical_reg(bool x, uint32_t op, Reg rd, Reg rn, Reg rm, Shift s, uint32_t amount) {
        emit((x ? 0x80000000u : 0) | op | uint32_t(s) << 22 | rm << 16 | amount << 10 | rn << 5 | rd);
    }
    bool logical_imm(bool x, uint32_t op, Reg rd, Reg rn, uint64_t imm) {
        uint32_t n, immr, imms;
        if (!logical_immediate(imm, x ? 64 : 32, n, immr, imms)) return false;
        emit((x ? 0x80000000u : 0) | op | n << 22 | immr << 16 | imms << 10 | rn << 5 | rd);
        return true;
    }
    void orr_imm_enc(bool x, Reg rd, Reg rn, uint32_t n, uint32_t immr, uint32_t imms) {
        emit((x ? 0x80000000u : 0) | 0x32000000u | n << 22 | immr << 16 | imms << 10 | rn << 5 | rd);
    }
    void ls_uimm(uint32_t op, uint32_t size, Reg rt, Reg rn, uint32_t off) {
        if (off % size || off / size > 0xFFF) throw std::logic_error("load/store offset out of range");
        emit(op | (off / size) << 10 | rn << 5 | rt);
    }
    void ls_reg(uint32_t op, Reg rt, Reg rn, Reg rm, Extend e, bool scaled) {
        emit(op | rm << 16 | uint32_t(e) << 13 | (scaled ? 1u : 0u) << 12 | rn << 5 | rt);
    }
    void pair(uint32_t op, uint32_t size, Reg t1, Reg t2, Reg rn, int32_t off) {
        if (off % int32_t(size) || off / int32_t(size) < -64 || off / int32_t(size) > 63)
            throw std::logic_error("pair offset out of range");
        emit(op | (uint32_t(off / int32_t(size)) & 0x7F) << 15 | t2 << 10 | rn << 5 | t1);
    }
    void branch(uint32_t op, Label &l, Kind kind) {
        const size_t at = code_.size();
        emit(op);
        kinds_.resize(code_.size(), Kind::B26);
        kinds_[at] = kind;
        if (l.at_ >= 0) patch(at, l.at_);
        else l.branches_.push_back(at);
    }
    void patch(size_t at, int64_t target) {
        const int64_t delta = target - int64_t(at);
        uint32_t &word = code_[at];
        switch (kinds_[at]) {
        case Kind::B26:
            if (delta < -(1 << 25) || delta >= (1 << 25)) throw std::logic_error("branch out of reach");
            word |= uint32_t(delta) & 0x3FFFFFF;
            break;
        case Kind::B19:
            if (delta < -(1 << 18) || delta >= (1 << 18)) throw std::logic_error("branch out of reach");
            word |= (uint32_t(delta) & 0x7FFFF) << 5;
            break;
        case Kind::B14:
            if (delta < -(1 << 13) || delta >= (1 << 13)) throw std::logic_error("branch out of reach");
            word |= (uint32_t(delta) & 0x3FFF) << 5;
            break;
        case Kind::ADR: {
            const int64_t bytes = delta * 4;
            if (bytes < -(1 << 20) || bytes >= (1 << 20)) throw std::logic_error("adr out of reach");
            word |= (uint32_t(bytes) & 3) << 29 | ((uint32_t(bytes) >> 2) & 0x7FFFF) << 5;
            break;
        }
        }
    }
};

} // namespace kairotowns::jit
