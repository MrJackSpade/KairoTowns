// The FM Towns machine (core/towns_machine.h) as the MachineCore the shared frame host runs
// (shared/frontend/src/main/cpp/kairo/machine_core.h).

#include <array>
#include <fstream>
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
constexpr int kHardDisk = 3;

// A hard disk image's SCSI ID, from its extension (.h0 to .h6); -1 for any other name.
int hard_disk_id(const std::string &path) {
    const size_t size = path.size();
    if (size < 3 || path[size - 3] != '.' || (path[size - 2] != 'h' && path[size - 2] != 'H')) return -1;
    const int id = path[size - 1] - '0';
    return id >= 0 && id < kairotowns::Config::kHardDisks ? id : -1;
}
constexpr const char *kStateFile = "/state.tss";
// MainActivity.stateError reads this.

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
    void mouse_warp(int x, int y) override { machine_->mouse_warp(x, y); }
    bool mouse_warp_done() override { return machine_->mouse_warp_done(); }
    void mouse_button(int button, bool down) override { machine_->mouse_button(button, down); }

    void reset() override { machine_->reset(); }

    int save_state(const std::string &directory) override {
        return machine_->save_state(directory + kStateFile) ? 0 : kFailed;
    }

    int load_state(const std::string &directory) override {
        return machine_->load_state(directory + kStateFile) ? 0 : kFailed;
    }

    int media(int slot, const std::string &path) override {
        switch (slot) {
        case kCd: return machine_->insert_cd(path) ? 0 : kFailed;
        case kFloppyA: return machine_->insert_fd(0, path) ? 0 : kFailed;
        case kFloppyB: return machine_->insert_fd(1, path) ? 0 : kFailed;
        case kHardDisk: {
            // The slot holds one hard disk, at the SCSI ID its name gives.
            const int id = path.empty() ? -1 : hard_disk_id(path);
            if (!path.empty() && id < 0) return kFailed;
            const int current = attached_hard_disk();
            if (current >= 0 && !machine_->insert_hd(current, "")) return kFailed;
            return path.empty() || machine_->insert_hd(id, path) ? 0 : kFailed;
        }
        default: return kUnsupported;
        }
    }

    std::vector<std::string> media_paths() override {
        const int hard_disk = attached_hard_disk();
        return {machine_->cd_path(), machine_->fd_path(0), machine_->fd_path(1),
                hard_disk < 0 ? std::string() : machine_->hd_path(hard_disk)};
    }

    // A game's startup commands go in at DOS's line input (TownsMachine::prompt).
    // Flat out while the guest reads a disk (Kairo#82).
    bool loading() override { return machine_->loading(); }
    // Auto touch mode (Kairo#78): the guest running the TBIOS mouse driver is the mouse, and so is
    // one reading the mouse's port itself (complete mouse reads, KairoTowns#82); with no keyboard
    // signal of its own, the keyboard stays the default.
    // CAPS and カナ, as the keyboard lights them (TownsKeys.CAPS, KATAKANA).
    int locks() override { return machine_->locks(); }

    bool guest_input(GuestInput &input) override {
        input.mouse_enabled = machine_->mouse_driver_running();
        input.mouse_reads = machine_->mouse_samples();
        return true;
    }
    kairo::Prompt prompt() override { return machine_->prompt(); }
    bool typed_key(char c, int &code, bool &shift) override { return TownsMachine::typed_key(c, code, shift); }
    int special_key(kairo::SpecialKey key) override { return TownsMachine::special_key(key); }
    bool expand_command(std::string &line) override { return TownsMachine::expand_command(line); }

private:
    // The SCSI ID of the hard disk in the slot, or -1.
    int attached_hard_disk() const {
        for (int id = 0; id < kairotowns::Config::kHardDisks; ++id)
            if (!machine_->hd_path(id).empty()) return id;
        return -1;
    }

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
    const std::string hard_disk = launch.media.size() > kHardDisk ? launch.media[kHardDisk] : "";
    if (!hard_disk.empty()) {
        const int id = hard_disk_id(hard_disk);
        if (id < 0) {
            error = "The hard disk " + hard_disk + " is not named .h0 to .h6";
            return nullptr;
        }
        config.hd[id] = hard_disk;
        // A game with a hard disk keeps its own CMOS beside the disk's working copy, with the drive
        // table its MS-DOS writes, so other games keep the machine's; it starts as a copy of that.
        const std::string own = hard_disk + ".cmos";
        if (!std::ifstream(own, std::ios::binary).good()) {
            std::ifstream machine(config.cmos_file, std::ios::binary);
            if (machine.good()) {
                std::ofstream copy(own, std::ios::binary);
                copy << machine.rdbuf();
                if (!copy.good()) {
                    error = "The hard disk's CMOS could not be written to " + own;
                    return nullptr;
                }
            }
        }
        config.cmos_file = own;
    }
    // The settings TownsCatalog's options name; every launch passes all of them.
    config.memory_mb = static_cast<unsigned>(std::stoul(launch.settings.at("memory")));
    config.cpu_mhz = static_cast<unsigned>(std::stoul(launch.settings.at("cpu")));
    const std::string &skip = launch.settings.at("skipMemoryCheck");
    if (skip != "on" && skip != "off") {
        error = "Skip memory check is \"" + skip + "\", not on or off";
        return nullptr;
    }
    config.skip_memory_check = skip == "on";
    // MainActivity's: on when the player starts the hard disk, which then boots before the CD.
    const std::string &boot_hard_disk = launch.settings.at("bootHardDisk");
    if (boot_hard_disk != "on" && boot_hard_disk != "off") {
        error = "Boot hard disk is \"" + boot_hard_disk + "\", not on or off";
        return nullptr;
    }
    config.boot_hd = boot_hard_disk == "on";
    auto machine = TownsMachine::create(config, error);
    if (!machine) return nullptr;
    return std::make_unique<TownsCore>(std::move(machine));
}
