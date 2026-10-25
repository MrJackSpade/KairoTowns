// Measures where the host's time goes while an FM Towns disc runs, flat out.
//
//   game_profile <rom dir> <work dir> <disc image> <seconds> <skip|check> [pcs] [host] [noskip] [exact]
//
// Runs the disc for <seconds> of guest frames as fast as the host can and prints, for every 600
// frames, the host time a frame took (mean, 95th percentile, longest) and how much of it went to
// drawing pictures and to sound, then the same for the whole run. skip and check are the memory
// check, as in frame_dump. With pcs it also samples where the guest CPU is and prints the most
// frequent CS:EIP of the last half of the run. With host (Windows only) it samples the host
// instruction pointer of the emulation thread every millisecond over the last half of the run and
// writes the addresses to <work dir>/host-samples.txt, for llvm-addr2line against a build with -g.
// noskip runs every pass of the guest's wait loops instead of skipping them (wait_loop.h), to
// compare. exact makes the run repeatable (Config::repeatable), so runs with check repeat
// exactly, and prints a hash of every picture and all sound for each 600 frames; two builds or
// options that draw and sound the same print the same hashes. Run it under shared\tools\run-limited.ps1.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "towns_machine.h"

using kairotowns::Config;
using kairotowns::TownsMachine;

namespace {

int fail(const std::string &message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    return 1;
}

void report(const char *label, std::vector<double> frame_ms, const TownsMachine::Profile &from,
            const TownsMachine::Profile &to) {
    std::sort(frame_ms.begin(), frame_ms.end());
    const double frames = static_cast<double>(to.frames - from.frames);
    const double run = (to.run_ns - from.run_ns) / 1e6 / frames;
    const double picture = (to.picture_ns - from.picture_ns) / 1e6 / frames;
    const double sound = (to.sound_ns - from.sound_ns) / 1e6 / frames;
    std::printf("%s: %.0f frames, ms a frame mean %.2f p95 %.2f max %.2f; pictures %.2f ms (%.2f a frame), "
                "sound %.2f ms, the rest %.2f ms; %.3f M instructions a frame run, %.3f M skipped in %.0f skips\n",
                label, frames, run, frame_ms[frame_ms.size() * 95 / 100], frame_ms.back(), picture,
                (to.pictures - from.pictures) / frames, sound, run - picture - sound,
                (to.instructions - from.instructions) / frames / 1e6,
                (to.skipped_instructions - from.skipped_instructions) / frames / 1e6,
                (to.wait_skips - from.wait_skips) / frames);
}

#ifdef _WIN32
// Samples the instruction pointer of one thread every millisecond while running is set.
class HostSampler {
public:
    explicit HostSampler(HANDLE thread) : thread_(thread) {}
    void start() {
        running_ = true;
        worker_ = std::thread([this] {
            timeBeginPeriod(1);
            while (running_) {
                Sleep(1);
                if (SuspendThread(thread_) == DWORD(-1)) continue;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(thread_, &context)) ++samples_[context.Rip];
                ResumeThread(thread_);
            }
            timeEndPeriod(1);
        });
    }
    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }
    // Addresses relative to the executable's load address, as llvm-addr2line takes them, with counts.
    void write(const std::string &file) const {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        FILE *out = std::fopen(file.c_str(), "w");
        if (!out) return;
        for (const auto &sample : samples_)
            std::fprintf(out, "0x%llx %llu\n", (unsigned long long) (sample.first - base + 0x140000000ull),
                         (unsigned long long) sample.second);
        std::fclose(out);
    }

private:
    HANDLE thread_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::map<uintptr_t, uint64_t> samples_;
};
#endif

