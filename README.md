# KairoTowns

**Play your own FM Towns games on Android phones and handhelds.** KairoTowns is an Android emulator for the Fujitsu FM Towns, built on the [Kairo shared frontend](https://github.com/MrJackSpade/Kairo): the same game library, controller and touch controls, on-screen keyboard, and per-game settings as [Kairo98](https://github.com/MrJackSpade/Kairo98) and [KairoDos](https://github.com/MrJackSpade/KairoDos). It runs a pinned copy of [Tsugaru](https://github.com/captainys/TOWNSEMU), the FM Towns emulator by Soji Yamakawa. It is an independent project, not an official Tsugaru release.

## Status

Early development. There is no release yet. The emulator core is pinned in this repository, and the Android app is being built around it.

## Games and firmware

Games, operating systems, and firmware are not supplied. The FM Towns needs its system ROMs, which you provide from your own machine.

The pinned core reads CD images (ISO, CUE, MDS), floppy images, and hard disk images.

## Documentation

- [Licensing and credits](docs/licensing.md): code license and third-party components
- [Source provenance](docs/source-import.md): where the emulator source came from and what was copied

## Help and community

- **Chat** with other players on [Discord](https://discord.gg/ZkYQ3bPTTq).

### Related projects

- [Kairo98](https://github.com/MrJackSpade/Kairo98): PC-98 emulator for Android
- [KairoDos](https://github.com/MrJackSpade/KairoDos): DOS emulator for Android
- [Kairo88](https://github.com/MrJackSpade/Kairo88): PC-88 emulator for Android
- [Kairo68](https://github.com/MrJackSpade/Kairo68): X68000 emulator for Android
- [KairoMSX](https://github.com/MrJackSpade/KairoMSX): MSX emulator for Android
- [Kairo](https://github.com/MrJackSpade/Kairo): shared frontend used by every Kairo app

## Building from source

Clone with submodules to get the pinned shared frontend:

```sh
git clone --recurse-submodules https://github.com/MrJackSpade/KairoTowns.git
```

The Android build is not set up yet.

## License

First-party code is [GPL-2.0-or-later](LICENSE.md). Third-party components keep their own licenses; see [licensing and credits](docs/licensing.md).
