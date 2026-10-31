<!-- kairo:discord -->
<p align="center">
  <a href="https://discord.gg/ZkYQ3bPTTq"><img src="https://raw.githubusercontent.com/MrJackSpade/Kairo/main/docs/assets/discord-banner.svg" alt="Join the Kairo Discord" width="49%"></a>
  <a href="https://play.google.com/store/apps/details?id=com.loxifi.kairotowns"><img src="https://raw.githubusercontent.com/MrJackSpade/Kairo/main/docs/assets/donate-banner.svg" alt="Donate: buying KairoTowns on Google Play is the donation; it adds no features" width="49%"></a>
</p>
<!-- /kairo:discord -->

<!-- kairo:family -->
<h3 align="center">Other Kairo apps</h3>
<table align="center">
  <tr>
    <td align="center" width="33%"><a href="https://github.com/MrJackSpade/Kairo98"><img src="https://raw.githubusercontent.com/MrJackSpade/Kairo98/main/kairo98/src/main/res/drawable-nodpi/product_icon_art.png" width="96" alt="Kairo98"><br><b>Kairo98</b></a><br>PC-98</td>
    <td align="center" width="33%"><a href="https://github.com/MrJackSpade/KairoDos"><img src="https://raw.githubusercontent.com/MrJackSpade/KairoDos/main/kairodos/src/main/res/drawable-nodpi/product_icon_art.png" width="96" alt="KairoDos"><br><b>KairoDos</b></a><br>DOS</td>
    <td align="center" width="33%"><a href="https://github.com/MrJackSpade/Kairo88"><img src="https://raw.githubusercontent.com/MrJackSpade/Kairo88/main/kairo88/src/main/res/drawable-nodpi/product_icon_art.png" width="96" alt="Kairo88"><br><b>Kairo88</b></a><br>PC-88</td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="https://github.com/MrJackSpade/Kairo68"><img src="https://raw.githubusercontent.com/MrJackSpade/Kairo68/main/kairo68/src/main/res/drawable-nodpi/product_icon_art.png" width="96" alt="Kairo68"><br><b>Kairo68</b></a><br>X68000</td>
    <td align="center" width="33%"><a href="https://github.com/MrJackSpade/KairoMSX"><img src="https://raw.githubusercontent.com/MrJackSpade/KairoMSX/main/kairomsx/src/main/res/drawable-nodpi/product_icon_art.png" width="96" alt="KairoMSX"><br><b>KairoMSX</b></a><br>MSX</td>
    <td align="center" width="33%"><a href="https://github.com/MrJackSpade/KairoST"><img src="https://raw.githubusercontent.com/MrJackSpade/KairoST/main/kairost/src/main/res/drawable-nodpi/product_icon_art.png" width="96" alt="KairoST"><br><b>KairoST</b></a><br>Atari ST</td>
  </tr>
  <tr>
    <td align="center" width="33%"><a href="https://github.com/MrJackSpade/Kairo64"><img src="https://raw.githubusercontent.com/MrJackSpade/Kairo64/main/kairo64/src/main/res/drawable-nodpi/product_icon_art.png" width="96" alt="Kairo64"><br><b>Kairo64</b></a><br>C64</td>
  </tr>
</table>
<p align="center">Every Kairo app is built on <a href="https://github.com/MrJackSpade/Kairo">Kairo</a>, the shared frontend for the game library, controls, and settings.</p>
<!-- /kairo:family -->

# KairoTowns

**Play your own FM Towns games on Android phones and handhelds.** KairoTowns is an Android emulator for the Fujitsu FM Towns, built on the [Kairo shared frontend](https://github.com/MrJackSpade/Kairo): the same game library with artwork, controller and touch controls, on-screen keyboard, save states, and per-game settings as [Kairo98](https://github.com/MrJackSpade/Kairo98) and [KairoDos](https://github.com/MrJackSpade/KairoDos). It runs a pinned copy of [Tsugaru](https://github.com/captainys/TOWNSEMU), the FM Towns emulator by Soji Yamakawa. It is an independent project, not an official Tsugaru release.

## Download

<!-- kairo:download -->
- **[Google Play](https://play.google.com/store/apps/details?id=com.loxifi.kairotowns)** (paid): updates automatically. Buying it supports development.
- **[GitHub Releases](https://github.com/MrJackSpade/KairoTowns/releases)** (free): install the APK yourself. The larger `-with-images` APK also bundles catalog artwork.

Both editions are built from the same source and have the same features. Requires an ARM64 device running Android 8.0 or newer.
<!-- /kairo:download -->

## Quick start

1. Install KairoTowns from [GitHub Releases](https://github.com/MrJackSpade/KairoTowns/releases).
2. Open KairoTowns and choose the folder that holds your FM Towns games. You can change it later in **Menu › Manage library**.
3. Import the FM Towns BIOS from your own machine in **Menu › Machine** (see [Firmware](#firmware)).
4. Select a game and tap **Play**.

Games and the FM Towns BIOS are not supplied.

## Supported formats

| Type | Formats |
| --- | --- |
| CDs | CUE (with its BIN, IMG or ISO), CCD (with its IMG), MDS (with its MDF), ISO |
| Floppies | D77, D88 |
| Archives | Any of the above inside ZIP or 7z archives |

A game's discs and disks show as one library entry. **Menu › Discs** changes them during play.

## Firmware

KairoTowns needs the FM Towns system, DOS, font and dictionary files (`FMT_SYS.ROM`, `FMT_DOS.ROM`, `FMT_FNT.ROM`, `FMT_DIC.ROM`) from your own machine; the 20-dot font (`FMT_F20.ROM`) is optional. **Menu › Machine** lists every BIOS file, each marked Required or Optional. Pick each file, or use **Import all from a folder…** to import every file the app recognizes in a folder you choose.

## Machine settings

**Menu › Machine** sets these for all games; each game can override them in its **Game settings**.

- **Memory:** 2 to 16 MB (6 MB by default).
- **CPU clock:** 16 to 66 MHz (40 MHz by default).
- **Skip memory check** (on by default): starts games without the FM Towns's power-on memory test.

## Documentation

- [Add a game](docs/add-a-game.md): set up a game the catalog doesn't know, such as your own copy of a CD game with its user disk
- [Create a game](docs/create-a-game.md): make a new game on a blank floppy, format it in Towns OS and save to it
- [Licensing and credits](docs/licensing.md): code license and third-party components
- [Source provenance](docs/source-import.md): where the emulator source came from and what was copied

## Help and community

<!-- kairo:community -->
- **Report a game problem** in [Issues](https://github.com/MrJackSpade/KairoTowns/issues). Include the title, file format, and what happened.
- **Chat** with other players on [Discord](https://discord.gg/ZkYQ3bPTTq).
<!-- /kairo:community -->

## Building from source

<!-- kairo:clone -->
Clone with submodules to get the pinned shared frontend:

```sh
git clone --recurse-submodules https://github.com/MrJackSpade/KairoTowns.git
```
<!-- /kairo:clone -->

Build the app, and optionally install it, with the shared build script:

```powershell
shared\tools\build.ps1 -Install <adb serial>
```

Host tests and UI tests: see [development](docs/development.md).

## License

<!-- kairo:license -->
First-party code is [GPL-2.0-or-later](LICENSE.md). The app as distributed is GPLv3 because it bundles GPLv3 components. Third-party components keep their own licenses; see [licensing and credits](docs/licensing.md) and [source provenance](docs/source-import.md).
<!-- /kairo:license -->
