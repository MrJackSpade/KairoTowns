// An FM Towns on Tsugaru (third_party/tsugaru), run one guest frame at a time by its host
// instead of Tsugaru's own real-time VM thread (towns/townsthread.cpp). It renders each new
// picture as RGBA and keeps the sound Tsugaru mixes (FM, PCM, CD audio and the buzzer) in step
// with guest time.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kairotowns {

// What a machine starts with.
struct Config {
    // The folder of the ROM images: FMT_SYS.ROM, FMT_DOS.ROM, FMT_FNT.ROM, FMT_DIC.ROM, and
    // optionally FMT_F20.ROM.
    std::string rom_dir;
    // The CMOS RAM file, kept between sessions; created when missing.
    std::string cmos_file;
    // A disc image (.ccd, .cue, .mds or .iso) and two floppy images; "" for none.
    std::string cd;
    std::string fd[2];
    // Main memory in megabytes.
    unsigned memory_mb = 4;
    // The CPU clock in MHz.
    unsigned cpu_mhz = 40;
    // Whether the boot runs as fast as the host can until its first disk read, so the BIOS
    // memory test passes in a moment (this BIOS ignores the CMOS setting that skips it).
    bool skip_memory_check = true;
    // Whether the passes of a wait loop that cannot change anything are skipped (wait_loop.h). It
    // changes nothing the guest does; off only to compare against.
    bool skip_wait_loops = true;
    // Whether a run repeats exactly, for tests that compare two runs: the clock starts at a fixed
    // time and runs with guest time, and a CD audio read finishes before the guest goes on (the
    // app reads it in the background).
    bool repeatable = false;
};

// The gamepad in game port 0. A, B, RUN and SELECT are the pad's buttons.
enum PadButton : unsigned {
    PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8, PAD_A = 16, PAD_B = 32, PAD_RUN = 64, PAD_SELECT = 128
};

class TownsMachine {
public:
    static constexpr int kAudioRate = 44100;

    // A machine booting from config's media; null with error set when it cannot start.
    static std::unique_ptr<TownsMachine> create(const Config &config, std::string &error);
    ~TownsMachine();

    // Runs at least 1/60 second of guest time; false with error set when the machine stops.
    bool run_frame(std::string &error);
    // The guest time the last run_frame() ran. The CPU skips idle time in whole steps, so a frame
    // can run longer than 1/60 second.
    double frame_seconds() const;
    // The latest picture, RGBA rows of width pixels; null before the first one.
    const uint8_t *frame(uint32_t &width, uint32_t &height) const;
    // Takes up to max_frames interleaved stereo frames produced so far.
    uint32_t read_audio(int16_t *stereo, uint32_t max_frames);

    // A key by its JIS key code (TOWNS_JISKEY_*, towns/townsdef/townsdef.h).
    void key(int code, bool down);
    // The held PadButton bits of the gamepad in port 0.
    void pad(unsigned buttons);
    // The mouse in port 1: motion in mickeys, and button 1 (left) or 2 (right).
    void mouse_move(int dx, int dy);
    void mouse_button(int button, bool down);

    // Host time run_frame() has spent since the machine started, in all and in its parts, for
    // measuring where a frame's time goes.
    struct Profile {
        uint64_t frames = 0, pictures = 0, instructions = 0;
        // Instructions of wait-loop passes skipped instead of run (wait_loop.h), and the skips.
        uint64_t skipped_instructions = 0, wait_skips = 0;
        uint64_t run_ns = 0, picture_ns = 0, sound_ns = 0;
        // With sample_pcs on: how often each CS:EIP was the next instruction, one sample every
        // kPcSampleInterval instructions, keyed by CS << 32 | EIP.
        std::map<uint64_t, uint64_t> pcs;
    };
    static constexpr uint64_t kPcSampleInterval = 4096;
    const Profile &profile() const;
    void sample_pcs(bool on);
    // count instructions from selector:eip, one a line, for measuring tools.
    std::string disassemble(uint16_t selector, uint32_t eip, int count) const;

    void reset();
    bool save_state(const std::string &file);
    bool load_state(const std::string &file);

    // Inserts an image, or ejects it for ""; true when it is in.
    bool insert_cd(const std::string &path);
    bool insert_fd(int drive, const std::string &path);
    std::string cd_path() const;
    std::string fd_path(int drive) const;

private:
    struct Impl;
    bool run_frame_body(std::string &error);
    // Runs one frame of guest time; [ran] is how much ran.
    bool run_guest(uint64_t &ran, std::string &error);
    explicit TownsMachine(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace kairotowns
