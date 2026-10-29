// Finds where the guest keeps a lock key's state (Kairo#80): boots a disc, then taps the key twice
// and runs on without input, comparing main RAM between the four points. A candidate bit flips at
// each tap and at nothing else.
//
//   lock_probe <rom dir> <work dir> <disc image> <seconds> <key code> [<floppy>]
//
// <key code> is a Towns JIS key code (0x55 CAPS, 0x5A KATAKANA). Prints each candidate's address,
// bit, and its value after each tap.

#include "towns_machine.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using kairotowns::Config;
using kairotowns::TownsMachine;

namespace {

int fail(const std::string &message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    return 1;
}

bool run(TownsMachine &machine, int frames, std::string &error) {
    for (int i = 0; i < frames; ++i)
        if (!machine.run_frame(error)) return false;
    return true;
}

bool tap(TownsMachine &machine, int code, std::string &error) {
    machine.key(code, true);
    if (!run(machine, 6, error)) return false;
    machine.key(code, false);
    return run(machine, 54, error);
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 6 || argc > 7)
        return fail("usage: lock_probe <rom dir> <work dir> <disc image> <seconds> <key code> [<floppy>]");
    const std::string work = argv[2];
    Config config;
    config.rom_dir = argv[1];
    config.cmos_file = work + "/cmos.bin";
    config.cd = argv[3];
    if (argc == 7) config.fd[0] = argv[6];
    config.repeatable = true;
    const int code = int(std::strtol(argv[5], nullptr, 0));

    std::string error;
    auto machine = TownsMachine::create(config, error);
    if (!machine) return fail("create: " + error);
    if (!run(*machine, std::atoi(argv[4]) * 60, error)) return fail("boot: " + error);
    const std::vector<unsigned char> before = machine->ram();
    if (!tap(*machine, code, error)) return fail("tap: " + error);
    const std::vector<unsigned char> once = machine->ram();
    // The keyboard's lamps (TownsMachine::locks) after one tap, kept by a saved state.
    const int lit = machine->locks();
    if (!machine->save_state(work + "/lamp.state")) return fail("save");
    if (!tap(*machine, code, error)) return fail("tap: " + error);
    const std::vector<unsigned char> twice = machine->ram();
    const int unlit = machine->locks();
    if (!machine->load_state(work + "/lamp.state")) return fail("load");
    std::printf("lamps: %d after a tap, %d after another, %d after loading the first's state\n", lit, unlit,
                machine->locks());
    if (!machine->load_state(work + "/lamp.state") || !tap(*machine, code, error)) return fail("restore");
    if (!run(*machine, 60, error)) return fail("run: " + error);
    const std::vector<unsigned char> still = machine->ram();
    int found = 0;
    for (size_t address = 0; address < before.size(); ++address) {
        const unsigned flipped = (before[address] ^ once[address]) & (once[address] ^ twice[address]) &
                                 ~(before[address] ^ twice[address]) & ~(twice[address] ^ still[address]) & 0xffu;
        for (int bit = 0; bit < 8; ++bit)
            if (flipped & (1u << bit)) {
                std::printf("%06zx bit %d: %d -> %d -> %d\n", address, bit, (before[address] >> bit) & 1,
                            (once[address] >> bit) & 1, (twice[address] >> bit) & 1);
                ++found;
            }
    }
    std::printf("%d candidates in %zu bytes\n", found, before.size());
    // Real-mode interrupt vectors that may lead to the keyboard BIOS's data (segment:offset).
    for (int vector : {0x09, 0x16, 0x90, 0x91, 0x92, 0x93}) {
        const unsigned char *v = &still[size_t(vector) * 4];
        std::printf("INT %02Xh -> %04x:%04x\n", vector, v[2] | v[3] << 8, v[0] | v[1] << 8);
    }
    return 0;
}
