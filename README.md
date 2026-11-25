# NBA Live 98 Fix: NBA Live 98 (PC) on modern Windows

NBA Live 98 for Windows (1997) was made for Windows 95, DirectX 5 and 3dfx Voodoo cards. This project lets you play it on a current PC without changing any of the game's files. A launcher and a few DLLs sit next to the game and fix it in memory while it runs.

**Unofficial.** Not affiliated with or endorsed by Electronic Arts. You need your own copy of the game. No EA code, data or art is included in this project or its downloads.

## What it does

- **Runs on Windows 10 and 11.** No dgVoodoo, no compatibility shims, no registry changes, no CD needed once the game is installed.
- **Graphics.** A replacement for the 3dfx Glide driver, built on Direct3D 11. Full screen (borderless) or a window, a higher internal resolution, VSync. ReShade works with it.
- **Smooth 60 fps.** The game simulates at 60 steps a second; this build shows every step on its own frame, without the stutter and judder the game has on modern PCs.
- **No more stalls.** Fixes for the long pause at start-up, lag on the team select and starting line-up screens, and commentary hitches during a match.
- **Sound.** The game's sound driver is replaced with one built on SDL3, with an adjustable latency.
- **Gamepads.** Modern controllers (Xbox, PlayStation and others) through SDL3, up to four, with the original PlayStation button layout as the default. The D-pad moves around the menus too.
- **Sharper pictures.** Pause screens and Instant Replay at full resolution, players sharp at a distance, and an optional CRT filter.
- **Fixes in the game itself.** Stat boxes no longer drawn under the players. All-Star game jerseys for four players who were on the wrong club.
- **Roster additions.** 54 players cut before the 1997-98 season, added as free agents, and Michael Jordan in the game's unnamed "Roster Player" slot. These use public facts and our own ratings only, and can be switched off.
- **Extras.** Skip the start-up movies and the exit credits, Page Down screenshots, and an Enhanced Options menu inside the game for all of the above.
- **Experimental: widescreen matches.** A wider view of the court at 16:9 and other wide screens, off by default. See Known limits.

## What you need

- Windows 10 or 11 and a graphics card with Direct3D 11.
- **NBA Live 98 for Windows on its original disc**, installed on this PC.
- **The public v1.1 patch** for NBA Live 98, applied to that install. The patched `NBAWIN.EXE` is 2,094,080 bytes, SHA-256 `a5f2a336c779862f89b37a55e4dd5d3fd8e05e1e9d5d661a68fa81f2f2d40a93`. With any other exe the launcher still starts the game, but the fixes that depend on the exact v1.1 exe stay off.
- A gamepad is optional; the keyboard and mouse work as before.

## Installing

1. **Install the game** from your disc. Any install size works; the full install is best.
   - If the game's own setup will not run on your PC, copy the `NBA98` folder from the disc to your hard drive instead (for example `C:\Games\NBA Live 98`). Our launcher makes the install paths match the folder it is in.
2. **Apply the v1.1 patch** to that folder, so `NBAWIN.EXE` is the v1.1 exe above.
3. **Download** `NBALive98Fix-<version>-Main.zip` from the Releases page. (The Debug zip is the same with logging switched on, for bug reports.)
4. **Unzip it into the game folder**, next to `NBAWIN.EXE`. Let it replace any older files of ours, but keep your `nba98.ini` (see Updating).
5. **Start the game with `NBALive98.exe`.** Not `NBAWIN.EXE`: starting the game directly skips every fix.

