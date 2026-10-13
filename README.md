# Frontschweine Recompiled

A native PC version of **Hogs of War** (German release *Frontschweine*,
Infogrames, 2000) for the PlayStation, built by static recompilation: the
game's MIPS code is translated to C and compiled into a Windows program that
runs against a hardware-accurate runtime, without an emulator.

The game plays from start to finish. On top of the original it comes with
these enhancements:

| Enhancement | Default | Effect |
|---|---|---|
| CPU Overclock | On, 200 % | Removes the slowdowns of the original hardware |
| Widescreen | On, 16:9 | Wider view in battles, HUD kept at its 4:3 layout |
| CD Audio Smoothing | On, 100 ms | Fades instead of clicks when CD audio stalls |
| Instant Loading | On | Loads game files from an extracted copy of the disc, without waiting for the emulated drive |
| Fast Enemy AI | On | Runs the AI's terrain line checks natively, so the picture no longer freezes while the enemy thinks |
| 60 Hz Mode (P/N Selector) | On | Plays like the classic PAL/NTSC selector patch: 60 Hz, battles at 30 instead of 25 fps, everything 20 % faster |

Each one is a mod you can switch on, off or adjust in the launcher's **Mods**
page; widescreen also offers 21:9.

Instant Loading takes over the game's own file loader: each data file is
copied into memory at once from `extracted/` next to the program, which is
filled from your disc the first time a file is needed. Speech, music and
cutscenes keep streaming from the emulated drive with their original timing.
The framework's generic CD Speed and Fast Loading mods are therefore not
shipped with this game.

Built on [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp), via
the fork [Nero2611/psxrecomp](https://github.com/Nero2611/psxrecomp) (branch
`frontschweine`), and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).

## You need

- **Your own copy of the game**: the German PAL release, serial SLES-02767,
  as a CUE file with its three BIN tracks. The build checks the data track
  against known checksums; other releases are not supported.
- Windows 10 or 11 (x64)
- [Git](https://git-scm.com/) and [Python](https://www.python.org/) 3.11 or
  newer

The compiler toolchain (clang, CMake, Ninja) is downloaded automatically on
the first build. No PlayStation BIOS is needed: the open-source OpenBIOS
bundled with psxrecomp is used.

## Build

```bash
git clone --recursive https://github.com/Nero2611/FrontschweineRecomp.git
cd FrontschweineRecomp
python psxrecomp/psxrecomp_cli.py generate --project-root . --disc "path/to/Frontschweine (Germany).cue"
python psxrecomp/psxrecomp_cli.py rebuild --project-root . --build-dir build
```

`generate` verifies your disc, prepares a working copy of it in `disc/` and
translates the game code; `rebuild` compiles the program. The rebuild
includes two short profile-guided training runs of the game for extra speed.
Then start `build/Frontschweine_Recompiled.exe`.

The battle modules the game unpacks at runtime are compiled ahead of time by
`generate`, from your disc. Other code the game loads at runtime is translated
in the background the first time it runs and cached in `build/cache/`.

**If `rebuild` warns** `overlay toolchain staging failed; overlays will run
interpreted`, the Tiny C Compiler archive could not be downloaded from
`download.savannah.gnu.org` or its mirrors. Fetch `tcc-0.9.27-win64-bin.zip`
from any GNU mirror (for example
[mirror.netcologne.de](https://mirror.netcologne.de/savannah/tinycc/)), place
it in `.cache/overlay-toolchain/` and run `rebuild` again. Its checksum is
verified against the pinned value, so any mirror is safe.

## Setup package

Instead of cloning, you can download the setup zip from the
[Releases](https://github.com/Nero2611/FrontschweineRecomp/releases) page. It
contains the setup program and the project sources, but no game data and no
translated game code. You need [Python](https://www.python.org/) 3.11 or
newer. Unpack the zip, start `Frontschweine_Recompiled.exe`, select your disc
and follow the **Generate & rebuild** wizard; it downloads the compiler
toolchain and builds the game on your machine.

## Legal

This repository contains no game data: no disc image, no executable code and
no assets of the original game, and no PlayStation BIOS. The translated code
is produced locally from your own disc and never leaves your machine.

*Hogs of War* and *Frontschweine* are trademarks of their respective owners;
PlayStation is a trademark of Sony Interactive Entertainment. This is an
unofficial, non-commercial fan project and is not affiliated with or endorsed
by any of them.

## License

[PolyForm Noncommercial License 1.0.0](LICENSE): you may use, change and
share this project for any non-commercial purpose. The psxrecomp framework is
licensed separately under the same terms by its author.

## Credits

- [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp) by Matthew
  Stanley and the RetroPortingToolKit team, PolyForm Noncommercial 1.0.0
- [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui), the launcher
