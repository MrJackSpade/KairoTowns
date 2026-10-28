// Boots an FM Towns disc on the host and checks what the app relies on.
//
//   boot_test <rom dir> <work dir> <disc image> <seconds>
//
// Runs the disc for <seconds>, writing <work dir>/boot.ppm of the last picture and reporting
// its audio. Then it saves a state, runs on, ejects the disc, loads the state, and checks that the
// disc is back in the drive as the machine reports it (cd_path) and that the next second draws the
// same picture as it did after the save. Last it starts a second machine in the same
// process, as the app does for its next game.

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

struct Run {
    std::vector<uint8_t> picture;
    uint32_t width = 0, height = 0;
    uint64_t audio_frames = 0, loud_frames = 0;
    double seconds = 0;
    std::vector<int16_t> sound;
};

bool run(TownsMachine &machine, int frames, Run &result, std::string &error) {
    std::vector<int16_t> audio(4096 * 2);
    for (int i = 0; i < frames; ++i) {
        if (!machine.run_frame(error)) return false;
        result.seconds += machine.frame_seconds();
        uint32_t got;
        while ((got = machine.read_audio(audio.data(), 4096)) > 0) {
            result.audio_frames += got;
            result.sound.insert(result.sound.end(), audio.begin(), audio.begin() + got * 2);
            for (uint32_t f = 0; f < got; ++f)
                if (std::abs(audio[f * 2]) > 64 || std::abs(audio[f * 2 + 1]) > 64) ++result.loud_frames;
        }
    }
    const uint8_t *pixels = machine.frame(result.width, result.height);
    if (pixels) result.picture.assign(pixels, pixels + result.width * result.height * 4);
    return true;
}

void write_ppm(const std::string &file, const Run &run) {
    FILE *out = std::fopen(file.c_str(), "wb");
    if (!out) return;
    std::fprintf(out, "P6\n%u %u\n255\n", run.width, run.height);
    for (size_t i = 0; i < run.picture.size(); i += 4) std::fwrite(&run.picture[i], 1, 3, out);
    std::fclose(out);
}

void write_wav(const std::string &file, const std::vector<int16_t> &sound) {
    FILE *out = std::fopen(file.c_str(), "wb");
    if (!out) return;
    const uint32_t bytes = static_cast<uint32_t>(sound.size() * 2), rate = TownsMachine::kAudioRate;
    auto u32 = [out](uint32_t v) { std::fwrite(&v, 4, 1, out); };
    auto u16 = [out](uint16_t v) { std::fwrite(&v, 2, 1, out); };
    std::fwrite("RIFF", 1, 4, out); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, out);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, out); u32(bytes);
    std::fwrite(sound.data(), 2, sound.size(), out);
    std::fclose(out);
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 5) return fail("usage: boot_test <rom dir> <work dir> <disc image> <seconds>");
    const std::string work = argv[2];
    Config config;
    config.rom_dir = argv[1];
    config.cmos_file = work + "/cmos.bin";
    config.cd = argv[3];
    // The state round trip compares two runs of the same second, so the run must repeat: CD audio
    // read in the background and the host's clock would make them differ (Config::repeatable).
    config.repeatable = true;
    const int frames = std::atoi(argv[4]) * 60;

    std::string error;
    auto machine = TownsMachine::create(config, error);
    if (!machine) return fail("create: " + error);
    // Half the time on the title, then RUN and Return held for half a second, as a player starts.
    Run boot, title, started;
    if (!run(*machine, frames / 2, title, error)) return fail("run: " + error);
    write_ppm(work + "/title.ppm", title);
    machine->pad(kairotowns::PAD_RUN);
    machine->key(0x1D, true);
    if (!run(*machine, 30, boot, error)) return fail("run: " + error);
    machine->pad(0);
    machine->key(0x1D, false);
    if (!run(*machine, frames / 2 - 30, started, error)) return fail("run: " + error);
    boot.seconds += title.seconds + started.seconds;
    boot.audio_frames += title.audio_frames + started.audio_frames;
    boot.loud_frames += title.loud_frames + started.loud_frames;
    boot.picture = started.picture;
    boot.width = started.width;
    boot.height = started.height;
    std::vector<int16_t> sound = title.sound;
    sound.insert(sound.end(), boot.sound.begin(), boot.sound.end());
    sound.insert(sound.end(), started.sound.begin(), started.sound.end());
    write_wav(work + "/boot.wav", sound);
    write_ppm(work + "/boot.ppm", boot);
    std::printf("boot: %ux%u picture, %llu audio frames (%llu audible) for %d video frames, %.2f guest seconds\n",
                boot.width, boot.height, (unsigned long long) boot.audio_frames, (unsigned long long) boot.loud_frames,
                frames, boot.seconds);
    if (boot.picture.empty()) return fail("no picture");
    const double expected = boot.seconds * TownsMachine::kAudioRate;
    if (boot.audio_frames < expected * 0.98 || boot.audio_frames > expected * 1.02)
        return fail("audio does not follow guest time");

    const std::string state = work + "/state.bin";
    if (!machine->save_state(state)) return fail("save state");
    Run after_save, after_load, ignored;
    if (!run(*machine, 60, after_save, error)) return fail("run after save: " + error);
    if (!run(*machine, 120, ignored, error)) return fail("run on: " + error);
    const std::string disc = machine->cd_path();
    if (disc.empty()) return fail("no disc reported in the drive");
    if (!machine->insert_cd("") || !machine->cd_path().empty()) return fail("eject the disc");
    if (!machine->load_state(state)) return fail("load state");
    std::printf("state: the drive holds %s after the load\n", machine->cd_path().c_str());
    if (machine->cd_path() != disc) return fail("the drive does not report the state's disc");
    if (!run(*machine, 60, after_load, error)) return fail("run after load: " + error);
    write_ppm(work + "/after_save.ppm", after_save);
    write_ppm(work + "/after_load.ppm", after_load);
    std::printf("state: the second after load %s the second after save\n",
                after_save.picture == after_load.picture ? "matches" : "differs from");
    if (after_save.picture != after_load.picture) return fail("state round trip");

    machine.reset();
    auto second = TownsMachine::create(config, error);
    if (!second) return fail("second machine: " + error);
    Run again;
    if (!run(*second, frames, again, error)) return fail("second run: " + error);
    write_ppm(work + "/second.ppm", again);
    std::printf("second machine: %ux%u picture, %llu audio frames\n", again.width, again.height,
                (unsigned long long) again.audio_frames);
    std::printf("PASS\n");
    return 0;
}
