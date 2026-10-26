// Draws the guest's pictures on a thread of their own (KairoTowns#21).
//
// TownsRender::BuildImage turns VRAM into the picture from what TownsRender::Prepare copied out of
// the CRTC, VRAM and the palettes, and nothing else. At a rendering point the emulation thread
// prepares the renderer and copies the VRAM it reads and the palettes (a memcpy), then runs on while this
// thread draws; the frame waits for the picture only at its end, so the picture shown after each
// frame is the one the emulation thread would have drawn itself, to the bit. Drawing took about a
// quarter of a busy frame on the RG DS (Mahou Daisakusen, 2.5 of 10.8 ms).

#pragma once

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "crtc.h"
#include "render.h"

namespace kairotowns {

class PictureWorker {
public:
    PictureWorker() : thread_([this] { run(); }) {}
    PictureWorker(const PictureWorker &) = delete;
    PictureWorker &operator=(const PictureWorker &) = delete;
    ~PictureWorker() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            stop_ = true;
        }
        changed_.notify_all();
        thread_.join();
    }

    // Starts drawing the picture of this moment: the renderer is prepared from [crtc] here, and
    // VRAM and the palettes are copied, so the guest may change them while the picture is drawn.
    void start(const TownsCRTC &crtc, const unsigned char *vram, size_t vram_size, bool damper_wire_line,
               bool scan_line_effect) {
        wait();
        render_.Prepare(crtc);
        render_.damperWireLine = damper_wire_line;
        render_.scanLineEffectIn15KHz = scan_line_effect;
        // Only the VRAM this picture reads (TownsRender::KairoVRAMBytesRead, KairoTowns#34); the
        // rest of the copy is never read.
        vram_.resize(vram_size);
        std::memcpy(vram_.data(), vram, render_.KairoVRAMBytesRead(vram_size));
        palette_ = crtc.GetPalette();
        chase_hq_palette_ = crtc.chaseHQPalette;
        {
            std::lock_guard<std::mutex> guard(mutex_);
            drawing_ = true;
        }
        changed_.notify_all();
    }

    // Waits for the picture being drawn, if any.
    void wait() {
        std::unique_lock<std::mutex> guard(mutex_);
        changed_.wait(guard, [this] { return !drawing_; });
    }

    // The last picture drawn; wait() first.
    const TownsRender::ImageCopy &image() const { return image_; }

private:
    void run() {
        for (;;) {
            {
                std::unique_lock<std::mutex> guard(mutex_);
                changed_.wait(guard, [this] { return stop_ || drawing_; });
                if (stop_) return;
            }
            render_.BuildImage(vram_.data(), palette_, chase_hq_palette_);
            render_.SwapImage(image_);
            {
                std::lock_guard<std::mutex> guard(mutex_);
                drawing_ = false;
            }
            changed_.notify_all();
        }
    }

    TownsRender render_;
    TownsRender::ImageCopy image_;
    std::vector<unsigned char> vram_;
    TownsCRTC::AnalogPalette palette_;
    TownsCRTC::ChaseHQPalette chase_hq_palette_;

    std::mutex mutex_;
    std::condition_variable changed_;
    bool drawing_ = false, stop_ = false;
    std::thread thread_;
};

} // namespace kairotowns
