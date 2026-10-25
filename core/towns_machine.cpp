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
#include "wait_loop.h"

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
    TownsRender render;
    TownsRender::ImageCopy image;
    std::string cd;
    uint64_t last_frame = kFrameNanoseconds;
    TownsMachine::Profile profile;
    bool sample_pcs = false;
    bool skip_wait_loops = true;
    WaitLoopSkip<Towns> wait_loop;
    // Clocks the CPU has run, for the wait-loop skip.
    uint64_t clocks = 0;

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
        render.Prepare(t.crtc);
        render.damperWireLine = t.var.damperWireLine;
        render.scanLineEffectIn15KHz = t.var.scanLineEffectIn15KHz;
        render.BuildImage(t.physMem.state.VRAM, t.crtc.GetPalette(), t.crtc.chaseHQPalette);
        render.SwapImage(image);
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
    impl->cd = config.cd;
    impl->skip_wait_loops = config.skip_wait_loops;
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
    ++impl_->profile.frames;
    impl_->profile.run_ns += elapsed_ns(start);
    return ran;
}

const TownsMachine::Profile &TownsMachine::profile() const {
    impl_->profile.skipped_instructions = impl_->wait_loop.counts().instructions;
    impl_->profile.wait_skips = impl_->wait_loop.counts().skips;
    return impl_->profile;
}

void TownsMachine::sample_pcs(bool on) { impl_->sample_pcs = on; }

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
    auto &towns = *impl_->towns;
    // Guest time run this frame, counted step by step: a reset starts Tsugaru's clock over.
    ran = 0;
    while (ran < kFrameNanoseconds) {
        const uint64_t step_start = towns.state.townsTime;
        towns.var.nextTimeSync = towns.state.townsTime + kTimeSync;
        towns.var.justLoadedState = false;
        while (towns.state.townsTime < towns.var.nextTimeSync) {
            // Until the fast device polling only the CPU runs (wait_loop.h).
            impl_->wait_loop.restart();
            while (towns.state.townsTime <= towns.state.nextFastDevicePollingTime && 0 == towns.GetStopFlags()) {
                if (impl_->sample_pcs && impl_->profile.instructions % kPcSampleInterval == 0)
                    ++impl_->profile.pcs[uint64_t(towns.CPU().state.CS().value) << 32 | towns.CPU().state.EIP];
                if (impl_->skip_wait_loops)
                    impl_->wait_loop.before_instruction(towns, impl_->profile.instructions, impl_->clocks,
                                                        towns.state.nextFastDevicePollingTime);
                ++impl_->profile.instructions;
                impl_->clocks += towns.RunOneInstruction();
                towns.pic.ProcessIRQ(towns.CPU(), towns.mem);
            }
            towns.RunScheduledTasks();
            towns.RunFastDevicePolling();
            if (towns.CheckAbort()) {
                error = towns.vmAbortReason.empty() ? "The FM Towns stopped" : towns.vmAbortReason;
                return false;
            }
            impl_->check_render();
        }
        const uint64_t step_end = towns.state.townsTime;
        const uint64_t step = step_end > step_start ? step_end - step_start : 0;
        ran += step;
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
        if (towns.var.powerOff) {
            error = "The FM Towns turned itself off";
            return false;
        }
    }
    return true;
}

double TownsMachine::frame_seconds() const { return impl_->last_frame / 1e9; }

const uint8_t *TownsMachine::frame(uint32_t &width, uint32_t &height) const {
    const auto &image = impl_->image;
    if (image.rgba.empty()) return nullptr;
    width = image.wid;
    height = image.hei;
    return image.rgba.data();
}

uint32_t TownsMachine::read_audio(int16_t *stereo, uint32_t max_frames) { return impl_->speaker.read(stereo, max_frames); }

void TownsMachine::key(int code, bool down) {
    if (code <= 0 || code > 0x7f) return;
    impl_->towns->keyboard.PushFifo(down ? TOWNS_KEYFLAG_JIS_PRESS : TOWNS_KEYFLAG_JIS_RELEASE,
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

void TownsMachine::reset() { impl_->towns->Reset(); }

bool TownsMachine::save_state(const std::string &file) { return impl_->towns->SaveState(file); }

bool TownsMachine::load_state(const std::string &file) {
    if (!impl_->towns->LoadState(file)) return false;
    impl_->speaker.restart();
    return true;
}

bool TownsMachine::insert_cd(const std::string &path) {
    auto &towns = *impl_->towns;
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

std::string TownsMachine::cd_path() const { return impl_->cd; }

std::string TownsMachine::fd_path(int drive) const {
    if (drive < 0 || drive > 1) return "";
    return impl_->towns->fdc.imgFile[drive].fName;
}

} // namespace kairotowns
