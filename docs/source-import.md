# Source import policy

[← KairoTowns](../README.md)

This repository is independent. Its only Git remote is `origin`, pointing to this project's repository. The first-party Kairo frontend is pinned at `shared/` as a submodule. Do not add an `upstream` remote, fork relationship, other submodule, subtree sync, or automated upstream merge. Third-party source is copied in as pinned snapshots under `third_party/`.

## Tsugaru snapshot

`third_party/tsugaru/` holds the emulator source of [Tsugaru](https://github.com/captainys/TOWNSEMU) by Soji Yamakawa (CaptainYS).

| | |
| --- | --- |
| Release | `v20260522`, tag commit `7f3bc46234a67673f532f5016fc42b83c68e3c2b` (2026-05-22) |
| Archive | <https://codeload.github.com/captainys/TOWNSEMU/tar.gz/refs/tags/v20260522> |
| Archive SHA-256 | `59e535dc04ade2d85c1aa051dbe89de57ea4b76ace531911477f5e28799f5342` |
| Imported | 2026-10-21 |

Copied from the archive: `src/`, `LICENSE`, `readme.md`, `command.md`, `hdimage.md`, `references.txt`, and `FMTOWNS_Technical_Databook_Errata.md`.

Not copied: the desktop GUI (`gui/`, `winapp/`), guest-side programs and utilities (`dosapp/`, `townsapp/`, `util/`), test programs and disk images (`testasm/`, `testc/`, `testdata/`), `experiments/`, `symtables/`, `scripts/`, and `reminder.txt`. They are not part of the emulator library. `src/towns/townsdef/.gitignore` was removed so every copied file is tracked.

Local changes to the Tsugaru sources, each marked `KairoTowns:` in the code:

- `src/towns/render/render.cpp`, `TownsRender::Render4Bit`: a 16-colour layer draws each VRAM byte as a run of zoomed pixels, and its row loop only checked that a run started inside the picture, so a layer whose origin and zoom put the last run across the right edge wrote past the row, and on the bottom row past the end of the picture (KairoTowns#7: Super Street Fighter II's warning screen, an 800-pixel picture with a 2x layer at x 186, crashed the app). Each run is now clipped at the row's end.
- `src/towns/render/render.cpp`, `TownsRender::Render16Bit`: a row whose VRAM is contiguous (no scroll wrap within the pixels it shows) is drawn one VRAM pixel at a time, read and converted once and written as many times as the zoom asks, instead of recomputing the scrolled address and converting the colour for every screen pixel; other rows take the original loop. Exact: every picture and sound hash equal on four games. A row at a whole-number zoom (the same on both halves of a pixel pair) writes each VRAM pixel's run as whole words, 2x as one 64-bit store. Exact: as above.
- `src/towns/render/render.cpp`, `TownsRender::Render8Bit`: the same for 256-colour rows (the layer's offset translation still applied to every VRAM address). Exact: as above.
- `src/cpu/i486.h`, `i486instfetch.h`: `i486DXCommon::DecodedInstruction`, a cache of decoded instructions by the host address of code read straight through a memory window; an entry is used only while the code's bytes still equal the bytes it was decoded from (and the default operand and address sizes match), so it is the decode of the code. `KAIROTOWNS_NO_DECODE_CACHE` builds without it, to compare. Exact: as above. `i486runinstruction.h`: `RunOneInstruction` reads a cached decode in place (`FetchInstructionClass::FetchInstructionRef`) instead of copying it out, and the instruction is cleared only when it is decoded afresh; nothing fetches again before the instruction has run. Exact: as above.
- `src/towns/pic/pic.h`, `pic.cpp`: `TownsPIC::State::UpdateIRRCache` keeps a request in its cache only when `ProcessIRQ` would fire it, chosen as `ProcessIRQ` chooses: not masked (OCW1), not held back by one in service and not held back in special fully nested mode. It runs after every register write, a fired interrupt, a reset, a loaded state and a change of a request bit (not when a device restates an unchanged request). So `ProcessIRQ` no longer searches both chips after every instruction while a request cannot fire, and the wait-loop skip goes on through such a request (KairoTowns#8: `INTToGo` was 2.6% of Mahou Daisakusen on the RG DS, the PIC is 0.6% in all now). Exact: every picture and sound hash equal on four games over 120 s.
- `src/towns/memory/physmem.cpp`, `TownsPhysicalMemory::IOReadByte`: reading I/O ports FF94h, FF95h and FF99h returned an uninitialized local, so a game reading them (Mahou Daisakusen) behaved differently from run to run (KairoTowns#32). They return what their memory-mapped twins at CFF94h, CFF95h and CFF99h return: 80h, FFh and FFh.
- `src/towns/render/render.h`, `render.cpp`: `TownsRender::SwapImage` hands over the picture and takes the previous one's buffer back, so drawing the next picture of the same size reuses it instead of allocating and zero-filling a new one as after `MoveImage`.
- `src/ramrom/ramrom.h`, `src/inout/inout.h`, `inout.cpp`, `src/cpu/i486.h`, `i486runinstruction.h`, `src/towns/memory/physmem.cpp`: counters of everything the CPU does that may have an effect beyond its own state and RAM, for the wait-loop skip (`core/wait_loop.h`): `Memory::sideEffects` counts every store (also those through the CPU's stack window and operand pointers) and every read from a memory object other than main RAM (`MemoryAccess::readWithoutSideEffect`); `InOut::accesses` counts every I/O access.
- `src/cpu/i486.h`: `i486DXCommon::KairoCodeOperandSize`, `KairoCodeAddressSize` and `KairoFetchOperandLoop`; `src/ramrom/ramrom.h`: `MemoryAccess::MemoryWindow::KairoLinearBaseAddrOffset`, `ConstMemoryWindow::KairoLinearBaseAddrOffset` and `Memory::KairoAccessTable`. Read-only access for the JIT (`core/jit`, KairoTowns#19), which decodes code as the interpreter does and reaches main RAM as it does. `src/ramrom/ramrom.h`, `ramrom.cpp`: `Memory::kairoMapGeneration` counts changes to which object serves which 4 KB slot, so the JIT knows when to drop where it keeps main RAM per page.
- `src/towns/cdrom/cdrom.h`, `cdrom.cpp`: `TownsCDROM::waitForWave`, and `src/towns/rtc/rtc.h`, `rtc.cpp`: `TownsRTC::fixedTime`, set only by `Config::repeatable` for host tests, so two runs repeat exactly: the CD audio read finishes before the guest goes on, and the clock starts at a fixed time and runs with guest time.

The Android and host builds (`core/CMakeLists.txt`) build Tsugaru's own subdirectories without changing them. For compilers other than MSVC they force-include `<cmath>` and `<cstdlib>` into C++ files that use `std::pow` and `rand` without including them, choose the Unix or Windows file-system helper and the silent sound backend themselves, give Android an empty `pthread` target (its threads are in the C library), on Windows pass `-fms-extensions` for MSVC's `__assume`, and compile with `-fsigned-char` (Tsugaru assumes x86's signed `char`; on ARM the BIOS otherwise stops on an undefined instruction) and `-O3` in every build type, because the core only runs in real time optimized.