uint64_t fnv1a(uint64_t hash, const void *data, size_t bytes) {
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < bytes; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
    return hash;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 6) return fail("usage: game_profile <rom dir> <work dir> <disc image> <seconds> <skip|check> [pcs] [host] [noskip] [exact]");
    bool pcs = false, host = false, noskip = false, exact = false;
    for (int i = 6; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "pcs") pcs = true;
        else if (option == "host") host = true;
        else if (option == "noskip") noskip = true;
        else if (option == "exact") exact = true;
        else return fail("unknown option " + option);
    }
#ifdef _WIN32
    HANDLE self = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0);
    HostSampler sampler(self);
#else
    if (host) return fail("host sampling is Windows only; use perf");
#endif
    const std::string memory_check = argv[5];
    if (memory_check != "skip" && memory_check != "check") return fail("the memory check is skip or check");
    const int frames = std::atoi(argv[4]) * 60;
    if (frames < 600) return fail("run at least 10 seconds");
    Config config;
    config.rom_dir = argv[1];
    config.cmos_file = std::string(argv[2]) + "/cmos.bin";
    config.cd = argv[3];
    config.skip_memory_check = memory_check == "skip";
    config.skip_wait_loops = !noskip;
    config.repeatable = exact;

    std::string error;
    auto machine = TownsMachine::create(config, error);
    if (!machine) return fail("create: " + error);
    std::vector<int16_t> audio(4096 * 2);
    std::vector<double> window, all;
    TownsMachine::Profile window_start = machine->profile();
    const TownsMachine::Profile run_start = window_start;
    uint64_t hash = 14695981039346656037ull;
    for (int frame = 1; frame <= frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        if (!machine->run_frame(error)) return fail("run: " + error);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        uint32_t got;
        while ((got = machine->read_audio(audio.data(), 4096)) > 0)
            if (exact) hash = fnv1a(hash, audio.data(), got * 2 * sizeof(int16_t));
        if (exact) {
            uint32_t width = 0, height = 0;
            const uint8_t *pixels = machine->frame(width, height);
            if (pixels) hash = fnv1a(hash, pixels, size_t(width) * height * 4);
        }
        window.push_back(ms);
        all.push_back(ms);
        if (pcs && frame == frames / 2) machine->sample_pcs(true);
#ifdef _WIN32
        if (host && frame == frames / 2) sampler.start();
#endif
        if (frame % 600 == 0) {
            const std::string label = "frames " + std::to_string(frame - 599) + "-" + std::to_string(frame);
            report(label.c_str(), window, window_start, machine->profile());
            if (exact) std::printf("  hash %016llx\n", (unsigned long long) hash);
            window.clear();
            window_start = machine->profile();
        }
    }
    report("all", all, run_start, machine->profile());
#ifdef _WIN32
    if (host) {
        sampler.stop();
        sampler.write(std::string(argv[2]) + "/host-samples.txt");
    }
#endif
    if (pcs) {
        std::vector<std::pair<uint64_t, uint64_t>> top(machine->profile().pcs.begin(), machine->profile().pcs.end());
        uint64_t samples = 0;
        for (const auto &pc : top) samples += pc.second;
        std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        std::printf("CS:EIP of %llu samples, one every %llu instructions:\n", (unsigned long long) samples,
                    (unsigned long long) TownsMachine::kPcSampleInterval);
        for (size_t i = 0; i < top.size() && i < 30; ++i)
            std::printf("  %04X:%08X %6.2f%%\n", unsigned(top[i].first >> 32), unsigned(top[i].first & 0xffffffff),
                        100.0 * top[i].second / samples);
        // The code around the most frequent place, in the segment the CPU is in now.
        uint32_t from = uint32_t(top[0].first);
        for (size_t i = 1; i < top.size() && i < 8; ++i)
            if (top[i].first >> 32 == top[0].first >> 32) from = std::min(from, uint32_t(top[i].first));
        std::printf("From %04X:%08X:\n%s", unsigned(top[0].first >> 32), from,
                    machine->disassemble(uint16_t(top[0].first >> 32), from, 12).c_str());
    }
    return 0;
}
