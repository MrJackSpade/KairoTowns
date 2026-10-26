// See towns_machine.h. The frame loop follows TownsThread::VMMainLoopTemplate's run mode
// (towns/townsthread.cpp) without its real-time waits and console; the host paces frames.

#include "towns_machine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>

#include "cpputil.h"
#include "outside_world.h"
#include "render.h"
#include "towns.h"
#include "townsdef.h"
#include "townsparam.h"
#include "jit/jit.h"
#include "picture_worker.h"
#include "wait_loop.h"

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace kairotowns {
namespace {

using Towns = FMTownsTemplate<i486DXDefaultFidelity>;

constexpr uint64_t kFrameNanoseconds = PER_SECOND / 60;
constexpr uint64_t kTimeSync = 1000000; // TownsThread::NANOSECONDS_PER_TIME_SYNC

uint64_t frames_to_ns(uint64_t frames) { return frames * PER_SECOND / TownsMachine::kAudioRate; }

uint64_t elapsed_ns(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
}

// The picture is drawn by the core itself, so the window does nothing.
class Window : public Outside_World::WindowInterface {
public:
    void Start() override {}
    void Stop() override {}
    void Interval() override {}
    void Render(bool) override {}
    void UpdateImage(TownsRender::ImageCopy &) override {}
    void Communicate(Outside_World *) override {}
};

// Collects the 44.1 kHz stereo waves Tsugaru mixes. Tsugaru asks for the next 20 ms piece when
// its channel is idle; the channel is busy while the sound produced is ahead of the guest time run
// so far, so the sound advances with the guest instead of with how often it asks. The guest time
// is the speaker's own count, because Tsugaru's clock starts over when the machine resets.
class Speaker : public Outside_World::Sound {
public:
    // The guest ran for nanoseconds more.
    void advance(uint64_t nanoseconds) { guest_time_ += nanoseconds; }

    void Start() override {}
    void Stop() override {}
    void Polling() override {}

    // CD audio is mixed into the FM and PCM waves by TownsCDROM, not played here.
    void CDDAPlay(const DiscImage &, DiscImage::MinSecFrm, DiscImage::MinSecFrm, bool, unsigned int,
                  unsigned int) override {}
    void CDDASetVolume(float, float) override {}
    void CDDAStop() override {}
    void CDDAPause() override {}
    void CDDAResume() override {}
    bool CDDAIsPlaying() override { return false; }
    DiscImage::MinSecFrm CDDACurrentPosition() override { return DiscImage::MinSecFrm(); }

    void FMPCMPlay(std::vector<unsigned char> &wave) override {
        append(wave, fm_end_);
        wave.clear();
    }
    void FMPCMPlayStop() override {}
    bool FMPCMChannelPlaying() override { return produced_time(fm_end_) > guest_time_; }

    void BeepPlay(int, std::vector<unsigned char> &wave) override {
        // The buzzer starts with the FM stream, never behind what has been read.
        beep_end_ = std::max(beep_end_, read_);
        append(wave, beep_end_);
    }
    void BeepPlayStop() override {}
    bool BeepChannelPlaying() const override { return produced_time(beep_end_) > guest_time_; }

    // Fills silence up to the guest's time, so a quiet machine still keeps its sound clock. The sound
    // is never behind by more than the frame that just ran; a larger gap means the guest time count
    // is wrong, and filling it could take all memory, so the machine stops instead.
    bool catch_up(uint64_t frame_ns, std::string &error) {
        const uint64_t target = guest_time_ * TownsMachine::kAudioRate / PER_SECOND;
        if (fm_end_ >= target) return true;
        const uint64_t missing = target - fm_end_;
        if (missing > frame_ns * TownsMachine::kAudioRate / PER_SECOND + 1) {
            error = "The sound fell " + std::to_string(missing) + " frames behind in one frame";
            return false;
        }
        grow(target);
        fm_end_ = target;
        return true;
    }

    uint32_t read(int16_t *stereo, uint32_t max_frames) {
        const uint64_t count = std::min<uint64_t>(max_frames, fm_end_ - read_);
        for (uint64_t i = 0; i < count; ++i) {
            stereo[i * 2] = clamp(mix_.front());
            mix_.pop_front();
            stereo[i * 2 + 1] = clamp(mix_.front());
            mix_.pop_front();
        }
        read_ += count;
        return static_cast<uint32_t>(count);
    }

    // A loaded state moves guest time; the sound starts over from there.
    void restart() {
        mix_.clear();
        guest_time_ = 0;
        read_ = fm_end_ = beep_end_ = 0;
    }

private:
    static int16_t clamp(int32_t value) { return static_cast<int16_t>(std::clamp(value, -32768, 32767)); }

    uint64_t produced_time(uint64_t frames) const { return frames_to_ns(frames); }

    void grow(uint64_t end) {
        while (read_ + mix_.size() / 2 < end) {
            mix_.push_back(0);
            mix_.push_back(0);
        }
    }

    // Adds a little-endian stereo 16-bit wave at cursor, mixing it with what is there.
    void append(const std::vector<unsigned char> &wave, uint64_t &cursor) {
        const uint64_t frames = wave.size() / 4;
        grow(cursor + frames);
        for (uint64_t i = 0; i < frames; ++i) {
            const size_t at = static_cast<size_t>((cursor + i - read_) * 2);
            mix_[at] += cpputil::GetSignedWord(wave.data() + i * 4);
            mix_[at + 1] += cpputil::GetSignedWord(wave.data() + i * 4 + 2);
        }
        cursor += frames;
    }

    uint64_t guest_time_ = 0;
    // Frames since the sound started: read so far, and the ends of the FM stream and of the buzzer.
    uint64_t read_ = 0, fm_end_ = 0, beep_end_ = 0;
    // Unread frames from read_ on, interleaved left and right.
    std::deque<int32_t> mix_;
};

class World : public Outside_World {
public:
    std::string GetProgramResourceDirectory() const override { return ""; }
    void Start() override {}
    void Stop() override {}
    // Input reaches the machine from TownsMachine directly.
    void DevicePolling(FMTownsCommon &) override {}
    bool ImageNeedsFlip() override { return false; }
    void SetKeyboardLayout(unsigned int) override {}
    WindowInterface *CreateWindowInterface() const override { return nullptr; }
    void DeleteWindowInterface(WindowInterface *) const override {}
    Sound *CreateSound() const override { return nullptr; }
    void DeleteSound(Sound *) const override {}
};

} // namespace

struct TownsMachine::Impl {
    std::unique_ptr<Towns> towns = std::make_unique<Towns>();
    World world;
    Window window;
    Speaker speaker;
    PictureWorker pictures;
    std::string cd;
    // The CD that goes in once a hard disk boot has read its first disk (Config::boot_hd).
    std::string cd_after_boot;
    // The guest's calls of DOS's line input when a key last went down (TownsMachine::prompt).
    uint64_t line_inputs_at_key = 0;
    // The modifier keys held, which every key's code carries (TownsMachine::key).
    bool shift_held = false, ctrl_held = false;
    uint64_t last_frame = kFrameNanoseconds;
    TownsMachine::Profile profile;
    bool sample_pcs = false;
    bool count_ops = false;
    bool skip_wait_loops = true;
    WaitLoopSkip<Towns> wait_loop;
    // Clocks the CPU has run, for the wait-loop skip.
    uint64_t clocks = 0;
    // Translated code, when the host has the JIT and the config wants it; and whether the
    // interpreter runs the next instruction (translated code stopped before it).
    std::unique_ptr<jit::Jit> jit;
    bool interpret_next = false;
    // The next fast device polling as a run of translated code started (jit::Host::must_stop).
    long long run_polling = 0;

    // Guest time on by clocks of CPU time, as FMTownsTemplate::RunOneInstruction counts each
    // instruction: the remainder carries, so adding a run's clocks at once, or in parts, gives
    // the same time.
    void advance(uint64_t clocks) {
        auto &t = *towns;
        t.state.clockBalance += clocks * 1000;
        const auto frequency = t.state.currentFreq;
        t.state.townsTime += t.state.clockBalance / frequency;
        t.state.clockBalance %= frequency;
    }
    static void host_advance(void *owner, uint32_t clocks) { static_cast<Impl *>(owner)->advance(clocks); }
    static bool host_must_stop(void *owner) {
        auto &t = *static_cast<Impl *>(owner)->towns;
        return (t.pic.state.IRR0_OR_IRR1_Cache != 0 && t.CPU().GetIF()) || t.GetStopFlags() != 0 ||
               t.state.nextFastDevicePollingTime != static_cast<Impl *>(owner)->run_polling;
    }

    // Where the machine's loop is when a run of translated code that did the polling itself
    // ends (host_poll): past the polling, or before the next instruction.
    enum class Polled { No, PastPolling, AtInstruction };
    Polled polled = Polled::No;

    // The most clocks translated code may run for from now: the interpreter's loop starts an
    // instruction while guest time has not passed the next fast device polling; after c more
    // clocks the time is townsTime + (clockBalance + 1000c) / FREQ, so c may be at most
    // ((polling - townsTime + 1) * FREQ - clockBalance - 1) / 1000. -1 where the interpreter runs
    // the next instruction: an interrupt the PIC would fire after it is the interpreter's.
    int32_t translated_room() {
        auto &t = *towns;
        if (t.pic.state.IRR0_OR_IRR1_Cache != 0 && t.CPU().GetIF()) return -1;
        const int64_t room = (int64_t(t.state.nextFastDevicePollingTime - t.state.townsTime) + 1) *
                                 int64_t(t.state.currentFreq) - int64_t(t.state.clockBalance) - 1;
        if (room < 0) return -1;
        run_polling = t.state.nextFastDevicePollingTime;
        return int32_t(std::min<int64_t>(room / 1000, 1 << 20));
    }
    // What the interpreter's loop does after an instruction, for the last of a run of translated
    // code.
    void end_run(uint32_t last_eip, bool keep_hold_irq) {
        auto &t = *towns;
        auto &cpu = t.CPU();
        // Each instruction the interpreter runs clears it first; the last may have set it again.
        if (!keep_hold_irq) cpu.state.holdIRQ = false;
        wait_loop.ran_to(cpu.state.CS().value, last_eip);
        process_irq();
    }
    // Takes an interrupt the PIC fires, as the interpreter's loop does after each instruction.
    void process_irq() {
        auto &t = *towns;
        // It takes one only where TownsPIC::ProcessIRQ would.
        auto &cpu = t.CPU();
        if (!jit || t.pic.state.IRR0_OR_IRR1_Cache == 0 || !cpu.GetIF() || cpu.state.holdIRQ)
            return t.pic.ProcessIRQ(cpu, t.mem);
        const auto mark = jit->mark();
        t.pic.ProcessIRQ(t.CPU(), t.mem);
        jit->after(mark);
    }
    // Runs the instruction at CS:EIP in the interpreter.
    uint32_t run_one_instruction() {
        auto &t = *towns;
        if (!jit) return t.RunOneInstruction();
        const auto mark = jit->mark();
        const uint32_t ran = t.RunOneInstruction();
        jit->after(mark);
        return ran;
    }
    // The fast device polling, when the CPU has run to it.
    void fast_polling() {
        auto &t = *towns;
        if (!jit) {
            t.RunScheduledTasks();
            t.RunFastDevicePolling();
            return;
        }
        const auto mark = jit->mark();
        t.RunScheduledTasks();
        t.RunFastDevicePolling();
        jit->after(mark);
    }
    // Whether every instruction is the interpreter's, with nothing counted, sampled or traced
    // around it: then interpret_to_polling runs them.
    bool plain_interpreter() const { return !jit && !sample_pcs && !count_ops && !interpreted_trace; }

    // run_step's loop up to the fast device polling where plain_interpreter() holds: the same
    // steps for each instruction (the wait-loop check, the instruction, ProcessIRQ), with what
    // they read held in locals instead of read through the machine for every instruction.
    void interpret_to_polling() {
        auto &t = *towns;
        auto &cpu = t.CPU();
        auto &state = t.state;
        auto &pic = t.pic;
        auto &mem = t.mem;
        const bool skip = skip_wait_loops;
        uint64_t instructions = profile.instructions, ran = clocks;
        while (state.townsTime <= state.nextFastDevicePollingTime && 0 == t.GetStopFlags()) {
            if (skip) wait_loop.before_instruction(t, instructions, ran, state.nextFastDevicePollingTime);
            ++instructions;
            ran += t.RunOneInstruction();
            pic.ProcessIRQ(cpu, mem);
        }
        profile.instructions = instructions;
        clocks = ran;
    }

    // Whether the machine's loop starts another instruction before the polling.
    bool cpu_runs() const {
        const auto &t = *towns;
        return t.state.townsTime <= t.state.nextFastDevicePollingTime && 0 == t.GetStopFlags();
    }
    // Before the loop starts an instruction, after `instructions` and `clocks` more than counted.
    void before_instruction(uint64_t instructions, uint64_t clocks) {
        auto &t = *towns;
        if (sample_pcs && profile.instructions % kPcSampleInterval == 0)
            ++profile.pcs[uint64_t(t.CPU().state.CS().value) << 32 | t.CPU().state.EIP];
        if (skip_wait_loops)
            wait_loop.before_instruction(t, profile.instructions + instructions, clocks + this->clocks,
                                         t.state.nextFastDevicePollingTime);
    }

    // The run of translated code reached the polling having run instructions and clocks, the last
    // at last_eip: what the machine's loop does from there to its next instruction (run_step).
    // Returns the clocks the run may go on for, or -1 where it ends.
    static int32_t host_poll(void *owner, uint32_t instructions, uint32_t clocks, uint32_t last_eip,
                             bool keep_hold_irq) {
        auto &impl = *static_cast<Impl *>(owner);
        auto &t = *impl.towns;
        impl.end_run(last_eip, keep_hold_irq);
        impl.polled = Polled::PastPolling;
        impl.fast_polling();
        if (t.CheckAbort()) return -1;
        impl.check_render();
        if (t.state.townsTime >= t.var.nextTimeSync) return -1;
        if (t.pic.state.IRR0_OR_IRR1_Cache != 0) impl.wait_loop.restart();
        if (!impl.cpu_runs()) return -1;
        impl.polled = Polled::AtInstruction;
        impl.before_instruction(instructions, clocks);
        return impl.translated_room();
    }
    // The wait-loop skip moved guest time at a jump back to the instruction at the CPU's EIP: the
    // run ends there as the machine's loop would end it, and goes on for the clocks returned, or
    // -1 where it ends.
    static int32_t host_go_on(void *owner, bool keep_hold_irq) {
        auto &impl = *static_cast<Impl *>(owner);
        impl.end_run(impl.towns->CPU().state.EIP, keep_hold_irq);
        impl.polled = Polled::AtInstruction;
        return impl.translated_room();
    }
    // Instructions and clocks before the running run of translated code, for host_jumped_back.
    uint64_t run_instructions = 0, run_clocks = 0;
    static bool host_jumped_back(void *owner, uint16_t cs, uint32_t jump_eip, uint32_t instructions, uint32_t clocks) {
        auto &impl = *static_cast<Impl *>(owner);
        if (!impl.skip_wait_loops) return false;
        auto &t = *impl.towns;
        const auto time = t.state.townsTime;
        const auto balance = t.state.clockBalance;
        impl.wait_loop.ran_to(cs, jump_eip);
        impl.wait_loop.before_instruction(t, impl.run_instructions + instructions, impl.run_clocks + clocks,
                                          t.state.nextFastDevicePollingTime);
        return t.state.townsTime != time || t.state.clockBalance != balance;
    }
    // Whether a read of [bytes] bytes at [port] has no effect on any device, and until when it
    // returns what it returns now (WaitLoopSkip::read_holds_until), for InOut::kairoPureRead.
    // Only ports whose reads were checked in Tsugaru's own code are pure (KairoTowns#36); a word or
    // dword read is the device's byte reads (Device::IOReadWord), each of which must be pure. What a
    // pure read returns may change only with guest time, which [until] says, or with an Out or the
    // app between steps (which restart the skip): never in a device's scheduled task or the fast
    // device polling, which a skip goes on across.
    static bool pure_read(void *owner, unsigned int port, unsigned int bytes) {
        auto &impl = *static_cast<Impl *>(owner);
        auto &t = *impl.towns;
        const Device *device = t.io.KairoDevice(port);
        uint64_t until = ~uint64_t(0);
        for (unsigned int i = 0; i < bytes; ++i)
            if (!impl.pure_byte(device, (port + i) & 0xFFFF, until)) return false;
        impl.wait_loop.read_holds_until(until);
        return true;
    }
    bool pure_byte(const Device *device, unsigned int port, uint64_t &until) {
        auto &t = *towns;
        const uint64_t now = t.state.townsTime;
        if (device == &t.crtc) {
            switch (port) {
            case TOWNSIO_CRTC_DATA_LOW:  // crtcReg[latch]: changes only with an Out
                return true;
            case TOWNSIO_CRTC_DATA_HIGH:  // the same, or with FR latched the sync bits as FDA0
                if (t.crtc.state.crtcAddrLatch == TownsCRTC::REG_FR) until = std::min(until, next_sync_change(now));
                return true;
            case TOWNSIO_HSYNC_VSYNC:  // InVSYNC and InHSYNC of guest time
                until = std::min(until, next_sync_change(now));
                return true;
            case TOWNSIO_HSYNC_VSYNC + 1:  // no port: the CRTC's 0xFF
                return true;
            }
            return false;
        }
        if (device == &t.fdc) {
            switch (port) {
            case TOWNSIO_FDC_DRIVE_STATUS_CONTROL: {
                // The drive's flags; DriveReady, asked while the motor runs, counts a pretended
                // not-ready read down (an effect) and is not ready until a time.
                const auto &state = t.fdc.state;
                const unsigned int drive = t.fdc.DriveSelect();
                const auto &d = state.drive[drive];
                if (d.motor && state.driveSelectBit != 0 && t.fdc.DiskLoaded(drive)) {
                    if (d.pretendDriveNotReadyCount > 0) return false;
                    if (now < d.pretendDriveNotReadyUntil) until = std::min<uint64_t>(until, d.pretendDriveNotReadyUntil);
                }
                return true;
            }
            case TOWNSIO_FDC_DRIVE_STATUS_CONTROL + 1:  // no port: the FDC's 0xFF
                return true;
            }
            return false;
        }
        return false;
    }
    // The first guest time after [time] at which InVSYNC or InHSYNC differs from what it is at
    // [time]. They are functions of the time within the frame (VSYNC_CYCLE) and line
    // (HSYNC_CYCLE); each candidate below is a place one of them may change, checked with the
    // functions themselves.
    uint64_t next_sync_change(uint64_t time) const {
        const auto &crtc = towns->crtc;
        auto sync = [&](uint64_t at) { return (crtc.InVSYNC(at) ? 1 : 0) | (crtc.InHSYNC(at) ? 2 : 0); };
        const int now = sync(time);
        const uint64_t frame = TownsCRTC::VSYNC_CYCLE, line = TownsCRTC::HSYNC_CYCLE;
        const uint64_t drawing = TownsCRTC::CRT_VERTICAL_DURATION, shown = TownsCRTC::CRT_HORIZONTAL_DURATION;
        uint64_t at = time;
        for (;;) {
            const uint64_t into = at % frame, start = at - into;
            const uint64_t row = into / line * line;
            uint64_t next = frame;
            for (uint64_t candidate : {row + shown + 1, row + line, drawing, drawing + 1})
                if (candidate > into && candidate < next) next = candidate;
            at = start + next;
            if (sync(at) != now) return at;
        }
    }

    jit::Host jit_host() {
        jit::Host host;
        host.owner = this;
        host.advance = &host_advance;
        host.must_stop = &host_must_stop;
        host.jumped_back = &host_jumped_back;
        host.poll = &host_poll;
        host.go_on = &host_go_on;
        return host;
    }
    // Traces for test tools (TownsMachine::trace_translated, trace_interpreted).
    std::vector<TownsMachine::CpuSnapshot> *translated_trace = nullptr, *interpreted_trace = nullptr;
    const std::vector<uint64_t> *trace_counts = nullptr;
    size_t trace_next = 0;

    TownsMachine::CpuSnapshot snapshot() const {
        const auto &t = *towns;
        const auto &st = t.CPU().state;
        TownsMachine::CpuSnapshot s;
        s.instructions = profile.instructions;
        for (int i = 0; i < 8; ++i) s.regs[i] = st.NULL_and_reg32[1 + i];
        s.eip = st.EIP;
        s.eflags = st.EFLAGS;
        s.cs = st.CS().value;
        s.cs_base = st.CS().baseLinearAddr;
        s.cpu_mode = st.mode;
        s.time = t.state.townsTime;
        s.clock_balance = t.state.clockBalance;
        s.side_effects = t.mem.sideEffects;
        return s;
    }

    // What a run of translated code did, for the machine's loop.
    enum class Translated {
        No,           // the interpreter runs the next instruction
        Ran,          // the loop goes on before the next instruction
        PastPolling,  // the run did the fast device polling, and the loop goes on after it
    };
    // Runs translated code from the CPU's CS:EIP when the interpreter's loop would run the same
    // instructions.
    Translated run_translated() {
        auto &t = *towns;
        auto &cpu = t.CPU();
        const int32_t limit = translated_room();
        if (limit < 0) return Translated::No;
        const uint32_t start_eip = cpu.state.EIP;
        polled = Polled::No;
        run_instructions = profile.instructions;
        run_clocks = clocks;
        uint8_t start_code[32] = {};
        if (translated_trace) {
            const uint32_t lin = cpu.state.CS().baseLinearAddr + start_eip;
            if (cpu.state.CSEIPWindow.IsLinearAddressInRange(lin)) {
                const auto code = cpu.state.CSEIPWindow.GetReadAccessPointer(lin);
                std::memcpy(start_code, code.ptr, std::min<size_t>(sizeof start_code, code.length));
            }
        }
        const jit::Jit::Run ran = jit->run(limit);
        interpret_next = ran.interpret_next;
        if (ran.instructions == 0) {
            if (polled != Polled::No) throw std::logic_error("a run of translated code polled without running");
            return Translated::No;
        }
        // I/O and the polling in the run added the clocks before them already.
        advance(ran.clocks - ran.advanced);
        t.var.disassemblePointer.SEG = cpu.state.CS().value;
        t.var.disassemblePointer.OFFSET = cpu.state.EIP;
        profile.instructions += ran.instructions;
        profile.jit_instructions += ran.instructions;
        ++profile.jit_runs;
        clocks += ran.clocks;
        // Ended where it did what follows its last instruction itself (host_poll, host_go_on).
        if (!ran.ended) end_run(ran.last_eip, ran.keep_hold_irq);
        if (translated_trace) {
            translated_trace->push_back(snapshot());
            translated_trace->back().start_eip = start_eip;
            std::memcpy(translated_trace->back().code, start_code, sizeof start_code);
        }
        if (ran.ended && polled == Polled::No) throw std::logic_error("a run of translated code ended without the machine");
        return ran.ended && polled == Polled::PastPolling ? Translated::PastPolling : Translated::Ran;
    }

    unsigned pad = 0;
    bool buttons[2] = {false, false};
    int mouse_dx = 0, mouse_dy = 0;

    // Gives the guest the held pad and mouse, as Outside_World::DevicePolling would.
    void poll_devices() {
        auto &t = *towns;
        t.SetGamePadState(0, pad & PAD_A, pad & PAD_B, pad & PAD_LEFT, pad & PAD_RIGHT, pad & PAD_UP, pad & PAD_DOWN,
                          pad & PAD_RUN, pad & PAD_SELECT, false);
        t.SetMouseButtonState(buttons[0], buttons[1]);
        if (mouse_dx || mouse_dy) {
            const int dx = std::clamp(mouse_dx, -127, 127), dy = std::clamp(mouse_dy, -127, 127);
            t.ControlMouseByDiffDirect(dx, dy);
            mouse_dx -= dx;
            mouse_dy -= dy;
        }
        t.rex3586.Polling();
    }

    // A new picture once a 60 Hz rendering slot has come, outside vertical sync.
    void check_render() {
        auto &t = *towns;
        if (t.state.nextRenderingTime > t.state.townsTime || t.crtc.InVSYNC(t.state.townsTime)) return;
        const auto start = std::chrono::steady_clock::now();
        pictures.start(t.crtc, t.physMem.state.VRAM, sizeof(t.physMem.state.VRAM), t.var.damperWireLine,
                       t.var.scanLineEffectIn15KHz);
        t.state.nextRenderingTime = t.state.townsTime + TOWNS_RENDERING_FREQUENCY;
        ++profile.pictures;
        profile.picture_ns += elapsed_ns(start);
    }

    void save_cmos() {
        auto &t = *towns;
        if (!t.var.CMOSFName.empty())
            cpputil::WriteBinaryFile(t.var.ExpandFileName(t.var.CMOSFName), TOWNS_CMOS_SIZE, t.physMem.state.CMOSRAM);
    }
};

TownsMachine::TownsMachine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::unique_ptr<TownsMachine> TownsMachine::create(const Config &config, std::string &error) {
    auto impl = std::make_unique<Impl>();
    TownsStartParameters parameters;
    parameters.ROMPath = config.rom_dir;
    parameters.CMOSFName = config.cmos_file;
    parameters.cdImgFName = config.cd;
    parameters.fdImgFName[0] = config.fd[0];
    parameters.fdImgFName[1] = config.fd[1];
    static_assert(Config::kHardDisks == TownsStartParameters::MAX_NUM_SCSI_DEVICES, "a hard disk for each SCSI ID");
    for (int id = 0; id < Config::kHardDisks; ++id) {
        if (config.hd[id].empty()) continue;
        parameters.scsiImg[id].imageType = TownsStartParameters::SCSIIMAGE_HARDDISK;
        parameters.scsiImg[id].imgFName = config.hd[id];
    }
    if (config.boot_hd) {
        if (std::none_of(std::begin(config.hd), std::end(config.hd), [](const std::string &hd) { return !hd.empty(); })) {
            error = "The machine has no hard disk to boot";
            return nullptr;
        }
        parameters.cdImgFName.clear();
    }
    parameters.memSizeInMB = config.memory_mb;
    parameters.freq = config.cpu_mhz;
    // Booting until the first disk read: run_frame runs it as fast as it can when skipping the memory check.
    parameters.noWait = config.skip_memory_check;
    parameters.noWaitStandby = false;
    parameters.gamePort[0] = TOWNS_GAMEPORTEMU_PHYSICAL0;
    parameters.gamePort[1] = TOWNS_GAMEPORTEMU_MOUSE;
    parameters.interactive = false;
    // Tsugaru by default darkens the row two thirds down every picture, as the damper wire of a
    // Trinitron monitor shows; on a phone screen it is only a discoloured line across the game.
    parameters.damperWireLine = false;
    auto &towns = *impl->towns;
    if (!FMTownsCommon::Setup(towns, &impl->world, &impl->window, parameters)) {
        error = towns.vmAbortReason.empty() ? "The FM Towns did not start; check its ROM files" : towns.vmAbortReason;
        return nullptr;
    }
    towns.DisableDebugger();
    // Setup only prints when a hard disk image does not open.
    for (int id = 0; id < Config::kHardDisks; ++id) {
        if (!config.hd[id].empty() && towns.scsi.state.dev[id].devType != TownsSCSI::SCSIDEVICE_HARDDISK) {
            error = "The hard disk image " + config.hd[id] + " did not open";
            return nullptr;
        }
    }
    // A hard disk's own MS-DOS boots only when the CMOS's drive table names a SCSI drive (KairoTowns#38),
    // as Towns OS's setup does when a player installs a hard disk: with none, the first free drive from
    // D: becomes SCSI unit 0. The guest's own MS-DOS then names the disk's partitions there itself.
    // Each drive, A: to P:, is two bytes, its type (2 for SCSI, FFh for none) and unit, from
    // TOWNSIO_CMOS_DRIVE_ASSIGN, outside the CMOS checksum's range; the bytes after P: are other settings.
    if (std::any_of(std::begin(config.hd), std::end(config.hd), [](const std::string &hd) { return !hd.empty(); })) {
        unsigned char *drives = towns.physMem.state.CMOSRAM + (TOWNSIO_CMOS_DRIVE_ASSIGN - TOWNSIO_CMOS_BASE) / 2;
        constexpr int kScsi = 2, kFirstDrive = 3, kDrives = 16;
        bool named = false;
        for (int drive = 0; drive < kDrives; ++drive) named = named || drives[drive * 2] == kScsi;
        if (!named) {
            if (config.hd[0].empty()) {
                error = "Only a hard disk at SCSI ID 0 (.h0) can be given a drive in the CMOS";
                return nullptr;
            }
            int drive = kFirstDrive;
            while (drive < kDrives && drives[drive * 2] != 0xFF) ++drive;
            if (drive == kDrives) {
                error = "The CMOS has no free drive for the hard disk " + config.hd[0];
                return nullptr;
            }
            drives[drive * 2] = kScsi;
            drives[drive * 2 + 1] = 0;
        }
    }
    impl->cd = config.cd;
    if (config.boot_hd) impl->cd_after_boot = config.cd;
    impl->skip_wait_loops = config.skip_wait_loops;
    // Reads without effect do not keep a wait loop from being skipped (Impl::pure_read).
    towns.io.kairoPureRead = &Impl::pure_read;
    towns.io.kairoPureOwner = impl.get();
    if (config.jit && jit::supported())
        impl->jit = std::make_unique<jit::Jit>(towns.CPU(), towns.mem, towns.io, &towns.physMem.mainRAMAccess,
                                               towns.physMem.state.RAM, impl->jit_host(),
                                               &towns.pic.state.IRR0_OR_IRR1_Cache);
    towns.cdrom.waitForWave = config.repeatable;
    if (config.repeatable) towns.rtc.fixedTime = 946684800; // 2000-01-01 00:00:00
    // As TownsThread::VMStart and VMMainLoopTemplate prepare the machine.
    if (towns.state.appSpecificSetting == TOWNS_APPSPECIFIC_BRANDISH) towns.state.pretend386DX = true;
    towns.sound.SetOutsideWorld(&impl->speaker);
    towns.sound.SetCDROMPointer(&towns.cdrom);
    towns.sound.SetSCSIPointer(&towns.scsi);
    impl->speaker.restart();
    return std::unique_ptr<TownsMachine>(new TownsMachine(std::move(impl)));
}

TownsMachine::~TownsMachine() {
    auto &towns = *impl_->towns;
    towns.cdrom.WaitUntilAsyncWaveReaderFinished();
    towns.fdc.SaveModifiedDiskImages();
    impl_->save_cmos();
}

bool TownsMachine::run_frame(std::string &error) {
    const auto start = std::chrono::steady_clock::now();
    const bool ran = run_frame_body(error);
    // The frame's picture, drawn beside the emulation since its rendering point (PictureWorker).
    const auto wait_start = std::chrono::steady_clock::now();
    impl_->pictures.wait();
    impl_->profile.picture_ns += elapsed_ns(wait_start);
    ++impl_->profile.frames;
    impl_->profile.run_ns += elapsed_ns(start);
    return ran;
}

const TownsMachine::Profile &TownsMachine::profile() const {
    impl_->profile.skipped_instructions = impl_->wait_loop.counts().instructions;
    impl_->profile.wait_skips = impl_->wait_loop.counts().skips;
    if (impl_->jit) {
        impl_->profile.jit_translations = impl_->jit->stats().translations;
        impl_->profile.jit_refused = impl_->jit->stats().refused;
        impl_->profile.jit_mismatches = impl_->jit->stats().mismatches;
        impl_->profile.jit_links = impl_->jit->stats().links;
        impl_->profile.jit_flushes = impl_->jit->stats().flushes;
    }
    return impl_->profile;
}

void TownsMachine::sample_pcs(bool on) { impl_->sample_pcs = on; }
void TownsMachine::count_ops(bool on) { impl_->count_ops = on; }

namespace {
// Group opcodes, whose ModR/M REG field picks the operation.
bool group_opcode(unsigned op) {
    return (op >= 0x80 && op <= 0x83) || op == 0xC0 || op == 0xC1 || (op >= 0xD0 && op <= 0xD3) || op == 0xF6 ||
           op == 0xF7 || op == 0xFE || op == 0xFF || op == 0xF00 || op == 0xF01 || op == 0xFBA;
}

uint32_t op_key_of(const Towns &towns, const i486DXCommon::InstructionAndOperand &decoded) {
    const auto &cpu = towns.CPU();
    const auto &inst = decoded.inst;
    const unsigned op = inst.RealOpCode();
    uint32_t key = op & 0xFFF;
    if (group_opcode(op)) key |= (1u << 15) | (inst.GetREG() << 12);
    if (decoded.op1.operandType == i486DXCommon::OPER_ADDR || decoded.op2.operandType == i486DXCommon::OPER_ADDR)
        key |= 1u << 16;
    key |= (inst.operandSize == 8 ? 0u : inst.operandSize == 16 ? 1u : 2u) << 17;
    if (inst.addressSize == 32) key |= 1u << 19;
    if (inst.instPrefix == i486DXCommon::INST_PREFIX_REP || inst.instPrefix == i486DXCommon::INST_PREFIX_REPNE)
        key |= 1u << 20;
    const uint32_t mode = cpu.IsInRealMode() ? 0 : (cpu.state.EFLAGS & i486DXCommon::EFLAGS_VIRTUAL86) ? 2 : 1;
    key |= mode << 21;
    if (cpu.PagingEnabled()) key |= 1u << 23;
    return key;
}
} // namespace

std::string TownsMachine::op_key(uint32_t key) {
    char text[96];
    static const char *modes[] = {"real", "prot", "v86", "?"};
    std::snprintf(text, sizeof text, "%03X%s%c %s o%u a%u%s %s%s", key & 0xFFF, (key >> 15) & 1 ? "/" : "",
                  (key >> 15) & 1 ? char('0' + ((key >> 12) & 7)) : ' ', (key >> 16) & 1 ? "mem" : "reg",
                  8u << ((key >> 17) & 3), (key >> 19) & 1 ? 32 : 16, (key >> 20) & 1 ? " rep" : "",
                  modes[(key >> 21) & 3], (key >> 23) & 1 ? " paging" : "");
    return text;
}

std::string TownsMachine::disassemble(uint16_t selector, uint32_t eip, int count) const {
    auto &towns = *impl_->towns;
    i486DXCommon::SegmentRegister cs;
    towns.CPU().DebugLoadSegmentRegisterFromFarPointer(cs, towns.mem, i486DXCommon::FarPointer{selector, eip});
    std::string lines;
    for (int i = 0; i < count; ++i) {
        i486DXCommon::InstructionAndOperand instruction;
        MemoryAccess::ConstMemoryWindow window;
        towns.CPU().DebugFetchInstruction(window, instruction, cs, eip, towns.mem);
        lines += towns.CPU().Disassemble(instruction.inst, instruction.op1, instruction.op2, cs, eip, towns.mem,
                                         towns.debugger.GetSymTable(), towns.debugger.GetIOTable()) + "\n";
        eip += instruction.inst.numBytes;
    }
    return lines;
}

bool TownsMachine::run_frame_body(std::string &error) {
    auto &towns = *impl_->towns;
    uint64_t ran = 0;
    if (!impl_->cd_after_boot.empty() && towns.kairoDiskReads > 0) {
        const std::string cd = impl_->cd_after_boot;
        impl_->cd_after_boot.clear();
        if (!insert_cd(cd)) {
            error = "The CD " + cd + " did not open";
            return false;
        }
    }
    if (!towns.state.noWait) {
        if (!run_guest(ran, error)) return false;
        impl_->last_frame = ran;
        return impl_->speaker.catch_up(ran, error);
    }
    // Skipping the memory check: until its first disk read, the machine runs as fast as this
    // frame's real time allows, as Tsugaru's -NOWAITBOOT does, so the BIOS memory test passes in
    // a moment. Its sound is dropped and the frame counts as one frame, so audio stays in step.
    const auto start = std::chrono::steady_clock::now();
    do {
        if (!run_guest(ran, error)) return false;
    } while (towns.state.noWait &&
             std::chrono::steady_clock::now() - start < std::chrono::nanoseconds(kFrameNanoseconds));
    impl_->speaker.restart();
    impl_->speaker.advance(kFrameNanoseconds);
    impl_->last_frame = kFrameNanoseconds;
    return impl_->speaker.catch_up(kFrameNanoseconds, error);
}

bool TownsMachine::run_guest(uint64_t &ran, std::string &error) {
    // Guest time run this frame, counted step by step: a reset starts Tsugaru's clock over.
    ran = 0;
    while (ran < kFrameNanoseconds) {
        uint64_t step = 0;
        if (!run_step(step, error)) return false;
        ran += step;
    }
    return true;
}

bool TownsMachine::run_step(uint64_t &ran, std::string &error) {
    auto &towns = *impl_->towns;
    {
        const uint64_t step_start = towns.state.townsTime;
        towns.var.nextTimeSync = towns.state.townsTime + kTimeSync;
        towns.var.justLoadedState = false;
        // Sound, input and the app's own hooks between steps can touch anything (wait_loop.h).
        impl_->wait_loop.restart();
        while (towns.state.townsTime < towns.var.nextTimeSync) {
            // Until the fast device polling only the CPU runs; after a polling, a skip goes on unless
            // an interrupt request is waiting (wait_loop.h), as ProcessIRQ would then act.
            if (towns.pic.state.IRR0_OR_IRR1_Cache != 0) impl_->wait_loop.restart();
            // Translated code may do the polling itself (Impl::host_poll).
            bool polled = false;
            if (impl_->plain_interpreter()) impl_->interpret_to_polling();
            else while (impl_->cpu_runs()) {
                impl_->before_instruction(0, 0);
                if (impl_->jit && !impl_->interpret_next) {
                    const auto ran = impl_->run_translated();
                    if (ran == Impl::Translated::PastPolling) {
                        polled = true;
                        break;
                    }
                    if (ran == Impl::Translated::Ran) continue;
                }
                impl_->interpret_next = false;
                ++impl_->profile.instructions;
                if (impl_->count_ops) {
                    i486DXCommon::InstructionAndOperand decoded;
                    MemoryAccess::ConstMemoryWindow window;
                    towns.CPU().DebugFetchInstruction(window, decoded, towns.mem);
                    const uint32_t key = op_key_of(towns, decoded);
                    ++impl_->profile.ops[key];
                    const uint32_t next = towns.CPU().state.EIP + decoded.inst.numBytes;
#if defined(__x86_64__)
                    const uint64_t ticks = __rdtsc();
#endif
                    impl_->clocks += impl_->run_one_instruction();
                    const bool transfer = towns.CPU().state.EIP != next;
                    impl_->process_irq();
#if defined(__x86_64__)
                    impl_->profile.op_ticks[key] += __rdtsc() - ticks;
#endif
                    if (transfer) ++impl_->profile.transfers;
                    continue;
                }
                impl_->clocks += impl_->run_one_instruction();
                impl_->process_irq();
                if (impl_->interpreted_trace) {
                    const auto &counts = *impl_->trace_counts;
                    while (impl_->trace_next < counts.size() && counts[impl_->trace_next] < impl_->profile.instructions)
                        ++impl_->trace_next;
                    if (impl_->trace_next < counts.size() && counts[impl_->trace_next] == impl_->profile.instructions)
                        impl_->interpreted_trace->push_back(impl_->snapshot());
                }
            }
            if (!polled) impl_->fast_polling();
            if (towns.CheckAbort()) {
                error = towns.vmAbortReason.empty() ? "The FM Towns stopped" : towns.vmAbortReason;
                return false;
            }
            if (!polled) impl_->check_render();
        }
        const uint64_t step_end = towns.state.townsTime;
        const uint64_t step = step_end > step_start ? step_end - step_start : 0;
        ran = step;
        // What sound, the app's own hooks and input write, translated code's checks watch.
        const jit::Jit::Mark between = impl_->jit ? impl_->jit->mark() : jit::Jit::Mark();
        impl_->speaker.advance(step);
        const auto sound_start = std::chrono::steady_clock::now();
        towns.ProcessSound(&impl_->world);
        towns.cdrom.UpdateCDDAState(towns.state.townsTime);
        impl_->profile.sound_ns += elapsed_ns(sound_start);
        impl_->world.ProcessAppSpecific(towns);
        if (towns.state.nextDevicePollingTime < towns.state.townsTime) {
            impl_->poll_devices();
            towns.state.nextDevicePollingTime = towns.state.townsTime + FMTownsCommon::DEVICE_POLLING_INTERVAL;
        }
        if (towns.state.nextSecondInTownsTime <= towns.state.townsTime) {
            towns.state.nextSecondInTownsTime += PER_SECOND;
            towns.fdc.SaveModifiedDiskImages();
            towns.physMem.state.memCard.SaveRawImageIfModified();
        }
        if (impl_->jit) impl_->jit->after(between);
        if (towns.var.powerOff) {
            error = "The FM Towns turned itself off";
            return false;
        }
    }
    return true;
}

bool TownsMachine::CpuSnapshot::operator==(const CpuSnapshot &o) const {
    // What the guest can tell: the instruction count is not, as the wait-loop skip (wait_loop.h)
    // counts the passes it skips apart, and may skip a pass sooner in one machine than another.
    return std::equal(regs, regs + 8, o.regs) && eip == o.eip && eflags == o.eflags && cs == o.cs && time == o.time &&
           clock_balance == o.clock_balance && side_effects == o.side_effects;
}

TownsMachine::CpuSnapshot TownsMachine::snapshot() const { return impl_->snapshot(); }

void TownsMachine::trace_translated(std::vector<CpuSnapshot> *out) { impl_->translated_trace = out; }

void TownsMachine::trace_interpreted(const std::vector<uint64_t> *counts, std::vector<CpuSnapshot> *out) {
    impl_->trace_counts = counts;
    impl_->interpreted_trace = out;
    impl_->trace_next = 0;
}

const std::vector<unsigned char> &TownsMachine::ram() const { return impl_->towns->physMem.state.RAM; }

double TownsMachine::frame_seconds() const { return impl_->last_frame / 1e9; }

const uint8_t *TownsMachine::frame(uint32_t &width, uint32_t &height) const {
    const auto &image = impl_->pictures.image();
    if (image.rgba.empty()) return nullptr;
    width = image.wid;
    height = image.hei;
    return image.rgba.data();
}

uint32_t TownsMachine::read_audio(int16_t *stereo, uint32_t max_frames) { return impl_->speaker.read(stereo, max_frames); }

void TownsMachine::key(int code, bool down) {
    if (code <= 0 || code > 0x7f) return;
    if (down) impl_->line_inputs_at_key = impl_->towns->CPU().kairoLineInputs;
    // The keyboard sends the held Shift and Ctrl with every key, as Tsugaru's own frontend does;
    // the BIOS reads them from there, not from the Shift and Ctrl keys' own codes.
    if (code == TOWNS_JISKEY_SHIFT) impl_->shift_held = down;
    if (code == TOWNS_JISKEY_CTRL) impl_->ctrl_held = down;
    const unsigned char flags = (impl_->shift_held ? TOWNS_KEYFLAG_SHIFT : 0) | (impl_->ctrl_held ? TOWNS_KEYFLAG_CTRL : 0);
    impl_->towns->keyboard.PushFifo(flags | (down ? TOWNS_KEYFLAG_JIS_PRESS : TOWNS_KEYFLAG_JIS_RELEASE),
                                    static_cast<unsigned char>(code));
}

void TownsMachine::pad(unsigned buttons) { impl_->pad = buttons; }

void TownsMachine::mouse_move(int dx, int dy) {
    impl_->mouse_dx += dx;
    impl_->mouse_dy += dy;
}

void TownsMachine::mouse_button(int button, bool down) {
    if (button == 1 || button == 2) impl_->buttons[button - 1] = down;
}

void TownsMachine::reset() {
    impl_->towns->Reset();
    if (impl_->jit) impl_->jit->changed();
}

bool TownsMachine::save_state(const std::string &file) { return impl_->towns->SaveState(file); }

bool TownsMachine::load_state(const std::string &file) {
    if (!impl_->towns->LoadState(file)) return false;
    if (impl_->jit) impl_->jit->changed();
    impl_->speaker.restart();
    return true;
}

bool TownsMachine::insert_cd(const std::string &path) {
    auto &towns = *impl_->towns;
    impl_->cd_after_boot.clear();
    if (path.empty()) {
        towns.cdrom.state.GetDisc().CleanUp();
        impl_->cd.clear();
        return true;
    }
    if (towns.cdrom.LoadDiscImage(path) != DiscImage::ERROR_NOERROR) return false;
    impl_->cd = path;
    return true;
}

bool TownsMachine::insert_fd(int drive, const std::string &path) {
    auto &towns = *impl_->towns;
    if (drive < 0 || drive > 1) return false;
    towns.fdc.SaveModifiedDiskImages();
    if (path.empty()) {
        towns.fdc.Eject(drive);
        towns.fdc.imgFile[drive].fName.clear();
        return true;
    }
    return towns.fdc.LoadD77orRDDorRAW(drive, path.c_str(), towns.state.townsTime, false);
}

bool TownsMachine::insert_hd(int id, const std::string &path) {
    auto &towns = *impl_->towns;
    if (id < 0 || id >= Config::kHardDisks) return false;
    auto &device = towns.scsi.state.dev[id];
    if (device.devType == TownsSCSI::SCSIDEVICE_CDROM) return false;
    if (path.empty()) {
        device.devType = TownsSCSI::SCSIDEVICE_NONE;
        device.imageFName.clear();
        device.imageSize = 0;
        return true;
    }
    return towns.scsi.LoadHardDiskImage(id, path);
}

kairo::Prompt TownsMachine::prompt() const {
    return impl_->towns->CPU().kairoLineInputs > impl_->line_inputs_at_key ? kairo::Prompt::Line : kairo::Prompt::None;
}

bool TownsMachine::typed_key(char c, int &code, bool &shift) {
    unsigned char codes[2];
    if (TownsKeyboard::TranslateChar(codes, static_cast<unsigned char>(c)) == 0) return false;
    // Ctrl characters are not typed: a line's {CTRL-x} holds Ctrl.
    if (codes[0] & TOWNS_KEYFLAG_CTRL) return false;
    code = codes[1];
    shift = (codes[0] & TOWNS_KEYFLAG_SHIFT) != 0;
    return true;
}

int TownsMachine::special_key(kairo::SpecialKey key) {
    switch (key) {
    case kairo::SpecialKey::Return: return TOWNS_JISKEY_RETURN;
    case kairo::SpecialKey::Shift: return TOWNS_JISKEY_SHIFT;
    case kairo::SpecialKey::Ctrl: return TOWNS_JISKEY_CTRL;
    case kairo::SpecialKey::F1: return TOWNS_JISKEY_PF01;
    case kairo::SpecialKey::F2: return TOWNS_JISKEY_PF02;
    case kairo::SpecialKey::F3: return TOWNS_JISKEY_PF03;
    case kairo::SpecialKey::F4: return TOWNS_JISKEY_PF04;
    case kairo::SpecialKey::F5: return TOWNS_JISKEY_PF05;
    }
    return -1;
}

std::string TownsMachine::cd_path() const { return impl_->cd; }

std::string TownsMachine::hd_path(int id) const {
    if (id < 0 || id >= Config::kHardDisks) return "";
    const auto &device = impl_->towns->scsi.state.dev[id];
    return device.devType == TownsSCSI::SCSIDEVICE_HARDDISK ? device.imageFName : "";
}

std::string TownsMachine::fd_path(int drive) const {
    if (drive < 0 || drive > 1) return "";
    return impl_->towns->fdc.imgFile[drive].fName;
}

} // namespace kairotowns