On the first start the launcher makes your `nba98.ini` from `nba98.ini.default`, and the roster additions are set up in a new `nba98data\` folder. The first start takes a few seconds longer.

You can move or rename the game folder at any time; it keeps working.

### What gets written where

Everything stays inside the game folder.
- `nba98.ini`: your settings.
- `nba98data\`: copies of the game's database with the roster additions, your saved rosters and seasons while the roster additions are on, the main menu layout with our Enhanced Options button, and the install paths.
- `screenshots\`: Page Down screenshots.
- `logs\`: the Debug build's logs (one folder per start).
- The game's own `nbatemp\` (created and deleted by the game).

The game's original files and folders are never changed.

## Updating

Unzip the new release into the game folder and let it replace our files. Your `nba98.ini` is never overwritten: new settings are added from `nba98.ini.default` when the game starts. Your saves in `nba98data\` are kept.

## Uninstalling

Delete the files from the zip, `nba98.ini`, `nba98data\`, `screenshots\` and `logs\`. **`nba98data\saves` holds the rosters and seasons you saved while the roster additions were on**: back it up first if you want them. The game then runs as it did before (with all its original problems).

## Settings

Choose **Enhanced Options** on the game's main menu (above Load). Settings marked with a note take effect the next time the game starts; when you leave a page after changing one, the game offers to restart itself.

| Page | Settings |
|---|---|
| Display | Resolution, Display Mode (full screen or window), VSync, CRT Filter (Off, Auto, Sharp, Soft), Widescreen (experimental), Distance Detail (Original, Auto, Full) |
| Gameplay | Michael Jordan, Cut Players, All-Star Jerseys |
| Controls | Gamepads, Dead Zone, Stick (8-way or raw), Pad Layout (PlayStation or the game's PC defaults), Menu Navigation |
| Sound | Audio Latency (lower is less delay; raise it if the sound crackles) |
| User Interface | Menu Player and Menu Colour (the player photo and colour behind the menus), Jordan art options (only with a personal pack, see below), Skip Intros, Skip Exit Credits |

Music, speech, effects and crowd volumes are on the game's own Rules & Options screen.

Every setting is also in `nba98.ini`, with a short description of each. Delete `nba98.ini` to go back to the defaults.

## Controls

- **Gamepad, in a match:** the PlayStation layout from the original manual. Offence: Circle shoot, Cross pass, Triangle spin, Square crossover, L2 direct shoot, R2 drive. Defence: Square steal, Triangle jump, Cross switch, Circle hand-check, L2 last man back, R2 face up. Both: R1 turbo, L1 direct pass or switch, Select play calling, Start pause. On an Xbox pad, A is Cross, B Circle, X Square, Y Triangle. Change any of it on the game's Controllers screen.
- **Gamepad, in the menus:** D-pad or stick to move, A to choose, B to go back. Start, A, X or B skips the start-up movies and the automatic replays.
- **Right stick button (R3):** CRT filter on or off.
- **Page Down:** screenshot to `screenshots\`.
- **Ctrl+F11:** frame rate meter on or off.
- Movement in the game is 8-way (the 1997 game has no analogue movement); turbo sets the speed.

## Known limits

- **Widescreen is experimental** and off by default. The end-of-quarter stat screens stay 4:3 with black sides, and the menus stay 4:3. **Do not use it in network games**: it has not been tested there.
- **Network play has not been tested with this build.** Only TCP/IP can work on modern Windows (IPX, serial and modem cannot). Reports are welcome.
- **CPU versus CPU matches run at 30 fps**, as the original game does.
- **Replays run at 15 fps**, as the original game does.
- **Michael Jordan** uses a face from the game's Create Player heads, and the arena announcer calls him by the speech the game has for the surname Jordan. The game has no photo or name speech for him.
- Pause menu text is drawn at the game's 640x480 and scaled up.
- Displays above 60 Hz show each frame for two or more refreshes; a variable refresh rate (G-Sync, FreeSync) display gives the smoothest picture.
- Also runs under Wine on Linux (tested during development). Proton and the Steam Deck have not been tested.

## Optional personal art pack

If you have a personal Jordan art pack (`mj.dat`) with your own pictures and speech, put it in the game folder: Jordan then uses its face, headshots, action shots and speech, chosen on the User Interface page. No pack is part of this project or its downloads.

## Troubleshooting

- **Nothing happens or the game starts without the fixes:** make sure you started `NBALive98.exe`, and that `nba98core.dll`, `glide2x.dll`, `eacsnd.dll` and `SDL3.dll` are next to it.
- **"Please insert your NBA Live 98 CD":** the exe is not the v1.1 exe. Apply the v1.1 patch.
- **Sound crackles:** raise Audio Latency on the Sound page.
- **Stutter:** turn VSync on, use full screen, and turn off frame limiters or other overlays in your graphics driver for this game.
- **Gamepad not seen:** plug it in before starting the game, and check Gamepads is On on the Controls page.
- **Something else:** use the Debug zip, reproduce the problem, and attach the newest folder from `logs\` to your report.

## Building from source

The sources are in `src/`. You need mingw-w64 (i686, g++), Python 3 and zip. On Linux or WSL:

```
cd src
./build.sh            # both releases; ./build.sh main or ./build.sh debug for one
```

The releases are written to `src/dist/` (`dist/Main`, `dist/Debug` and a zip of each). `src/README.md` has the technical notes for every part. The code is C++ (a restricted subset, see `src/README.md`).

## What we ship

We do not ship EA assets or EA intellectual property: no game files, art, audio, video, text or database records, and nothing made from them. Anything that needs the game's own material is made on your PC from your own files. The roster additions are public facts plus our own values. See [CONTRIBUTING.md](CONTRIBUTING.md) for the full rule.

## Third-party components

| Component | Licence | Licence file |
|---|---|---|
| SDL 3.2.26 (`SDL3.dll`) | zlib | `SDL3-LICENSE.txt` |
| DOSBox Staging CRT shaders crt-hyllian and vga-1080p, ported to HLSL (in `glide2x.dll`) | MIT | `CRT-SHADERS-LICENSE.txt` |

## Licence

CC0 1.0 Universal (public domain dedication). See LICENSE.
