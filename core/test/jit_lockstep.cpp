// Finds where translated code (core/jit) and the interpreter part ways, on a host with the JIT.
//
//   jit_lockstep <rom dir> <work dir> <disc image> <steps> [noskip]
//
// Runs two machines from the same start, one with the JIT and one without, a step (about a
// millisecond of guest time) at a time, and compares the CPU, guest time and main RAM after each
// step. At the first step that differs it runs both again to the step before, traces that step
// (the CPU after every run of translated code, and the interpreter's CPU at the same instruction
// counts) and prints the first run whose result differs, with its code. Instruction counts may
// part ways while all else stays equal: the wait-loop skip (wait_loop.h) counts the passes it
// skips apart, and may skip one pass sooner in one machine; the trace then aligns by count and
// shows that first. Machines run as
// game_profile's exact runs do (memory check, repeatable). Run it under shared\tools\run-limited.ps1
// on a host, or through adb on a device. With noskip both machines run every pass of
// the guest's wait loops instead of skipping them (wait_loop.h), so the comparison does not lean
// on the skip taking the same passes in both.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "towns_machine.h"

using kairotowns::Config;
using kairotowns::TownsMachine;

namespace {

int fail(const std::string &message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    return 1;
}

bool noskip = false;

std::unique_ptr<TownsMachine> make(char **argv, bool jit, const std::string &name) {
    const std::string dir = std::string(argv[2]) + "/" + name;
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir + "/cmos.bin");
    Config config;
    config.rom_dir = argv[1];
    config.cmos_file = dir + "/cmos.bin";
    config.cd = argv[3];
    config.skip_memory_check = false;
    config.repeatable = true;
    config.jit = jit;
    config.skip_wait_loops = !noskip;
    std::string error;
    auto machine = TownsMachine::create(config, error);
    if (!machine) {
        std::fprintf(stderr, "FAIL: create: %s\n", error.c_str());
        std::exit(1);
    }
    return machine;
}

void step(TownsMachine &machine, const char *which) {
    uint64_t ran = 0;
    std::string error;
    if (!machine.run_step(ran, error)) {
        std::fprintf(stderr, "FAIL: %s machine: %s\n", which, error.c_str());
        std::exit(1);
    }
}

void print(const char *label, const TownsMachine::CpuSnapshot &s) {
    static const char *names[8] = {"EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"};
    std::printf("  %-12s after %llu instructions: CS:EIP %04X:%08X EFLAGS %08X time %llu+%llu side effects %llu\n   ",
                label, (unsigned long long) s.instructions, s.cs, s.eip, s.eflags, (unsigned long long) s.time,
                (unsigned long long) s.clock_balance, (unsigned long long) s.side_effects);
    for (int i = 0; i < 8; ++i) std::printf(" %s %08X", names[i], s.regs[i]);
    std::printf("\n");
}

// The first few addresses where the two machines' RAM differs.
size_t ram_differences(const TownsMachine &a, const TownsMachine &b, int show) {
    const auto &x = a.ram(), &y = b.ram();
    if (x.size() != y.size()) {
        std::printf("  RAM sizes differ: %zu, %zu\n", x.size(), y.size());
        return 1;
    }
    size_t count = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        if (x[i] == y[i]) continue;
        if (int(count) < show) std::printf("  RAM %08zX: translated %02X, interpreted %02X\n", i, x[i], y[i]);
        ++count;
    }
    return count;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 5 && !(argc == 6 && std::string(argv[5]) == "noskip"))
        return fail("usage: jit_lockstep <rom dir> <work dir> <disc image> <steps> [noskip]");
    noskip = argc == 6;
    const long steps = std::atol(argv[4]);
    auto a = make(argv, true, "translated");
    auto b = make(argv, false, "interpreted");
    long differ = -1;
    for (long s = 0; s < steps; ++s) {
        step(*a, "translated");
        step(*b, "interpreted");
        if (!(a->snapshot() == b->snapshot()) || a->ram() != b->ram()) {
            differ = s;
            break;
        }
        if ((s + 1) % 1000 == 0) {
            std::printf("step %ld: same, %llu instructions\n", s + 1, (unsigned long long) a->snapshot().instructions);
            std::fflush(stdout);
        }
    }
    if (differ < 0) {
        std::printf("same after %ld steps, %llu instructions\n", steps, (unsigned long long) a->snapshot().instructions);
        return 0;
    }
    std::printf("DIFFER after step %ld\n", differ + 1);
    print("translated", a->snapshot());
    print("interpreted", b->snapshot());
    std::printf("  %zu RAM bytes differ\n", ram_differences(*a, *b, 8));

    // The same step again, traced.
    a = make(argv, true, "translated");
    b = make(argv, false, "interpreted");
    for (long s = 0; s < differ; ++s) {
        step(*a, "translated");
        step(*b, "interpreted");
    }
    std::vector<TownsMachine::CpuSnapshot> translated, interpreted;
    a->trace_translated(&translated);
    TownsMachine::CpuSnapshot before = a->snapshot();
    step(*a, "translated");
    a->trace_translated(nullptr);
    std::vector<uint64_t> counts;
    for (const auto &s : translated) counts.push_back(s.instructions);
    b->trace_interpreted(&counts, &interpreted);
    step(*b, "interpreted");
    b->trace_interpreted(nullptr, nullptr);
    std::printf("traced %zu runs of translated code, %zu interpreter points\n", translated.size(), interpreted.size());
    size_t j = 0;
    for (size_t i = 0; i < translated.size(); ++i) {
        while (j < interpreted.size() && interpreted[j].instructions < translated[i].instructions) ++j;
        const bool found = j < interpreted.size() && interpreted[j].instructions == translated[i].instructions;
        if (found && translated[i] == interpreted[j]) {
            before = translated[i];
            continue;
        }
        std::printf("FIRST DIFFERENT RUN: translated code from %04X:%08X\n", translated[i].cs, translated[i].start_eip);
        print("before", before);
        print("translated", translated[i]);
        if (found) print("interpreted", interpreted[j]);
        else std::printf("  the interpreter never reached instruction %llu\n", (unsigned long long) translated[i].instructions);
        // The code's bytes as they were when the run started.
        const uint32_t at = translated[i].cs_base + translated[i].start_eip;
        std::printf("  CS base %08X, mode %u; code at %08X:", translated[i].cs_base, translated[i].cpu_mode, at);
        for (uint8_t byte : translated[i].code) std::printf(" %02X", byte);
        std::printf("\n");
        return 2;
    }
    std::printf("every run of translated code ended as the interpreter did; RAM after the step:\n");
    ram_differences(*a, *b, 16);
    return 2;
}
