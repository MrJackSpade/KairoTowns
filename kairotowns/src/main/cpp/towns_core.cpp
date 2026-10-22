// The FM Towns machine (core/towns_machine.h) as the MachineCore the shared frame host runs
// (shared/frontend/src/main/cpp/kairo/machine_core.h).

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "kairo/machine_core.h"
#include "towns_machine.h"

namespace {

using kairotowns::TownsMachine;

// Media slots, as MainActivity numbers them.
constexpr int kCd = 0;
constexpr int kFloppyA = 1;
constexpr int kFloppyB = 2;
constexpr const char *kStateFile = "/state.tss";
// MainActivity.stateError reads this.
constexpr int kStateFailed = 2;

class TownsCore : public kairo::MachineCore {
public:
    explicit TownsCore(std::unique_ptr<TownsMachine> machine) : machine_(std::move(machine)) {}

    bool run_frame() override { return machine_->run_frame(error_); }
    std::string error() override { return error_; }

    const uint8_t *frame(uint32_t &width, uint32_t &height) override {
        const uint8_t *pixels = machine_->frame(width, height);
        if (pixels) return pixels;
        // Black until the machine draws its first picture.
        width = 640;
        height = 480;
        blank_.resize(width * height * 4);
        for (size_t i = 3; i < blank_.size(); i += 4) blank_[i] = 0xff;
        return blank_.data();
    }

    double frame_seconds() override { return machine_->frame_seconds(); }
    int32_t audio_rate() override { return TownsMachine::kAudioRate; }
    uint32_t read_audio(int16_t *stereo, uint32_t max_frames) override { return machine_->read_audio(stereo, max_frames); }

    void release_inputs() override {
        for (int code = 0; code < static_cast<int>(keys_.size()); ++code)
            if (keys_[code]) key(code, false);
        pad_ = 0;
        machine_->pad(0);
        machine_->mouse_button(1, false);
        machine_->mouse_button(2, false);
    }

    void key(int code, bool down) override {
        if (code <= 0 || code >= static_cast<int>(keys_.size()) || keys_[code] == down) return;
        keys_[code] = down;
        machine_->key(code, down);
    }

    // Controls in TownsKeys.joystick order, which is the order of the core's pad bits.
    void joystick(int control, bool down) override {
        if (control < 0 || control > 7) return;
        pad_ = down ? pad_ | (1u << control) : pad_ & ~(1u << control);
        machine_->pad(pad_);
    }

    void mouse_move(int dx, int dy) override { machine_->mouse_move(dx, dy); }
    void mouse_button(int button, bool down) override { machine_->mouse_button(button, down); }

    void reset() override { machine_->reset(); }

    int save_state(const std::string &directory) override {
        return machine_->save_state(directory + kStateFile) ? 0 : kStateFailed;
    }

    int load_state(const std::string &directory) override {
        return machine_->load_state(directory + kStateFile) ? 0 : kStateFailed;
    }

    int media(int slot, const std::string &path) override {
        switch (slot) {
        case kCd: return machine_->insert_cd(path) ? 0 : kStateFailed;
        case kFloppyA: return machine_->insert_fd(0, path) ? 0 : kStateFailed;
        case kFloppyB: return machine_->insert_fd(1, path) ? 0 : kStateFailed;
        default: return kUnsupported;
        }
    }

    std::vector<std::string> media_paths() override {
        return {machine_->cd_path(), machine_->fd_path(0), machine_->fd_path(1)};
    }

private:
    std::unique_ptr<TownsMachine> machine_;
    std::string error_;
    std::vector<uint8_t> blank_;
    std::array<bool, 128> keys_{};
    unsigned pad_ = 0;
};

} // namespace

std::unique_ptr<kairo::MachineCore> kairo::create_core(const kairo::Launch &launch, std::string &error) {
    kairotowns::Config config;
    config.rom_dir = launch.firmware;
    config.cmos_file = launch.home + "/cmos.bin";
    config.cd = launch.media.size() > kCd ? launch.media[kCd] : "";
    config.fd[0] = launch.media.size() > kFloppyA ? launch.media[kFloppyA] : "";
    config.fd[1] = launch.media.size() > kFloppyB ? launch.media[kFloppyB] : "";
    // The settings TownsCatalog's options name; every launch passes both.
    config.memory_mb = static_cast<unsigned>(std::stoul(launch.settings.at("memory")));
    config.cpu_mhz = static_cast<unsigned>(std::stoul(launch.settings.at("cpu")));
    auto machine = TownsMachine::create(config, error);
    if (!machine) return nullptr;
    return std::make_unique<TownsCore>(std::move(machine));
}
