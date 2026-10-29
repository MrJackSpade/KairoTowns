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

#include "kairo/startup_typer.h"

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
    // Hard disk images by SCSI ID ("" for none), written in place as the guest writes them.
    static constexpr int kHardDisks = 7;
    std::string hd[kHardDisks];
    // Whether the machine boots its hard disk although a CD is given: the BIOS boots a CD before a hard
    // disk, so the drive starts empty, as a player would power on, and the CD goes in once the boot
    // has read its first disk. (Tsugaru's boot keys do not help: this BIOS reads no key before it boots.)
    bool boot_hd = false;
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
    // Whether the CPU runs translated code where it can, on hosts that have the JIT (jit/jit.h).
    // It changes nothing the guest does. Off by default: it is not yet faster where it matters
    // and stalls frames while it translates (KairoTowns#19).
    bool jit = false;
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

    // The machine as the shared typer types a game's startup commands (kairo/startup_typer.h): a
    // line goes in when the guest has called DOS's line input (INT 21h AH=0Ah, where COMMAND.COM
    // waits for a command) since the last key, typed on the JIS keyboard as Tsugaru types text.
    kairo::Prompt prompt() const;
    static bool typed_key(char c, int &code, bool &shift);
    static int special_key(kairo::SpecialKey key);
    static bool expand_command(std::string &line) { (void) line; return true; }
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
        // Instructions translated code ran, of instructions, and the runs.
        uint64_t jit_instructions = 0, jit_runs = 0;
        // Translations made, and those whose first instruction was left to the interpreter.
        uint64_t jit_translations = 0, jit_refused = 0;
        // Code found changed at a block's entry, blocks linked, and code buffer flushes.
        uint64_t jit_mismatches = 0, jit_links = 0, jit_flushes = 0;
        // With sample_pcs on: how often each CS:EIP was the next instruction, one sample every
        // kPcSampleInterval instructions, keyed by CS << 32 | EIP.
        std::map<uint64_t, uint64_t> pcs;
        // With count_ops on: how many instructions of each kind the interpreter ran (keyed as
        // op_key() describes), and how many of them went on somewhere other than the instruction
        // after them.
        std::map<uint32_t, uint64_t> ops;
        uint64_t transfers = 0;
        // With count_ops on, on x86-64 hosts: the host's time stamp counter ticks each kind took,
        // from before the instruction to after the interrupt check that follows it.
        std::map<uint32_t, uint64_t> op_ticks;
    };
    static constexpr uint64_t kPcSampleInterval = 4096;
    const Profile &profile() const;
    void sample_pcs(bool on);
    void count_ops(bool on);
    // An ops key as text: the opcode, its ModR/M REG field for group opcodes, a memory first
    // operand, the operand and address size, a REP prefix and the CPU mode.
    static std::string op_key(uint32_t key);
    // count instructions from selector:eip, one a line, for measuring tools.
    std::string disassemble(uint16_t selector, uint32_t eip, int count) const;

    // For test tools (core/test/jit_lockstep.cpp): one step of about a millisecond of guest time,
    // as run_frame runs them; [ran] is how much ran.
    bool run_step(uint64_t &ran, std::string &error);
    // The CPU and guest time at a moment.
    struct CpuSnapshot {
        uint64_t instructions = 0;  // run before it
        uint32_t start_eip = 0;     // where the run of translated code that ended here started
        uint32_t regs[8] = {}, eip = 0, eflags = 0;
        uint16_t cs = 0;
        uint32_t cs_base = 0, cpu_mode = 0;
        // With a translated trace, the code where the run started, as it was then.
        uint8_t code[32] = {};
        uint64_t time = 0, clock_balance = 0, side_effects = 0;
        // Equal in all the guest can tell, the instruction count aside.
        bool operator==(const CpuSnapshot &o) const;
    };
    CpuSnapshot snapshot() const;
    // With out set, every run of translated code adds the CPU after it to out.
    void trace_translated(std::vector<CpuSnapshot> *out);
    // With out set, the interpreter adds the CPU after each instruction whose count (instructions
    // run, as CpuSnapshot::instructions counts them) is in counts, ascending.
    void trace_interpreted(const std::vector<uint64_t> *counts, std::vector<CpuSnapshot> *out);
    const std::vector<unsigned char> &ram() const;

    void reset();
    bool save_state(const std::string &file);
    bool load_state(const std::string &file);

    // Inserts an image, or ejects it for ""; true when it is in.
    bool insert_cd(const std::string &path);
    bool insert_fd(int drive, const std::string &path);
    // The hard disk at a SCSI ID ("" ejects it), as a SCSI device attached while running.
    bool insert_hd(int id, const std::string &path);
    std::string cd_path() const;
    std::string fd_path(int drive) const;
    std::string hd_path(int id) const;
    // Whether the last frame read a CD, floppy or hard disk sector (Tsugaru's NotifyDiskRead), which
    // the host runs flat out (Kairo#82). CD audio plays without reading data and does not count.
    bool loading() const;
    // Whether the guest has the TBIOS mouse driver running (MOS_start to MOS_end), as Towns OS
    // programs and most mouse games do. Its reads go past any port a pad uses too, so this, not the
    // reads, says the guest wants the mouse.
    bool mouse_driver_running() const;

private:
    struct Impl;
    bool run_frame_body(std::string &error);
    // Runs one frame of guest time; [ran] is how much ran.
    bool run_guest(uint64_t &ran, std::string &error);
    explicit TownsMachine(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace kairotowns
