# LGL — Local Games Launcher

[Русская версия](README.ru.md)

A lightweight, gamepad-driven launcher for local PC games on **Windows XP** and newer.
Think PS2 OPL or Steam Big Picture, but as a single native Win32 executable that runs
comfortably on a Pentium III.

<!-- Add screenshots to docs/ and uncomment:
![Game list](docs/screenshot-list.png)
![Button layout editor](docs/screenshot-editor.png)
-->

## Features

- **Gamepad-first UI**: navigate, launch, edit settings and shut down the PC without a keyboard.
- **Folder scanning**: point it at your games folder, and every subfolder becomes a game.
  The main `.exe` is detected automatically (setup, uninstall, config and redist executables are skipped)
  and can be changed from the game's settings.
- **Gamepad-to-keyboard/mouse mapping** for old games that know nothing about gamepads:
  sticks as WASD, arrows or mouse, a modifier layer (hold LB for a second set of keys),
  key combos like `Ctrl+1`, minimum hold time for games that miss short presses.
  Input is sent only while the game window is active.
- **Button layout editor on the gamepad**: assign any button from an on-screen grid or by pressing
  the key on a real keyboard. Editing a shared profile creates a custom copy for that game.
- **Proper game tracking**: games are started inside a Job Object, so launchers that spawn the
  real game process are tracked correctly. The launcher hides while you play, restores the screen
  resolution and takes focus back afterwards.
- **Close any game from the pad**: hold Back + Start for 1.5 s (hold again to force-close).
- **Covers** from a `covers` folder or `folder.jpg` inside the game folder, plus a script that
  downloads them from SteamGridDB.
- Favorites, recently played, play time, hidden games, sorting, jump by first letter.
- Five color themes, background photos with adjustable dimming, optional navigation sounds.
- Russian and English UI, switchable from the menu.
- Autostart with Windows, restart and shutdown from the menu: a console-like PC.

## Requirements

- Windows XP SP2 or newer (32-bit build; also runs on Windows 11).
  Tested on a Pentium III 1 GHz with 512 MB RAM.
- An XInput gamepad: an Xbox 360 controller or a compatible one (8BitDo, Logitech F310/F710 in X mode, PowerA, PDP…).
- On Windows XP:
  - Xbox 360 Accessories Software 1.2 (the Xbox 360 controller driver);
  - DirectX 9.0c End-User Runtime (June 2010) for `xinput1_3.dll`.
  - Third-party XInput pads may need [PadDriverFix](#paddriverfix) to get the driver installed.

DirectInput-only pads (DualShock, PS2 adapters, generic HID pads) are not supported yet.

## Quick start

1. Copy `LGL.exe` to any folder and run it. It creates `games.ini` and `profiles.ini` next to itself.
2. Open `games.ini` and point it at your games:
   ```ini
   [launcher]
   scan=D:\Games
   ```
   You can add several `scan=` lines.
3. Press **F5** (or Start → Rescan games). Every subfolder of `D:\Games` is now a game.
4. For a keyboard-only game: **Y** → Mapping profile → *Shooter*, *Racing* or *Strategy*,
   or **Y** → Button layout to make your own.

## Controls

| Gamepad | Keyboard | Action |
|---|---|---|
| D-pad / left stick | Arrows | Navigate |
| A | Enter | Launch / select |
| B, Back | Backspace | Back |
| X | Tab | Favorite |
| Y | Space | Game settings |
| LB / RB | PgUp / PgDn | Previous / next section |
| LT / RT | Home / End, letters | Jump by first letter |
| Start | Esc | Launcher menu |
| Hold Back + Start (in game) | — | Close the game |
| — | F5 | Rescan games |
| — | F11 | Toggle full screen |

## Files

All files live next to `LGL.exe`.

| File / folder | Purpose |
|---|---|
| `games.ini` | Scan folders, general settings, per-game overrides. Edited by you. |
| `profiles.ini` | Shared mapping profiles (shooter, racing, strategy…). Edited by you. |
| `profiles_games.ini` | Custom per-game layouts. Written by the layout editor. |
| `state.ini` | Favorites, play time, settings changed in the UI. Written by the launcher. |
| `covers\` | Covers named after the game folder: `covers\Half-Life.jpg`. |
| `backgrounds\` | Background pictures, chosen in Start → Background. |
| `sounds\` | Optional `move.wav`, `select.wav`, `back.wav`. |

### Per-game overrides

Add a section named after the game folder to `games.ini`
(`exe=` may be relative to the game folder or a full path):

```ini
[Heroes3]
name=Heroes of Might and Magic III
exe=Heroes3.exe
args=-nointro
genre=Strategy
year=1999
profile=strategy
```

Games outside the scan folders can be added the same way with a full `exe=` path.

### Mapping profiles

Profiles are plain sections in `profiles.ini`; the generated file documents every key.
A short example (`^A` is A while the layer button RB is held).
Comments in `.ini` files must take a whole line.

```ini
[shooter]
name=Shooter: WASD + mouse
lstick=wasd
rstick=mouse
mouse_speed=900
RT=MOUSE_LEFT
LT=MOUSE_RIGHT
A=SPACE
LB=WHEEL_UP
layer=RB
^A=F5
```

## Tools

### PadDriverFix

Installs the Xbox 360 driver on third-party XInput gamepads under Windows XP, where the
stock driver only accepts Microsoft's own device IDs.

It finds connected pads by their Xbox 360 interface class (`USB\Class_FF&SubClass_5D&Prot_01`),
makes a copy of the installed `xusb21.inf` with their IDs added and installs it.
No Microsoft files are shipped with it: it works with the driver already installed by
Xbox 360 Accessories 1.2. Run it as administrator with the pad connected in X-input mode,
and accept the unsigned driver warning.

### get-covers

A PowerShell script for a PC with internet access. Copy `state.ini` and `games.ini` next to it,
run `get-covers.bat`, and it downloads vertical covers from [SteamGridDB](https://www.steamgriddb.com)
(free API key required), downscaled to JPEG so a slow PC decodes them quickly.
Copy the resulting `covers` folder next to the launcher.

If a game is matched wrongly, add `cover_search=` or `sgdb=<id>` to its section in `games.ini`.

## Building

Open `LGL.sln` in Visual Studio 2010 (the free Express edition is fine) and build **Release | Win32**.
The executables go to `bin\Release\`. The C runtime is linked statically, so they have no
dependencies and run on a clean Windows XP.

Every push is also compile-checked on GitHub Actions with Zig (Clang + MinGW).
That build is for catching errors only; release binaries come from Visual Studio 2010.

## Known limitations

- XInput gamepads only.
- Up to 512 games and 32 mapping profiles.
- Steam games with DRM may refuse to start from their exe or restart through Steam.
- Programs that inject keyboard input, register autostart and can shut down the PC sometimes
  trigger antivirus heuristics. The source is here for you to check and build yourself.

## License

Public domain, see [LICENSE](LICENSE) (The Unlicense).
