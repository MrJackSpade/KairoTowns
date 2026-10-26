// Checks the JIT's AArch64 assembler (core/jit/a64_assembler.h) against a real assembler.
//
//   a64_encoding_test <out dir>
//
// Writes <out dir>/expected.s, every instruction form the assembler emits in assembly syntax, and
// <out dir>/generated.bin, the same instructions as the assembler encodes them, one 32-bit word
// each. tools/check-a64-encoding.ps1 assembles expected.s with clang and compares the words.

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "jit/a64_assembler.h"

using namespace kairotowns::jit;

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: a64_encoding_test <out dir>\n");
        return 1;
    }
    Assembler a;
    std::vector<std::string> lines;
    auto check = [&](const std::string &text, auto emit) {
        const size_t before = a.size();
        emit();
        if (a.size() != before + 4) {
            std::fprintf(stderr, "FAIL: %s emitted %zu bytes\n", text.c_str(), a.size() - before);
            std::exit(1);
        }
        lines.push_back(text);
    };
    for (uint32_t s : {0u, 16u}) {
        check("movz w3, #0xABCD, lsl #" + std::to_string(s), [&] { a.movz(false, 3, 0xABCD, s); });
        check("movk w3, #0x1234, lsl #" + std::to_string(s), [&] { a.movk(false, 3, 0x1234, s); });
        check("movn w3, #0x1234, lsl #" + std::to_string(s), [&] { a.movn(false, 3, 0x1234, s); });
    }
    check("movz x16, #0xBEEF, lsl #48", [&] { a.movz(true, 16, 0xBEEF, 48); });
    check("movk x16, #0xBEEF, lsl #32", [&] { a.movk(true, 16, 0xBEEF, 32); });
    check("mov w5, w21", [&] { a.mov(false, 5, 21); });
    check("mov x19, x0", [&] { a.mov(true, 19, 0); });
    check("add w1, w2, #4095", [&] { a.add_imm(false, 1, 2, 4095); });
    check("add x16, x16, #8", [&] { a.add_imm(true, 16, 16, 8); });
    check("sub w15, w15, #7", [&] { a.sub_imm(false, 15, 15, 7); });
    check("cmp w15, #3000", [&] { a.cmp_imm(false, 15, 3000); });
    check("add w4, w21, w22, lsl #2", [&] { a.add(false, 4, 21, 22, LSL, 2); });
    check("add x16, x16, x13, lsl #4", [&] { a.add(true, 16, 16, 13, LSL, 4); });
    check("sub w1, w2, w3", [&] { a.sub(false, 1, 2, 3); });
    check("cmp w9, w10", [&] { a.cmp(false, 9, 10); });
    check("cmp x16, x17", [&] { a.cmp(true, 16, 17); });
    check("neg w13, w1", [&] { a.neg(false, 13, 1); });
    check("add x6, x16, w8, uxtw", [&] { a.add_ext(true, 6, 16, 8, UXTW); });
    check("add x17, x17, w7, uxtw", [&] { a.add_ext(true, 17, 17, 7, UXTW); });
    check("and w1, w2, w3", [&] { a.and_(false, 1, 2, 3); });
    check("orr w13, w12, w12, lsl #8", [&] { a.orr(false, 13, 12, 12, LSL, 8); });
    check("eor w13, w12, w12, lsr #4", [&] { a.eor(false, 13, 12, 12, LSR, 4); });
    check("bic w1, w12, w13", [&] { a.bic(false, 1, 12, 13); });
    check("mvn w2, w0", [&] { a.mvn(false, 2, 0); });
    check("tst w12, w13", [&] { a.tst(false, 12, 13); });
    for (uint32_t imm : {0xFFFFF000u, 0xFFFu, 0xFFu, 0xFFFFu, 0xFFFFFFFEu, 0xFFFFFBFFu, 0xFFFFFDFFu, 1u, 0x400u, 0x80u, 0x40u, 31u, 0x10u})
        check("and w5, w6, #" + std::to_string(imm), [&] { a.and_imm(false, 5, 6, imm); });
    check("orr w12, w12, #1", [&] { a.orr_imm(false, 12, 12, 1); });
    check("eor w12, w12, #1", [&] { a.eor_imm(false, 12, 12, 1); });
    check("tst w22, #0xffff", [&] { a.tst_imm(false, 22, 0xFFFF); });
    check("mov w3, #0x55555555", [&] { a.mov_imm32(3, 0x55555555u); });
    check("lsl w1, w2, #3", [&] { a.lsl_imm(false, 1, 2, 3); });
    check("lsl w1, w2, #24", [&] { a.lsl_imm(false, 1, 2, 24); });
    check("lsr w13, w5, #12", [&] { a.lsr_imm(false, 13, 5, 12); });
    check("asr w23, w21, #31", [&] { a.asr_imm(false, 23, 21, 31); });
    check("ubfx w0, w21, #8, #8", [&] { a.ubfx(false, 0, 21, 8, 8); });
    check("sbfx w0, w21, #15, #1", [&] { a.sbfx(false, 0, 21, 15, 1); });
    check("bfi w21, w0, #8, #8", [&] { a.bfi(false, 21, 0, 8, 8); });
    check("bfi w21, w0, #0, #16", [&] { a.bfi(false, 21, 0, 0, 16); });
    check("bfi w12, w13, #4, #1", [&] { a.bfi(false, 12, 13, 4, 1); });
    check("bfxil w8, w5, #0, #12", [&] { a.bfxil(false, 8, 5, 0, 12); });
    check("uxtb w1, w2", [&] { a.uxtb(1, 2); });
    check("uxth w1, w2", [&] { a.uxth(1, 2); });
    check("sxtb w1, w2", [&] { a.sxtb(1, 2); });
    check("sxth w21, w21", [&] { a.sxth(21, 21); });
    check("lsl w14, w0, w1", [&] { a.lslv(false, 14, 0, 1); });
    check("lsr w14, w0, w1", [&] { a.lsrv(false, 14, 0, 1); });
    check("asr w14, w0, w1", [&] { a.asrv(false, 14, 0, 1); });
    check("ror w14, w14, w13", [&] { a.rorv(false, 14, 14, 13); });
    check("ror w1, w2, #5", [&] { a.ror_imm(false, 1, 2, 5); });
    check("rev w1, w2", [&] { a.rev(1, 2); });
    check("rev16 w1, w2", [&] { a.rev16(1, 2); });
    check("mul w1, w2, w3", [&] { a.mul(false, 1, 2, 3); });
    check("smull x1, w2, w3", [&] { a.smull(1, 2, 3); });
    check("umull x1, w2, w3", [&] { a.umull(1, 2, 3); });
    check("cset w13, lo", [&] { a.cset(false, 13, LO); });
    check("csel w1, w2, w3, ne", [&] { a.csel(false, 1, 2, 3, NE); });
    check("ldrb w0, [x6]", [&] { a.ldrb(0, 6); });
    check("ldrh w0, [x6]", [&] { a.ldrh(0, 6); });
    check("ldr w0, [x6]", [&] { a.ldr_w(0, 6); });
    check("ldr w13, [x20, #1236]", [&] { a.ldr_w(13, 20, 1236); });
    check("ldr x16, [x19, #96]", [&] { a.ldr_x(16, 19, 96); });
    check("strb w0, [x6]", [&] { a.strb(0, 6); });
    check("strh w0, [x6]", [&] { a.strh(0, 6); });
    check("str w12, [x20, #48]", [&] { a.str_w(12, 20, 48); });
    check("str x17, [x16]", [&] { a.str_x(17, 16); });
    check("ldr x16, [x16, w13, uxtw #3]", [&] { a.ldr_x_r(16, 16, 13, UXTW, true); });
    check("ldrb w13, [x16, w13, uxtw]", [&] { a.ldrb_r(13, 16, 13); });
    check("ldp w21, w22, [x20, #4]", [&] { a.ldp_w(21, 22, 20, 4); });
    check("stp w27, w28, [x20, #28]", [&] { a.stp_w(27, 28, 20, 28); });
    check("ldp x16, x17, [x19, #88]", [&] { a.ldp_x(16, 17, 19, 88); });
    check("stp x29, x30, [sp, #-96]!", [&] { a.stp_x_pre(29, 30, SP, -96); });
    check("stp x19, x20, [sp, #16]", [&] { a.stp_x(19, 20, SP, 16); });
    check("ldp x29, x30, [sp], #96", [&] { a.ldp_x_post(29, 30, SP, 96); });
    check("sub x17, x6, w5, uxtw", [&] { a.sub_ext(true, 17, 6, 5, UXTW); });
    check("add w14, w13, w29", [&] { a.add(false, 14, 13, 29); });
    check("ldur x17, [x16, #-8]", [&] { a.ldur_x(17, 16, -8); });
    check("ldur w17, [x16, #3]", [&] { a.ldur_w(17, 16, 3); });
    check("ldurh w17, [x16, #1]", [&] { a.ldurh(17, 16, 1); });
    check("br x16", [&] { a.br(16); });
    check("blr x16", [&] { a.blr(16); });
    check("ret", [&] { a.ret(); });
    // Branches to a label 3 instructions on, so the expected text uses a numeric offset.
    auto forward = [&](const std::string &text, auto emit) {
        Assembler::Label l;
        check(text, [&] { emit(l); });
        a.emit(0xD503201Fu);  // nop
        a.emit(0xD503201Fu);
        lines.push_back("nop");
        lines.push_back("nop");
        a.bind(l);
    };
    forward("b . + 12", [&](Assembler::Label &l) { a.b(l); });
    forward("b.ne . + 12", [&](Assembler::Label &l) { a.b_cond(NE, l); });
    forward("b.lt . + 12", [&](Assembler::Label &l) { a.b_cond(LT, l); });
    forward("cbz w11, . + 12", [&](Assembler::Label &l) { a.cbz(false, 11, l); });
    forward("cbnz w0, . + 12", [&](Assembler::Label &l) { a.cbnz(false, 0, l); });
    forward("cbz x17, . + 12", [&](Assembler::Label &l) { a.cbz(true, 17, l); });
    forward("tbnz w11, #31, . + 12", [&](Assembler::Label &l) { a.tbnz(11, 31, l); });
    forward("tbz w12, #6, . + 12", [&](Assembler::Label &l) { a.tbz(12, 6, l); });
    forward("adr x0, . + 12", [&](Assembler::Label &l) { a.adr(0, l); });
    forward("ldr x16, . + 12", [&](Assembler::Label &l) { a.ldr_x_literal(16, l); });
    {
        // A backward branch.
        Assembler::Label l;
        a.bind(l);
        a.emit(0xD503201Fu);
        lines.push_back("nop");
        check("b . - 4", [&] { a.b(l); });
    }

    const std::string dir = argv[1];
    FILE *s = std::fopen((dir + "/expected.s").c_str(), "w");
    FILE *b = std::fopen((dir + "/generated.bin").c_str(), "wb");
    if (!s || !b) {
        std::fprintf(stderr, "FAIL: cannot write to %s\n", dir.c_str());
        return 1;
    }
    std::fprintf(s, "    .text\n");
    for (const auto &line : lines) std::fprintf(s, "    %s\n", line.c_str());
    std::fwrite(a.code(), 1, a.size(), b);
    std::fclose(s);
    std::fclose(b);
    std::printf("%zu instructions\n", lines.size());
    return 0;
}
