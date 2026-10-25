# KairoTowns' ARM64 core profile

The Android build of the core (Tsugaru and `core/`) is optimized with this profile of the device
playing games ([#9](https://github.com/MrJackSpade/KairoTowns/issues/9)). It is on by default;
`-DKAIRO_PGO=OFF` (CMake) builds the unprofiled core to compare against. Host builds don't use it.

`arm64.proftext` holds LLVM function names, CFG hashes and execution counts. It contains no game
data. `profile.json` records the compiler that made it, the source revision it was trained on, the
text's SHA-256 and the raw profiles merged into it. `shared/cmake/KairoPgo.cmake` checks both,
restores the checkout root in function names (`@KAIRO_SOURCE@/`) and indexes the text with the
NDK's `llvm-profdata` at configure time.

## When the core changes

A function whose code no longer matches its profile is a build error
(`-Werror=profile-instr-out-of-date`), so the profile never silently stops applying. After a change
to the core, train the profile again on the device, from the committed tree:

```text
tools\perf\pgo-train.ps1 -Device <adb address>
```

`pgo-train.ps1` builds the measurement tool to record a profile, runs it flat out over 90 s of each
training disc (Afterburner, Super Street Fighter II, Mahou Daisakusen; 3x3 Eyes is held out),
merges the runs with equal weight and writes this folder with `shared/tools/pgo_freeze.py`. The
text is sparse: functions that never ran are left out, and LLVM optimizes them as it would without a
profile. Experiments on the core use `-DKAIRO_PGO=OFF`
(`tools\perf\device-profile.ps1 -Name <variant> -CMakeArgs '-DKAIRO_PGO=OFF'`).
