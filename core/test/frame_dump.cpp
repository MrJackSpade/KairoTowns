// Runs an FM Towns disc on the host and writes its pictures, to look at what the core draws.
//
//   frame_dump <rom dir> <work dir> <disc image> <seconds> <frames between pictures> <skip|check>
//
// Writes <work dir>/frame-<n>.ppm, n being the frame number, every <frames between pictures>
// frames, and prints every change of picture size with the frame it happened on. skip skips the
// memory check as the app does, which runs as fast as real time allows, so the frames vary from run
// to run; check runs it and makes the run repeatable (Config::repeatable), so two runs of the same
// build draw the same pictures.

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

bool write_ppm(const std::string &file, const uint8_t *pixels, uint32_t width, uint32_t height) {
    FILE *out = std::fopen(file.c_str(), "wb");
    if (!out) return false;
    std::fprintf(out, "P6\n%u %u\n255\n", width, height);
    for (size_t i = 0; i < size_t(width) * height * 4; i += 4) std::fwrite(&pixels[i], 1, 3, out);
    return std::fclose(out) == 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 7) return fail("usage: frame_dump <rom dir> <work dir> <disc image> <seconds> <frames between pictures> <skip|check>");
    const std::string memory_check = argv[6];
    if (memory_check != "skip" && memory_check != "check") return fail("the memory check is skip or check");
    const std::string work = argv[2];
    const int frames = std::atoi(argv[4]) * 60;
    const int every = std::atoi(argv[5]);
    if (frames < 1 || every < 1) return fail("seconds and frames between pictures must be positive");
    Config config;
    config.rom_dir = argv[1];
    config.cmos_file = work + "/cmos.bin";
    config.cd = argv[3];
    config.skip_memory_check = memory_check == "skip";
    config.repeatable = memory_check == "check";

    std::string error;
    auto machine = TownsMachine::create(config, error);
    if (!machine) return fail("create: " + error);
    std::vector<int16_t> audio(4096 * 2);
    uint32_t last_width = 0, last_height = 0;
    for (int frame = 1; frame <= frames; ++frame) {
        if (!machine->run_frame(error)) return fail("run: " + error);
        while (machine->read_audio(audio.data(), 4096) > 0) {}
        uint32_t width = 0, height = 0;
        const uint8_t *pixels = machine->frame(width, height);
        if (!pixels) continue;
        if (width != last_width || height != last_height) {
            std::printf("frame %d: %ux%u\n", frame, width, height);
            last_width = width;
            last_height = height;
        }
        if (frame % every == 0 &&
            !write_ppm(work + "/frame-" + std::to_string(frame) + ".ppm", pixels, width, height))
            return fail("cannot write frame " + std::to_string(frame));
    }
    std::printf("PASS\n");
    return 0;
}
