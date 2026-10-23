// Boots the FM Towns with no media twice, skipping its power-on memory test and then running it,
// and writes one picture a second of each to show the difference.
//
//   memory_check_test <rom dir> <work dir> <seconds>
//
// Writes <work dir>/skip-<n>.ppm and <work dir>/check-<n>.ppm for n = 1..<seconds>, and fails
// unless the two boots differ by the end: a skipped test must change what the BIOS shows.

#include <cstdio>
#include <cstdlib>
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

bool write_ppm(const std::string &file, TownsMachine &machine) {
    uint32_t width = 0, height = 0;
    const uint8_t *pixels = machine.frame(width, height);
    if (!pixels) return false;
    FILE *out = std::fopen(file.c_str(), "wb");
    if (!out) return false;
    std::fprintf(out, "P6\n%u %u\n255\n", width, height);
    for (size_t i = 0; i < size_t(width) * height * 4; i += 4) std::fwrite(&pixels[i], 1, 3, out);
    std::fclose(out);
    return true;
}

// The last picture of the boot, after one picture a second.
bool boot(const std::string &rom_dir, const std::string &work, bool skip, int seconds,
          std::vector<uint8_t> &last, std::string &error) {
    Config config;
    config.rom_dir = rom_dir;
    config.cmos_file = work + (skip ? "/skip-cmos.bin" : "/check-cmos.bin");
    config.skip_memory_check = skip;
    auto machine = TownsMachine::create(config, error);
    if (!machine) return false;
    std::vector<int16_t> audio(4096 * 2);
    for (int second = 1; second <= seconds; ++second) {
        for (int frame = 0; frame < 60; ++frame) {
            if (!machine->run_frame(error)) return false;
            while (machine->read_audio(audio.data(), 4096) > 0) {}
        }
        const std::string name = work + (skip ? "/skip-" : "/check-") + std::to_string(second) + ".ppm";
        if (!write_ppm(name, *machine)) { error = "no picture at second " + std::to_string(second); return false; }
    }
    uint32_t width = 0, height = 0;
    const uint8_t *pixels = machine->frame(width, height);
    last.assign(pixels, pixels + size_t(width) * height * 4);
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) return fail("usage: memory_check_test <rom dir> <work dir> <seconds>");
    const int seconds = std::atoi(argv[3]);
    if (seconds < 1 || seconds > 60) return fail("seconds must be 1 to 60");
    std::string error;
    std::vector<uint8_t> skipped, checked;
    if (!boot(argv[1], argv[2], true, seconds, skipped, error)) return fail("skipping boot: " + error);
    if (!boot(argv[1], argv[2], false, seconds, checked, error)) return fail("checking boot: " + error);
    if (skipped == checked) return fail("the boots end on the same picture");
    std::printf("PASS: the boots differ after %d seconds\n", seconds);
    return 0;
}
