# Licensing and credits

[← KairoTowns](../README.md)

KairoTowns first-party code is [GPL-2.0-or-later](../LICENSE.md). The pinned [Kairo frontend](https://github.com/MrJackSpade/Kairo) is also GPL-2.0-or-later. Third-party components keep their own notices.

## Tsugaru

The FM Towns emulation comes from [Tsugaru](https://github.com/captainys/TOWNSEMU) by Soji Yamakawa, under the BSD-3-Clause license in `third_party/tsugaru/LICENSE`. The imported source is the author's own code, including its i486 CPU, YM2612 FM sound, and RF5C68 PCM emulation and the bundled `src/externals/` libraries; no other copyright holder appears in it.

## Shared frontend

<!-- kairo:shared-frontend-license -->
The shared RetroArch Slang shader runtime uses librashader under its GPL-3.0-only option, so a shader-enabled KairoTowns build is distributed under GPLv3 terms using first-party "or later" permission. See [shared licensing](../shared/docs/licensing.md).
<!-- /kairo:shared-frontend-license -->

## Firmware and games

KairoTowns does not include FM Towns system firmware (BIOS), operating systems, or games. Users supply any firmware and games they need.
