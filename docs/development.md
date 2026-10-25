# Development

## Building

Build the app, and optionally install it, with the shared build script (see [Android build](../shared/docs/android-build.md)):

```powershell
shared\tools\build.ps1 -Install <adb serial>
```

## Host tests

The machine also builds for the host, without Android: build `core/` with CMake for the host. Run them under `shared\tools\run-limited.ps1`.

- `boot_test <rom dir> <work dir> <disc image> <seconds>` boots a disc, checks that a save state loads back to the same picture, and starts a second machine in the same process, as the app does for its next game.
- `frame_dump <rom dir> <work dir> <disc image> <seconds> <frames between pictures> <skip|check>` runs a disc and writes a picture every so many frames, printing each change of picture size; to look at what the core itself draws. With `check` (the memory check runs, and the run is repeatable: the clock starts at a fixed time and CD audio reads finish before the guest goes on) two runs of the same build draw the same pictures; `skip` boots as the app does, as fast as real time allows, which varies. Build it with `-fsanitize=address` to check the core's memory use with a game.
- `game_profile <rom dir> <work dir> <disc image> <seconds> <skip|check> [pcs] [host] [noskip] [exact]` runs a disc flat out and reports, for every 600 frames, the host time a frame took (mean, p95, longest), how much went to pictures and to sound, and the guest instructions run and skipped. `exact` makes the run repeatable and prints a hash of every picture and all sound per 600 frames: a change that is exact prints the same hashes as the build before it (with `check`). Until KairoTowns#32 is fixed (uninitialized reads in the core still make some runs differ), compare builds configured with `-DCMAKE_CXX_FLAGS=-ftrivial-auto-var-init=zero`, where every run repeats. `noskip` turns off the wait-loop skip to compare. `pcs` samples the guest CPU's CS:EIP over the last half and disassembles the hottest place; `host` (Windows) samples the emulation thread's host instruction pointer into `<work dir>/host-samples.txt`, for `llvm-addr2line` against a build with `-g`.
- `tools\perf\device-profile.ps1 -Device <serial> -Disc <disc image> -Seconds <n> [-Options skip,exact,...] [-Name <variant> -CMakeArgs <...>]` builds `game_profile` for arm64 with the NDK, pushes it with the firmware and the disc's folder to `/data/local/tmp/kairotowns` and runs it there: the same report on the device, flat out, without the app.
- `memory_check_test <rom dir> <work dir> <seconds>` boots with no media twice, skipping the power-on memory test and then running it, and writes one picture a second of each.

<!-- kairo:ui-tests -->
## UI tests

KairoTowns has device tests in the style of Selenium tests, on the shared Kairo test framework: they
name the app's elements, act on them as a player would, and assert what the player would see
([how they work and how to run them](https://github.com/MrJackSpade/Kairo/blob/main/docs/ui-tests.md)). Run them from this
repository with a device connected over adb (Android 10 or later) and the `LOCAL_BUILD_TOKEN` key set:

```
shared\tools\uitest.ps1 -Device <serial>                                              every test
shared\tools\uitest.ps1 -Device <serial> -Filter 'MenuTests|ControllerTests'          by test name
shared\tools\uitest.ps1 -Device <serial> -Class com.mrjackspade.kairotowns.uitest.<Class>   one class
shared\tools\uitest-all.ps1 -Device <serial>                                          every Kairo app
```

The tests are in `kairotowns/src/androidTest/java/com/mrjackspade/kairotowns/uitest/`. They run in a
build of their own, `com.loxifi.kairotowns.uitest`, installed beside the app without touching its data, and
each run's logs, screenshots and summary land in `.build/uitest/<run id>/`.
<!-- /kairo:ui-tests -->
