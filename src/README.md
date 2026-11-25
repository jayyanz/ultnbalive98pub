# NBA Live 98 PC: rebuild source

Plan and findings: see the project doc `nba98-plan-and-exe-findings.md`.
Reference exe: `Patch v1.1/NBAWIN.EXE` (SHA-256 a5f2a336...2d40a93).

## Building: Main and Debug releases

`./build.sh` (mingw-w64 i686 g++, Python 3, zip) builds both releases from the same source; `./build.sh main`
or `./build.sh debug` builds one. The version is in `VERSION`; every build is stamped with the UTC build
time, e.g. `0.1.0 Debug (20261028.1216)`. Output:
- `dist/Main/` and `dist/Debug/`: the full file set for the game folder (`NBALive98.exe`, `nba98core.dll`,
  `glide2x.dll`, `eacsnd.dll`, `SDL3.dll`, `nba98.ini.default`, `nba98_cutplayers.dat`, `nba98_jordan.dat`),
  plus `dist/NBALive98Fix-<version>-<build>.zip`. Since 0.7.0 a release never contains `nba98.ini` (the
  build refuses it), so an update cannot overwrite the user's settings.
- `build/` holds intermediate files only.

| | Main (players) | Debug (testing) |
|---|---|---|
| Logs (`logs\launcher.log`, `nba98core.log`, `glide2x.log`, `eacsnd.log`) | off (`Log=1` in a section turns one on) | on |
| Crash report | none (normal Windows behaviour) | log entry, `crash\nba98_<date>_<time>.txt` and `.dmp`, game closed cleanly, launcher shows where the report is |
| System report at start (versions of our files and SDL3, Windows or Wine version, CPUs, RAM, desktop mode, nba98.ini settings) | no | yes |
| Diagnostics: `CrashTest`, `CrashDumpFull`, `LogFileOpens`, `ThreadDump`, `WatchAddr`, `DumpFrame`, test gamepad | compiled out | available |
| Compiler | -O2, stripped | -O1 -g, frame pointers, symbols kept |

Shared pieces: `common/nba98_build.h` (version, build name, log defaults, `logs\` path helper),
`common/version.rc.in` (version resource in every file, shown in Explorer's Details tab),
`common/nba98.ini.in` (one ini template; `;@debug` / `;@main` blocks pick the lines per build, made by
`tools/make_ini.py`), `third_party/SDL3.dll`.

Both launchers read the version resource of `nba98core.dll`, `glide2x.dll` and `eacsnd.dll` and warn
(with a choice to start anyway) if any file is missing, has no version, or comes from another version or
build. The Debug core keeps its own exception filter on top of the game's (Watcom runtime) one: the
game's filter is still called first, and if it does not handle the exception the report is saved and the
process ends with the exception code instead of showing the Windows or Wine crash dialog.

Tested under Wine: both builds start and reach a match; Main writes no logs; Debug writes all four logs;
`CrashTest=25` gives a .txt and .dmp (MDMP) in `crash\` and the launcher message; a Debug
`nba98core.dll` next to a Main launcher gives the mismatch warning.

## C++ rules (since 0.7.15)

The release code is C++ compiled with mingw-w64 g++ (i686, `g++-mingw-w64-i686`, the win32 thread model), in a
restricted subset so the files stay msvcrt-only and keep the Windows 98 path open (project doc
nba98-cpp-and-win98-plan):
- `-std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics`, `-static-libgcc`. No `std::thread`, no
  iostreams, little or no STL (fixed arrays and our own small helpers). The built files must not need
  libstdc++-6.dll, libgcc_s or libwinpthread-1.dll.
- **Imports must not grow:** every file keeps the same DLL list, and no post-Windows-98 function becomes a static
  import (look newer APIs up at run time). **Exports must not change:** glide2x.dll's Glide names (stdcall
  decoration, `glide2x.def`), eacsnd.dll's six `iSND..._` names (`eacsnd.def`) and nba98core.dll's names. Compare
  both tables against the last release after every change.
- Every export and every symbol the inline asm thunks use by name has C linkage. Each source file includes
  `common/nba98_cxx.h` first and puts its own code between `NBA98_C_BEGIN` and `NBA98_C_END` (`extern "C"`), so
  every name is the same as in the C build. `nba98_cxx.h` also sets `CINTERFACE` and `COBJMACROS` (the C-style COM
  calls such as `IDXGIFactory1_Release(f)` stay) and `D3D11_NO_HELPERS`.
- Files: the compiled sources are `.cpp` (`launcher/launcher.cpp`, `nba98core/nba98core.cpp`,
  `glide2x/gl_api.cpp`, `gl_d3d.cpp`, `gl_win.cpp`, `gl_stubs.cpp` (generated), `eacsnd/eacsnd.cpp`); the files
  #included into them in a fixed order are `.inc` (`nba98core/optmenu.inc`, `widescreen.inc` and the rest,
  `glide2x/gl_overlay.inc`, `gl_judder.inc`, `gl_shot.inc`). `glide2x_log/` and the `test/` programs stay C (not
  shipped).

## Frame rate and pacing meter

- Overlay (glide2x, `glide2x/gl_overlay.inc`), both builds: `[glide] Overlay=1` shows it from the start (default in
  Debug), Ctrl+F11 shows or hides it any time (handled in our window procedure, the game never sees it).
  Line 1: fps, frame time avg (min-max), frames over 50 ms. Line 2: game simulation steps per second and the
  game clock (ticks per second, min-max gap). Line 3: frames that showed 0, 1, 2, 3+ new simulation steps.
  Graph: last 212 frame times, 0 to 50 ms, lines at 16.7 and 33.3 ms, bars coloured by steps shown (grey 0,
  green 1, yellow 2, red 3+). Text is GDI into a DIB, uploaded as a texture and alpha-blended on the back buffer.
- Counters (nba98core, `nba98core/timing.inc`, shared struct `common/nba98_stats.h`, export `nba98core_stats`):
  game clock callbacks and their QPC gaps (through our timeSetEvent wrapper), and simulation steps: the step
  block's first instruction at 0x415A0C (`mov eax, [0x5E7ED1]`, 5 bytes) becomes a call to a thunk that counts
  and does the same load. Game state is never written.
- `[nba98] TimingLog=1` (default in Debug): `logs\timing.log`, one line per second with ticks/s and gap min avg
  max, steps/s, and the frame figures from glide2x.
- Wine (software rendering) results: the clock is 120 ticks/s on average but gaps run 0.1 to 17 ms (clumpy,
  as predicted); a match runs 60 simulation steps/s whatever the frame rate (at 6 fps under llvmpipe every
  frame showed 3+ steps), so the simulation is 60 Hz.

## Smooth 60 fps: precise clock and step lock (Phase 3, steps 1 and 2)

The first Windows timing log (6.4 h) showed 60.0 simulation steps/s and a steady 60 fps, but only 68% of
frames showed exactly one new step (16% none, 16% two): the judder. Two fixes, both on by default:
- `[nba98] PreciseClock=1` (nba98core, `timing.inc`): the exe's GetTickCount import returns a millisecond
  clock from QueryPerformanceCounter (same start value, wraps the same way) and timer resolution is set
  to 1 ms. The game's 120 Hz timer callback re-arms itself from GetTickCount, so its ticks no longer come in
  15.6 ms clumps. Under Wine the tick gaps went from 0.1-17 ms to about 5-12 ms.
- `[glide] StepLock=1` (glide2x, `step_lock()` in `gl_d3d.cpp`): in a match each swap waits (on an event the
  core sets after every finished game timer callback) until the clock has finished 2 more ticks than at the
  last swap, so the game finds exactly one new input frame, runs one step and draws it. Frames follow the
  game clock. Off in menus and pauses (no steps for 250 ms), 50 ms timeout, no wait when drawing ran late.
  `StepLock=2` shows every other step (even 30 fps). The overlay shows "lock" and "qpc" when active.
- Stats struct version 2 adds `ticks_done`, `tick_event`, `precise_clock`.
- `[glide] DisplayLock=1` (`display_lock()` in `gl_d3d.cpp`, with `nba98core_set_clock_rate` exported by the
  core): the second Windows log (0.4.0, 59.94 Hz desktop) had 87% perfect seconds; the rest came in 3 to 5
  second bursts every ~17 s (1 / 0.06 Hz), the 60.0 Hz game clock beating against the 59.94 Hz display
  (presents are throttled to vblank even with sync 0). The game clock now runs at display rate / 60 (0.999x
  at 59.94 Hz) and a slow correction keeps each present halfway between refreshes, using DWM's composition
  timing (vblank time and period). Displays near 60 or 120 Hz only. The core's GetTickCount clock is a
  two-slot anchor (QPC, ms, rate) so rate changes are atomic and continuous. Stats struct version 3.
- Wine test (llvmpipe manages about 30 fps, so StepLock=2 was used): every match frame showed exactly 2 steps
  (30/30 per second) against a mix of 2 and 3+ without the lock.
- Not yet covered: displays above 60 Hz still get each 60 Hz frame shown for 2 or 3 refreshes (uneven on
  a fixed 144 Hz screen, fine with VRR); that is the interpolation step. The per-render camera timer issue
  above 60 fps does not arise while frames are locked to steps.

## Machine report in the logs (0.5.2)

`nba98core/sysinfo.inc` (both builds) writes to `logs\nba98core.log` when logging is on, and as a `machine:`
line at the top of `logs\timing.log`: Windows edition, version and build with update revision (or the Wine
version); PC maker, model, board and BIOS; CPU name, logical CPUs and RAM; every graphics adapter with PCI ids,
video memory and driver version (plus the NVIDIA release number, e.g. 32.0.15.7680 = 576.80); every active
display with resolution, refresh rate and position; versions of our files and SDL3; the nba98.ini settings.
It runs on the timing thread because it loads dxgi.dll (not allowed in DllMain).

## Stall profiler (Debug, 0.5.3)

`nba98core/profile.inc`, `[nba98] ProfileStalls=1` (default in Debug): whenever the current frame has taken over
40 ms, a high-priority thread samples the game's main thread every 2 ms (suspend, copy registers and stack,
resume; nothing that takes a lock while suspended) and records the module it was in and the first two
NBAWIN.EXE addresses on its stack (site and caller). The game's file calls (open, read, write, seek, flush,
copy, delete, find, volume, disk free, drive type, move) are timed through its import table, split into main
thread and other threads, with the names of files the main thread opened. One block per second with slow
frames goes to `logs\profile.log`. Also: the LogFileOpens setting is now read once (it was re-reading
nba98.ini on every file open in the Debug build).

## Drive information cache (0.5.4)

`nba98core/fscache.inc`, `[nba98] CacheDriveInfo=1` (default, both builds). The stall profiler showed the game
calling GetVolumeInformationA and GetDiskFreeSpaceA for the drive on almost every file it opens (100 of each
on a screen change, 20 to 60 ms a second on Jason's PC). The exe's imports of those two and GetDriveTypeA are
answered from a per-root cache: volume details and drive type for the session, free space for 5 seconds.
Failures are not cached. Hits and misses are logged once a minute (Wine test: 537 hits, 10 misses).
Names use an `fsc_` prefix: profile.inc (Debug) wraps the same imports, and C merged the two same-named static
function pointers into one, which sent the cache into endless recursion on the first call (found under Wine).

Also `tools/nba98pic.py` (player picture converter, see the project doc `nba98-player-pictures.md`).

## Temp file write buffer (0.5.5)

`nba98core/tempbuf.inc`, `[nba98] BufferTempFiles=1` (default, both builds). At the starting line-up the game
builds each player card into `nbatemp\` (jersey template plus the picture from teamdata.viv) in about 20 small
writes, about 1.4 ms each on Jason's PC (0.7 s of every second). Files under an `nbatemp\` path opened for
writing are kept in memory while open: WriteFile, ReadFile, SetFilePointer, GetFileSize and SetEndOfFile work on
the memory copy, and the file is written in one WriteFile on CloseHandle. FlushFileBuffers on these files writes
the copy out but skips the real flush. GetFileInformationByHandle / GetFileTime write the copy out first;
DuplicateHandle and CreateFileMappingA stop buffering that handle; any new open writes out all pending copies.
Names, folder, find, delete and rename stay real. Files over 16 MB, overlapped and unbuffered handles are not
buffered. Wine test (exhibition to tip-off): 30 temp files, 834 writes became 30, and the 30 files were
byte-identical to a run with the buffer off.

## Boot pause fix: joystick thread start race (0.5.6)

Some boots stopped for 7 to 25 s after the gamepad setup. The stall profiler showed the main thread for all of
it in the SleepEx(0) right after the game created its joystick thread. The game (0x4f33c8) creates the joystick
thread (0x4f3174, above normal priority, through the thread starter 0x5382e0, which resumes it and yields), and
only afterwards creates the event (0x6247fc, CreateEvent via 0x5386b4) and lock (0x624800, 0x5387a0) that thread
waits on. When the thread runs first, its wait on the null event fails at once and it polls the joysticks in a
tight loop; with the game's threads on one CPU it starves the main thread, which only gets in on Windows'
starvation boost (about every 4 s). `h_CreateThread` now starts that one thread through `joy_thread_wrap`, which
sleeps until both values are set (at most 2 s) and then calls the game's entry. Applies to the verified v1.1 exe
only. Wine: the wrapper waited 1.1 ms and the sound thread started 1 ms after the joystick thread. The other
game threads (0x551148, 0x551b4c, 0x5340ac, 0x5252b0) create their objects first.

Debug additions: `BootWatch=1` logs every thread with its CPU use every 0.5 s if the sound thread has not started
3 s after the joystick thread; thread dumps show CPU ms per thread; profile.log gains a
"threads (CPU ms this second)" line per slow second (tid, priority, CPU ms) for the game's threads.

## Front buffer flush thread (0.5.7)

Updates the game draws into the Glide front buffer (LFB writes, triangles to the front) are shown at most once
per frame period; one that arrived sooner waited for the game's next Glide call. On the starting line-up the game
then sat in its own input loop for about 5 s (main thread at full CPU in game code, no Glide or file calls, other
threads idle; the same 4992 ms frame is in the 0.5.3 profile), so the last update could stay hidden that long.
glide2x now runs a small thread (`front_flush_thread` in gl_d3d.cpp) that shows any waiting front buffer update one
frame period after the last present. It only takes the Glide lock with TryEnterCriticalSection and is stopped in
d3d_close. glide2x.log notes the first 20 such updates and the total at close.

Note: the profile run also showed the player card files (nbatemp) are built during the match loading screen, not
the starting line-up; with the temp file buffer those 684 writes took 11.5 ms in total.

- **Text colours and more pages (0.6.8):**
  - Headings are yellow and notes white, as on the game's own screens. The game sets text colour in two calls:
    0x525C5C converts a 32-bit colour to the screen format and returns it in EAX, and 0x459FCC sets the text colour
    from EAX. We had called 0x459FCC with EAX 0, so our labels were black.
  - Root page: Display, Gameplay, Controls, User Interface, Back.
  - Gameplay: Michael Jordan (`[nba98] Jordan`), Cut Players (`CutPlayers`), each On or Off.
  - User Interface: Jordan Art (`JordanArt`), Jordan Headshot and Jordan Action Shot (`JordanHeadshot`,
    `JordanActionShot`: Random, then each picture the pack has, shown as "2 of 3"; "none in mj.dat" when there is no
    usable one), Jordan Speech (`JordanSpeech`). Planned here later: HUD scaling, intro and credit skips.
  - All take effect the next time the game starts (noted on each page). Wine test: every row cycled and saved.

- **Menu Player and Menu Colour (0.6.9, User Interface page):** at start-up the game picks a menu tint (`rand() % 11`,
  a dword at 0x619879) and a player photo set (`rand() % 5 + 1`, word at 0x61987D); every front-end screen reads both
  when it loads (project doc nba98-menu-backdrops). The call at 0x42F4E2, right after the two picks, goes through a
  thunk that writes `[menu] Tint` and `Player` when they are not `random`. A change in our menu is written at once
  and shows on the next screen. Colours, by how they look: Grey (no tint, new), Rose, Pink, Orchid, Lavender, Blue,
  Sky Blue, Teal, Mint, Green, Gold, Copper. The tint dword is 0x00RRGGBB as the screen shows it (blue in the byte
  at 0x619879); the investigation had guessed the other order, and its names were corrected after a Wine check.
  Players: Mitch Richmond, Larry Johnson, Christian Laettner, Tim Hardaway, Joe Dumars (sets 1 to 5). Wine test:
  Rose and Tim Hardaway pinned from the first screen; Blush/Rose changed in our menu showed on the next screen.

## Stat boxes covered by players: out-of-range depth (0.6.6)

In a match, players, their shadows and floor reflections could show over the stat boxes (Rebounds, Steals,
quarter scores). dgVoodoo showed the same.

- **Cause (found with the draw trace below):** the game draws its HUD last, in screen space, with the depth
  test "less or equal" and depth writes on. The text uses ooz 0 (nearest). The box backgrounds (icon panel,
  header bar, dark band; Glide calls from 0x55C09B, 0x55B89A, 0x55B8B2) use ooz 32768000 = 500 x 65536 with
  oow 0.02. A Direct3D depth range clamps that to the far end, so the box failed the test wherever a player or
  reflection had already written depth, and passed over the bare floor (drawn without depth writes).
- **Fix:** `glide2x.dll` wraps any ooz past the 16-bit range to its low 16 bits in the z-buffer modes, as a
  Voodoo's 16-bit depth does: 32768000 becomes 0, so the boxes cover the players. Normal draws never reach it
  (the highest ooz in 24 traced match frames, 58,083 draw runs, was 18,102). `[glide] DepthWrap=1` (default);
  0 gives the old behaviour. The log notes the first wraps with their caller.
- The dark band of each box is see-through by design (the court shows through); only its contents changed.
- Not yet checked on real 3dfx hardware.

### Draw trace (Debug)

Ctrl+F10 in the Debug build writes the next frame's draws to `logs\glide_trace_<frame>.txt` and saves that frame
as `logs\glide_frame_<n>.bmp`. One line per run of triangles with the same caller and state: the game's return
address into the Glide call, triangle count, screen box, ooz and oow ranges, depth mode, function, write and bias
level, blend, alpha test and chroma key, texture address and format, colour and alpha combine, cull and buffer.
Clears, LFB locks and writes, and the buffer swap are listed in order too.

What the traces show about how the game draws a match frame: arena and floor first with depth always and no
writes; the 3D court with real ooz plus bias 10000; players, shadows and reflections with real ooz and a
per-primitive bias level that works as a sort key (about 2000 to 13000); player markers with depth always;
more 3D with bias 0; then the HUD.

## Optional personal art and speech for Jordan (0.6.0, speech 0.6.1)

`nba98core/jordan_art.inc`, `[nba98] JordanArt=1` and `JordanSpeech=1` (defaults). Builds can use an optional
personal pack named `mj.dat` in the game folder. No art or audio is part of this project: not in the repo, the
dist zips or any release. `.gitignore` keeps `mj.dat`, pictures, face packages and VIV copies out, and
`build.sh` refuses to package any file in dist other than the release files.

- **Pack check (every start, after the roster setup):** `mj.dat` must be a complete BIGF archive. Its two
  parts are judged separately, and a bad speech part never turns the art off.
  - Art: `0MJORDAN.FSH` (an SHPI package), `MJORDAN.BMP` (103x118, 32-bit) and `MJACT.BMP` (146x205, 32-bit),
    each optional from 0.6.7 (see below).
  - Speech: `GAMESPCH\MIJORDA.HDR/.DAT` and `PLAYSPCH\MIJORDA.HDR/.DAT`, all four or none. Each pair:
    hdr byte 3 = take count, byte 4 x 256 = .dat length, take offsets from byte 8 (3-byte records in the arena
    bank, 4-byte in play-by-play), every take starting `PT\0\2`.
  - Missing, damaged or switched-off parts are logged and use the defaults. The pack is only used with
    Jordan's roster data in use (`Jordan=1`, nba98data active).
- **Roster fields (records 303 and 404 only, all three copies: dbdata, origdb, saves\default):** one masked
  MODIFY through `rm_apply`, every start. Art: PLAYERPKG / L_PORT / ACTSHOT = `0MJORDAN.FSH` / `MJORDAN.BMP` /
  `MJACT.BMP`. Speech: SPCHHDR / SPCHDATA = `MIJORDA.HDR` / `.DAT`. Defaults: PLAYERPKG and the speech keys from
  `nba98_jordan.dat` (`:KAAA23.303`, REJORDA); L_PORT and ACTSHOT from the user's own `origdb\PLAYERS.DBF`
  (`1ROPLAYE.FSH`). Reggie Jordan (442) is not touched.
- **Parts are independent (0.6.7):** the face (`0MJORDAN.FSH`), the headshot and the action shot are each used
  if `mj.dat` has a usable one; anything missing or bad stays the default for that part only (face code, or "No
  photo available"), and is logged. A pack with no pictures, or with only one, works.
- **Several headshots and action shots (0.6.7):** `MJORDAN.BMP`, `MJORDAN2.BMP` to `MJORDAN9.BMP` (103x118) and
  `MJACT.BMP`, `MJACT2.BMP` to `MJACT9.BMP` (146x205), all optional, each checked (32-bit uncompressed, that size);
  a bad one is logged and left out. The game only asks for `MJORDAN.BMP` / `MJACT.BMP`, so at each start one usable
  picture of each kind is written under that name to `nba98data\art\`. `[nba98] JordanHeadshot` and
  `JordanActionShot`: `random` (default) picks one at random, never the one shown at the previous start (kept in
  `nba98data\art\mjordan.txt` / `mjact.txt`); `1` to `9` pins one, falling back to the first usable one (logged).
- **Wine tests (0.6.7, stand-in packs of solid-colour pictures):** three of each plus one wrong-sized of each:
  both rotate with no repeats, the bad ones left out, `JordanHeadshot=2` pinned; face only: face set, both
  pictures default; face and one headshot: action shot default; one `MJACT2.BMP` only: default face and headshot,
  that action shot used; the earlier real-format pack (one of each plus speech): as before; no `mj.dat`: all
  defaults. Roster fields checked in all three copies each time.
- **Redirection (game files never changed):** the two pictures are extracted to `nba98data\art\`, and the
  game's opens of them (read-only and once read/write) go there. Archive copies are built on the game's first
  read, then rebuilt when the original (size, time) or `mj.dat` (CRC) changes (stamp file `<copy>.txt`):
  - `nba98data\art\PLRDATA.VIV`: + `0MJORDAN.FSH`, inserted in EA's order (names compared in upper case).
  - `nba98data\speech\gamespch\PLYRHDRS.VIV`, `PLYRDATA.VIV` and the same pair in `playspch`: + `mijorda.hdr`
    / `mijorda.dat` appended at the end. EA's speech banks are not sorted, and the lookup (0x53CA8C) walks the
    whole directory. Opens are matched by file name and parent folder (both banks use the same file names).
  - All copies keep every original entry byte for byte. They are written as EA writes BIGF: big-endian sizes,
    with the header-size field and every entry 4-byte aligned.
- **Tests:**
  - Wine, full pack: all five roster fields set in all three copies. The playspch copies built in the match
    were byte-identical to the hand-built manual-install banks. The gamespch pair was built by the same C code
    in a test harness, and was also byte-identical to the manual-install copies (the All-Star exhibition does
    not open the arena bank).
  - Wine, art-only pack, truncated `MIJORDA.DAT`, or `JordanSpeech=0`: art on, REJORDA. No pack: all defaults.
  - Still to hear on Windows: the arena announcer saying "Michael Jordan" in a Chicago home game.

## Our settings screens (0.6.2, roadmap item 30)

`nba98core/optmenu.inc`, `[nba98] OptionsMenu=1` (default). An "Enhanced Options" button on the main menu (Game
Setup), above Load, opens our own screens. They are built with the game's own UI code, so they look and behave
like its menus: banner, headings, buttons, the line drawing, the back arrow, Esc.

- **Layouts:** the main menu's `GSETUP.LAY` with one more button object (`NBA98Options`, x 330, y 347), and one
  layout per page (`NBAOPT<n>.LAY`), are written to `nba98data\menu\` at start-up. The CreateFileA hook serves
  them by file name. The game's own layout is never changed.
- **Button:** the call to Game Setup init (0x49D1E4) at 0x4C428E goes through a thunk that adds our button
  with the game's add-button call (0x4C81C0: EAX screen, EDX object name, EBX text, ECX id, four stack arguments,
  callee pops), id 0x2F0.
- **Click:** in Game Setup's event handler an unknown id ends at 0x49DD1F (`jne 0x49DBD3`). That jump now goes
  to our stub: id 0x2F0 sets the screen result (byte at [esp+0x128]) to 0x1A; any other id carries on as before.
- **Screen:** the front-end dispatcher (0x4C3F78) jumps through a 0x49-entry table at 0x4C3E38 by screen id
  (current 0x5C8284, previous 0x5C8285, history 0x61CEAC, keep-running flag 0x5C8286). Entry 0x1A was unused; it
  now runs our screen and does the same bookkeeping as the Rules/Options entry, then returns to Game Setup (4).
- **Pages:** a stack of pages in `k_pages` (root: Display, Gameplay, Controls, Sound, User Interface, Back; each sub
  page has a heading and Back). Each page is opened, run and closed with the same game calls as Rules/Options' set-up and Game
  Setup's run loop (fade, ticker, line drawing `gsetup.lin`, event poll 0x4585E4). Events: our button ids
  0x2E0 + n, 0x0D back arrow / Esc, 0x0C forward arrow (treated as back for now).
- **Wine test:** the button shows on the main menu. Root -> Display -> Back -> Controls -> back arrow -> back
  arrow returned to the main menu, which still worked, and an exhibition match then ran normally.
- **Main menu line drawing (0.6.3):** the vertical line that rises from the bottom to the Load ... Rosters dots
  now reaches our button too, with an orange dot. `LINEMAP.VIV` (EA compact archive: C0 FB, u16 BE directory end
  - 4, u16 BE count, 3-byte BE offset and size per entry, data 4-byte aligned) is copied to `nba98data\menu\` with
  `gsetup.lin` changed and served by name. A `.lin` is RefPack, 10100 bytes: 100 records of 101 bytes (0xFF =
  unused). Record: +0 direction, +1/+3 start x/y, +5/+7 x/y, +9 u16 length, +12 speed, dots from +14 (x, y, flag;
  5 bytes each), +64 u16 dot count (twice). Record 2 of gsetup.lin: length 108 -> 127 (top at y 352), dot
  (320, 352) added before the four. Stored with literal-only RefPack; every other entry is copied byte for byte.
  The file is only changed when the unpacked `gsetup.lin` is exactly the v1.1 English one: 10100 bytes, CRC32
  `a44f4b84` (0.6.5; before that the check compared the record's original bytes, which kept EA data in the
  source). Any other file leaves the main menu's lines as they are.
- **Display page (0.6.4):** three setting rows, the same way the game's Rules & Options rows work: each reads
  "Name: value" and moves to the next value each time it is chosen. The new value is written to `nba98.ini` at
  once and every setting row's text is changed with the game's set-button-text call (0x4C842C: EAX screen, EDX
  object name, EBX text, ECX 0, two stack arguments, callee pops). The ini writer keeps all comments: it changes
  an existing `key=` line, else makes the commented `;key=` example line active, else adds the line at the end of
  the section. A note under the rows says the changes take effect the next time the game starts.
  - Resolution: `[glide] Width/Height`. Auto (0/0, shown with the size it means: the 4:3 part of the monitor in
    full screen, 1280x960 in a window), 640x480, 800x600, 1024x768, 1280x960, 1440x1080, 1600x1200, 1920x1440,
    2560x1920, 2880x2160. In a window, sizes taller than the screen are skipped. A size typed into the ini by
    hand is shown as it is, and the next press goes on from the next bigger size in the list.
  - Display Mode: `[glide] Windowed`, Full Screen (0) or Window (1). Full screen is already borderless (a
    window covering the monitor, no exclusive mode).
  - VSync: `[glide] VSync`, Off (0) or On (1).
  - Wine test: Resolution Auto -> 640x480 -> 800x600, Display Mode -> Window, VSync -> On; the ini got
    `Width=800`, `Height=600`, `Windowed=1`, `VSync=1` in place of the commented lines; the values were still
    shown after Back and reopening; the next start opened an 800x600 window.
  If the file is not as expected the lines are left alone.
- **To do:** a line drawing for our own pages (they reuse the main menu's).

## Settings kept across updates, stale nbatemp cleanup (0.7.0)

- **Settings:** releases ship `nba98.ini.default` (made from `common/nba98.ini.in` as before) and never
  `nba98.ini`. The launcher (`make_ini()` in `launcher/launcher.cpp`) copies the default to `nba98.ini` only when
  there is no `nba98.ini` yet (CopyFileA with fail-if-exists), before it reads any setting, and logs which case
  it was. From then on `nba98.ini` is the user's own, including what the Enhanced Options pages write there, and
  copying a new release over an install keeps it. New settings in a later version still work with their built-in
  defaults; `nba98.ini.default` shows them. Deleting `nba98.ini` returns to the defaults. A Main ini kept in a
  Debug install (or the other way round) only differs in its comments: every default comes from the build.
- **nbatemp:** the game keeps its scratch files in `nbatemp\` in the game folder and deletes the folder on a
  normal quit; Alt+F4 or a crash left it behind. `clean_stale_nbatemp()` in `nba98core.cpp` runs in the core's
  setup, before any game code, after the current directory is set to the game folder: it deletes the files
  directly in `nbatemp\` (clearing read-only), then the folder. Subfolders (the game makes none) and a
  `nbatemp` that is not a plain folder (a link) are left alone. A named mutex per game folder
  (`NBA98_GAME_FOLDER_<FNV-1a of the upper-case folder path>`, held for the life of the process) means a
  second copy started from the same folder never empties the folder of one already running.
  `[nba98] CleanTempFolder=1` (default), 0 = off.
- Wine tests (Debug): fresh folder with a stale `nbatemp\` of three files (one read-only): `nba98.ini` created
  byte-identical to the default, folder removed. Second start with an edited `nba98.ini` and a changed default:
  `nba98.ini` unchanged. A subfolder in `nbatemp\`: files deleted, folder kept. `CleanTempFolder=0`: left alone.
  A second copy started while the first was running: the first copy's file in `nbatemp\` kept.

## Restart pop-up and auto-restart (0.7.1)

- **When:** most of our settings only take effect when the game starts (all of Display and Gameplay, and the
  Jordan rows on User Interface; Menu Player and Menu Colour apply on the next screen and never ask). When the
  player leaves a page (the Back row, Esc or the back arrow, the forward arrow) after changing one of those on
  that page, and the settings now differ from the ones the game started with, the game's own Yes/No pop-up asks
  "Restart now to apply the changes?". Changing a setting and then changing it back asks nothing. The values the
  game started with are taken as row texts at start-up (`opt_restart_snapshot()`), so the comparison is exactly
  what the rows show.
- **The pop-up:** Game Setup's quit question (on Esc, 0x49DD74) calls 0x4E9C48, a variadic Watcom function (every
  argument on the stack, caller pops 13 dwords): 0, box colour (0xFFFF50FF through 0x525C5C), x 320, y 240, 0, 0,
  0, font 0x5C6509, text colour (white), 0, the number of lines, then the lines (Game Setup passes
  TXT_ARE_YOU_SURE_YOU and TXT_WANT_TO_QUIT? through the text lookup 0x4C3B40, which returns plain text unchanged).
  It returns the event: 0x0C for the green (Yes) button, else No. Before and after it the page's buttons are
  switched off and on with 0x4C9558 (EBX 0 / 1). The pop-up shows one of the game's 110 cheerleader pictures
  (`art\frontend\foregrnd\popup\CHEERnnn.FSH`, picked at random), as the quit question does.
- **Yes:** the core writes `nba98data\restart.req`, then quits the way Game Setup's quit does: 0x4DA6A8 (EAX 0,
  EDX 3; works in the `saves` folder) and the next screen 6 instead of 5. Screen 5 is the credits roll shown on
  a normal quit (credits.lay); screen 6 (0x4C43D0) runs the game's shutdown calls and clears the keep-running
  flag 0x5C8286. The game then exits normally (saves, desktop back, nbatemp deleted).
- **Launcher:** both builds now wait for the game to close (no window, no CPU). If `nba98data\restart.req` exists
  and the exit code is 0, the launcher deletes it, logs "restart requested from the settings screens" and starts
  the game again with the same arguments. After a crash (any other exit code) it deletes the request and does not
  restart. A stale request is deleted before the first start. The launcher sets `NBA98_RESTART_OK=1` in the game's
  environment; without it (the game started some other way) the question is never asked and the page notes say
  the changes apply at the next start.
- **Wine tests (Debug, full front end, 1920x1080 Xvfb, SDL_AUDIODRIVER=dummy):** Display, VSync Off -> On, Back:
  the pop-up showed "Restart now to apply the changes?"; green: the game exited with code 0, the launcher started
  it again, the new session ran with VSync=1 and found no stale nbatemp. Then VSync On -> Off, Back, red: the page
  closed normally, no request, no restart. VSync back to On (the running value), Back: no question. A request
  placed by hand followed by `CrashTest=40`: "restart requested but the game did not exit cleanly (0xc0000005): not
  restarted", request deleted.

## CRT filter (0.7.2)

`[glide] CRT=off|auto|sharp|soft` (default off), also the CRT Filter row on the Display page, where it applies
at once (no restart question). Ports of two DOSBox Staging shaders (`resources/shaders/crt/crt-hyllian.glsl` and
`vga-1080p.glsl`, MIT; credits and changes in `glide2x/gl_shaders.h`, licence in
`third_party/CRT-SHADERS-LICENSE.txt`, which `build.sh` copies into every release).

- **Passes** (`crt_present()` in `gl_d3d.cpp`, before Present): `ps_crt_down` reduces the rendered frame to the
  game's own 640x480 lines (4 bilinear taps per texel, with the game's gamma) into a 640x480 texture; then
  `ps_crt_hyllian` or `ps_crt_vga` draws it to the screen. The high internal 3D resolution is given up while the
  filter is on, as on a real CRT.
- **Presets:** Auto picks as DOSBox does by screen height: crt-hyllian with its file defaults (Hermite filter,
  scanline strength 0.85, beam width 0.95 to 1.30, colour boost 2.5) when the picture is at least 3x 480 lines
  tall (1440 and up, 4K); below that vga-1080p, drawn at exactly 2 screen lines per game line (1280x960, centred;
  pictures under 960 lines use crt-hyllian). Sharp: crt-hyllian, Catmull-Rom filter, beam profile 1. Soft:
  crt-hyllian, Hermite, beam profile 2. Phosphor layout 2 (2x2 shadow mask), mask 0.55 for crt-hyllian, as
  in the files.
- **Live switching:** the shared stats struct (version 4) gains `crt_mode` (-1 = not set). The options menu
  writes the ini and sets `crt_mode`; glide2x reads it every frame and falls back to its ini value.
- **Mouse:** `G.pic_*` holds the rectangle actually drawn (smaller for the VGA preset); the mouse mapping and
  ClientToScreen use it, so the game's cursor stays under the pointer.
- **Wine tests (Debug, 1920x1080):** CRT=auto at start: "vga-1080p (2 lines per game line), picture 1280x960 at
  320,60", scanlines and mask visible; menu clicks land where expected. On the Display page CRT Filter Auto ->
  Sharp -> Soft -> Off changed the picture at once each time (crt-hyllian 1440x1080 at 240,0, then off); Back:
  no restart question. The 4K path is the same crt-hyllian shader with the file's defaults; how it looks needs
  Jason's screen (Wine renders in software).

## Controls and Sound pages (0.7.3)

Two more pages in Enhanced Options (root now: Display, Gameplay, Controls, Sound, User Interface, Back). Rows
work like the other setting rows: each press moves to the next value and writes it to `nba98.ini` at once.

- **Controls** (`[input]`):
  - Gamepads: On (SDL3 gamepads) or Off (Windows joysticks used directly). `Gamepads=1/0`. Next start.
  - Dead Zone: 10, 15, 20 (default), 25, 30 %. `Deadzone`. Applies at once.
  - Stick: 8-Way (default) or Raw. `Stick=8way/raw`. Applies at once.
  - Pad Layout: PlayStation (default) or PC. `PadLayout=ps1/pc`. Next start (it sets the game's joystick
    defaults when the game loads; controllers you customised are never changed).
- **Sound** (`[sound]`):
  - Audio Latency: 20, 40 (default), 60, 80 ms. `LatencyMs`. Next start (eacsnd.dll reads it when it loads).
- Dead Zone and Stick apply at once because the pad code reads both on every poll: `pad_reload_live()` in
  `pad_input.inc` re-reads them when the row changes (logged as `gamepads: dead zone N%, stick ...`). The other
  three rows count as restart settings, so 0.7.1's restart pop-up covers them.
- A value typed into the ini by hand (say `Deadzone=12`) is shown as it is; the next press goes on from the next
  bigger value in the list.

## Gamepad menu navigation (0.7.4, roadmap item 13)

`nba98core/pad_menu.inc`, `[input] PadMenus=1` (default), also the Menu Navigation row on the Controls page
(applies at once). The D-pad or left stick moves a dotted grey box between the buttons of any menu screen
(the game's own, our pages and the Yes/No pop-ups); A (Cross) chooses, B (Circle) goes back (Esc, also No on
a pop-up). Moving the mouse hides the box; the next pad press brings it back.

- **Hook:** the UI event poll 0x4585E4 is called once per frame by every menu loop. Its first five bytes
  (five one-byte pushes) are replaced by a jump to a stub that runs `pad_menu_tick()` and then the pushes.
- **Widgets:** 35 pointers at 0x5F9258. Live: dword 'DOOG' at +0 and +0x7C. +0x1F word id (-1 none),
  +0x1B/+0x1D position, +0x43 active, +0x45 radio group, +0x46 chosen in its group, +0x69 bit 8 never
  hovered. Size from +0x53/+0x55 when +0x48 is set (centred), else from the art at +0x25 (size +4/+6, hot spot
  +8/+0xA), as in the hit test 0x456684. Ids 0x0C/0x0D are the forward and back arrows.
- **Moving:** nearest object in the pressed direction: distance along it plus twice the sideways gap between
  the rectangles, inside a cone (gap at most twice the distance). Held directions repeat after 400 ms, every
  150 ms. A screen starts on the object under the mouse, else its first button (creation order).
- **Choosing:** the selected object's centre goes into the raw mouse position (x 0x62A30C, y 0x62A308; the
  menus add the screen offset 0x5F389C >> 16), so the game shows its own hover look; A sets the left button
  bit in 0x62A304 for at least three menu frames, so the click is an ordinary mouse click.
- **Back:** B sets Esc the way the game's own joystick code does: key byte 0x5D7015 and last scan code
  0x62A7CC (1 down, 0x81 up), for at least three frames.
- **The game's joystick menu code (0x48C454)** is skipped while ours is on (jump at its entry, five one-byte
  pushes). It is live in the front end and turns a joystick controller's Shoot binding into Enter and Pass
  into Esc, which with the PlayStation layout made Cross go back.
- **Matches:** nothing is done while the game clock's simulation steps are moving (checked over 300 ms), so
  in-game buttons never reach the menu code; pause menus work.
- **Box:** `nav_box` / `nav_time` in the shared stats (version 5); glide2x (`nav_draw`, shader `ps_navbox`)
  draws it after the picture, one game pixel wide with dashes three times that, mapped through the drawn
  picture (CRT presets included), and hides it once it is 250 ms old.
- **Debug:** the core log lists each screen's objects once (slot, id, position, flags) and every selection,
  A and B.
- **Not covered yet:** sliders (volume) and grids (team and player lists) are reached as one object and
  clicked in the middle; text fields need the keyboard.
- **Wine test (virtual pad):** main menu box and hover look on Enhanced Options; A opened Enhanced Options;
  Down to Controls, A; Dead Zone changed with A and saved; B back twice to the main menu; A on Season chose it;
  B opened the quit question with the box on No; B closed it and the game carried on.

## Long match judder: step lock on input frames (0.7.6)

The step lock (`[glide] StepLock=1`) now waits for the game's input frames instead of raw clock ticks.

- **Cause (Jason's 0.6.9 logs, 32 minutes):** stretches of frames showing 0, 1 or 2 steps at random (up to 55 s
  long, longer as the session went on) while frame times stayed at 16.7 ms. The lock counted 2 ticks per frame
  from whatever tick was current when it (re)started (match start, dead balls, late frames). The game makes an
  input frame every other tick, so half the restarts released on the tick in between; with the display lock
  holding the present at mid-refresh, the frame-making tick then fell on the vblank and tick jitter decided
  which frame the game saw. Details: nba98-smoothness ("Long match judder").
- **Counter (nba98core, timing.inc):** the input task 0x42E4A0 counts ticks in 0x5C6034 and resets it to 0 when it
  samples (every 2 ticks; 4 in the CPU v CPU 30 Hz mode). Its registration (0x42E0F5) and removal (0x42E101) both
  load the task pointer with `mov eax, imm32`; both now load our wrapper, which runs the task and adds one to
  stats `input_frames` when 0x5C6034 is 0 afterwards (a ring-full or stopped sample leaves it above 0). It runs
  inside the timer callback, before `ticks_done` and `tick_event`.
- **Lock (glide2x `step_lock`):** waits until `input_frames` has moved by 1 (2 for StepLock=2) since the last
  swap, so it always releases just after the tick that made a frame. Without the counter it counts ticks as
  before. Stats struct version 7.
- **timing.log:** a new `inp` column (input frames per second) next to `steps`.
- **0.7.7 fix:** the wait for the next input frame now has a 50 ms limit in all. The tick event is set on every
  tick, so the old per-wait 50 ms timeout never fired while the clock ran: when an auto replay started, the game
  stopped making input frames with the clock still running and the lock waited for ever (Jason's first 0.7.6 test,
  2026-10-31: replay, main thread waiting, no frames, ticks still 120/s).
- **Wine test:** human v CPU exhibition, input frames 60.0/s against 59 to 61 steps/s, match runs normally.
  Wine draws this match at about 16 fps, so the pacing itself can only be judged on Windows: a 20+ minute match,
  where the steps/frame column should stay 1 for every second of live play.

## Widescreen matches, Hor+ (0.7.5, experimental, roadmap item 1)

`[glide] Widescreen=1` (default 0), also the Widescreen row on the Display page (next start). On a screen
wider than 4:3, matches show more of the court at the sides with the same height; the HUD, the menus and the
front end stay 4:3 in the middle (black bars beside the front end). Plan and background: the phase 2
widescreen doc.

- **Camera (nba98core, `widescreen.inc`):** the match viewport setup 0x42C7E0 (jump at its entry, six bytes)
  first writes the horizontal half-angle of view at 0x5A1098 (original 1/15 turn, 48 degree view), with
  tan(new) = tan(24 degrees) x aspect / (4/3), so the vertical view is unchanged (16:9: 61.4 degrees). The
  aspect comes from glide2x (stats `ws_aspect`, 0 = off). Culling, clipping and projection all follow.
- **3D or 2D (nba98core):** the match frame renders the world in 0x42D118 and the overlays after it. The two
  calls of 0x42D118 (0x42CB2C, 0x42648C) go through a thunk that sets stats `draw_2d` when the world is done;
  glide2x clears it at each buffer swap.
- **glide2x:** the render target has the window's shape (same height as before); the game's 640x480 screen
  is the 4:3 area in the middle (`c43_x`, `c43_w`). Each draw picks its layer (`d3d_ws_layer`): full width
  for match 3D (buffer swaps in the last half second, `draw_2d` not set, no CRT filter), otherwise the 4:3
  area. Vertices, clip windows and clears follow the layer. LFB uploads and readbacks use the 4:3 area only,
  so a menu over a match keeps the full-width picture beside it; outside matches the bars are cleared black.
  The mouse and the menu box are mapped to the 4:3 area.
- **CRT filter:** (0.7.5) its picture was 4:3, so with a CRT preset on, matches were drawn 4:3. Since 0.7.9 the
  CRT picture is widescreen too (section 0.7.9).
- **Stats struct version 6** (`ws_aspect`, `draw_2d`, `ws_fov`).
- **Known limits of this first version:** player markers and names drawn as 2D at 3D positions are part of
  the world render, so they are stretched with it; the front-end team logos stay 4:3; net play not yet
  checked (the camera constant must not affect game logic).
- **Wine test (854x480 screen, 16:9, CPU v CPU exhibition):** camera 61.4 degrees; the arena, court and players
  fill the full width with the right proportions; the shot clock and the end-of-quarter score panel are drawn
  4:3 in the middle at their own size, with the 3D arena scoreboard full width behind; the front end stays 4:3
  with black bars; menu clicks and the pad box still land.

## Player distance detail (0.7.8, roadmap item 51)

`[nba98] DistanceDetail=auto` (default; also `original`, `full`), also the Distance Detail row on the Display
page (applies at once). Findings: the player LOD and distance detail docs.

- **What the game does:** each player's uniform texture (256, 128, 64, 32 pixels, composited per player at
  match load) and body model (HIGH, MAIN, MED.BIN, with reflection and shadow quality) are picked from his
  camera depth with limits made for 640x480: uniforms at 850, 1700, 3400 (floats 0x5AABAC..0x5AABB4),
  models at 700, 1400, 2800 (immediates of `cmp edx, imm32` at 0x4B8DB9, 0x4B8DC1, 0x4B8DC9). In live play
  players sit at depth 1,000 to 4,000, so nobody had the 256 uniform and wide shots used the 32 one.
- **Change (nba98core, `player_lod.inc`):** all six limits are scaled. `auto` = picture height / 480 from
  glide2x (stats `render_h`, 480 with the CRT filter, so x1 there): x4.5 at 2160, which puts every live-play
  player on the 256 uniform and HIGH.BIN; `full` = every player always on the top tier; `original` = as
  shipped. Applied on the game thread at start, at every match viewport setup (the 0x42C7E0 hook from 0.7.5)
  and from the menu. Rendering only. Stats struct version 8 (`render_h`).
- **timing.log:** in a match each line ends with `uniforms 256:a 128:b 64:c 32:d, 256 slots n/10` (players
  per uniform size, and how many of the game's ten 256 slots it was granted at load; fewer than 10 means
  texture memory ran short and near players share). nba98core.log notes the slot count when it changes.
- **Not changed:** draw distance (far plane 20000, nothing in the arena is ever further than about 11,000)
  and arena textures (one detail level, no distance switching).

## 0.7.9: Jordan's in-match portrait, All-Star jerseys, CRT filter in widescreen

From Jason's 0.7.7 full match (2026-11-01).

- **Jordan's portrait in matches (bug):** the foul and free throw panels do not use the headshot (L_PORT) but
  the `port` picture inside the player's 3D face package (85x96, 8-bit type 0x7B, 256-entry BGRA palette type
  0x2A, index 255 see-through). Jordan's came from 0MJORDAN.FSH in mj.dat, so it ignored the headshot picked
  for the menus. `jordan_art.inc` now builds a copy of the face package whose `port` is made from the headshot
  in use (area-averaged to 85x96 from the premultiplied 32-bit BMP, alpha below half see-through, 255 colours
  by median cut) and puts that copy in nba98data\art\PLRDATA.VIV. The archive stamp now includes the added
  entry's CRC, so a new pick rebuilds it. With no face in mj.dat the default face's portrait stays (logged).
- **All-Star jerseys (`allstar_fix.inc`, `[nba98] AllStarJerseys=1`, Gameplay page, next start):** in an
  All-Star game each player wears his club's jersey (the All-Star packs x1/x2 in TEAMDATA.VIV hold only
  shoes); the club is the All-Star record's ORIGTEAM, which EA set to the 1997-98 club. Set to the club each
  played the 9 February 1997 game for: Kemp (406) SuperSonics, Brandon (407) Cavaliers, Vin Baker (410)
  Bucks, Chris Gatling (386) Mavericks. Every other All-Star record already matches the NBA's 1997 rosters
  (Barkley a Rocket since August 1996; the Bullets are the Wizards' jersey). Numbers are left as EA set them.
  Each record is changed only if its PLAYERID and NAME match, in the three roster copies in nba98data; 0 puts
  EA's values back. Needs the roster copy (CutPlayers or Jordan on).
- **CRT filter with widescreen (glide2x):** the CRT source is now as wide as the picture (480 lines; 854x480
  at 16:9) instead of the 4:3 area, the match keeps its full-width 3D and wide camera under the filter, and
  the menus stay 4:3 in the middle of it. The shaders take the source width in `u0.w` (0 = 640) and the
  reduce pass in `color.x`. VGA mode (2 lines per game line) is used only if twice the source width fits.
- **CRT filter and distance detail:** stats `render_h` is now the height the 3D is drawn at even with the CRT
  filter on, so Auto keeps the scaled limits (the CRT picture is made from the full-size 3D).

## 0.7.10: log folders per start, PC report, judder log (Debug)

For the judder Jason still sees after 0.7.9 (report 4 in the test schedule): the logs did not record what
the game, the camera or the animations do frame by frame.

- **Log folders (`common/nba98_build.h`):** every start of the game writes its logs to its own folder,
  `logs\<YYYY-MM-DD_HHMMSS>_<version>_<Main|Debug>_<computer name>\`, so old logs never need deleting;
  `logs\latest.txt` names the newest. The name travels in the environment variable `NBA98_LOG_SESSION`: the
  launcher sets a new one before each game start (a restart from the settings screens gets its own folder;
  launcher.log stays in the first and says where each start logs), and a game started without the launcher
  makes its own. Draw traces and frame pictures go there too.
- **PC report (`sysinfo.inc`):** nba98core.log now also has the computer name, mains or battery (charge, battery
  saver) and the install folder's drive type (local, network, removable); timing.log's machine line starts with
  the computer name and drive type.
- **Judder log (`glide2x/gl_judder.inc`, `[glide] JudderLog=1`, Debug):** `judder.log`, one line a second while a
  match runs and one line for every uneven frame (longer than 20.5 ms, or showing 0 or 2+ steps):
  - each frame's time split into the game's own work (end of our last present to its next buffer swap), our
    last batch, the step lock or pacing wait and the present (vsync, CRT pass);
  - the game's main-thread file calls inside the frame (nba98core times them; stats `io_main_us`, `io_main_n`);
  - the camera (eye 0x60D7D8 and rotation 0x60D7E4, read at each swap), the players (object table 0x5E60FC,
    players 0 to 9, world position at +8/+0xC/+0x10) and each player's drawn shape (the triangles the player
    mesh draw, rend3dfx caller 0x55C270, makes with his uniform texture: count and box), counted per second as
    frames in which each moved or changed, and the shape changes in frames with the camera still (those come
    only from the animation). Players moving every frame while their shapes change in half or a quarter of the
    camera-still frames would mean 30 or 15 Hz animations.
  - Game memory is only read with the verified v1.1 exe (stats `game_v11`). Stats struct version 9.
- **Wine check (854x480):** session folder `2026-11-01_153650_0.7.10_Debug_VM` with every log, the PC lines,
  and judder.log lines in the match (Wine shows about 5 steps a frame, so animation rates need Jason's PC).

## 0.7.11: speech in memory, full-resolution pause and replay pictures, widescreen from the first frame

From Jason's 0.7.10 tests (reports 5 and 6 in the test schedule).

- **Speech banks in memory (`nba98core/spchmem.inc`, `[nba98] SpeechInMemory=1`):** the judder log showed the
  late live-play frames are the game loading a commentary line on its main thread (about 17 file calls on
  `speech\*\*DATA.VIV` / `*HDRS.VIV`, 4 to 63 ms from a network drive), more of them near the end of each
  quarter. A background thread (idle priority, from 3 s after start) reads every speech bank into memory once
  (the Jordan speech copies in `nba98data\speech\` instead of the originals they replace; about 100 MB with the
  pack, 140 MB without). From then on an open of a bank returns the handle of an unnamed event (unique, closes
  normally) and read, seek, size, type, information, time and duplicate calls on it are answered from memory, so
  a commentary line costs no disk or network time. Opens for writing, overlapped opens and banks not loaded yet
  go to the real file. nba98core.log reports the banks loaded and, every minute, the opens and reads served.
- **Full-resolution pause and replay pictures (`glide2x`, `[glide] FullResPause=1`):** when the game freezes a
  match frame (pause, Instant Replay screen) it reads the frame back through the LFB at 640x480, draws on it
  and writes it back, and glide2x used to show that 640x480 copy over the 4:3 area. Now, when the game reads
  back a frame of a running match (a buffer swap in the last second), glide2x keeps the full-resolution target
  and the 640x480 picture it handed over. When the game writes a picture back, each 640x480 pixel is compared
  with the one handed over: unchanged keeps the full-resolution pixels, all three channels scaled down by the
  same factor (the darkened background) darkens the full-resolution pixels by that factor, anything else (menus,
  text, replay controls) is the game's pixel (shader `ps_lfbm`, the pixel and its mode packed in an R32_UINT
  texture). A picture with more than half of its pixels new is a new picture: uploaded as before, and the kept
  frame dropped. Front-end pictures are never compared (no swaps).
- **Widescreen from the first match frame:** at the start of a match the game draws its first frames before
  its first buffer swap of the match, and glide2x only treated 3D as match 3D after a swap in the last half
  second, so the first frame (shown for about 0.8 s on Jason's PC, Wine probe: 11,595 draws in the 4:3 area
  with the wide camera) came out in the 4:3 area. nba98core now sets stats `match_3d` at the match camera
  setup (0x42C7E0, not the call at 0x48D0FB) and before each world render; glide2x draws full width while it
  is set and clears it at each swap and at LFB uploads outside matches. Stats struct version 10.
- Debug probes: `ws probe` lines in nba98core.log (viewport setups, projection at the first world renders) and
  glide2x.log (draws per layer for the first swaps after a gap); `LFB read/write` lines for the kept frame.
- **Wine check (854x480, widescreen, Jordan pack):** 20 speech banks (101 MB) in memory 1 to 10 s after start;
  in the match 70 bank opens and 272 reads served from memory, none from the disk, and the per-second file time
  in live play at most 6 ms in all. First match frames full width (probe: 11,465 full-width draws in the first
  frame, only the HUD in the 4:3 area). Pause menu, Instant Replay screen, Done and Resume: the game's menu and
  replay controls drawn over the kept frame (252,700 of 307,200 pixels kept), live play carries on after.

## 0.7.12 and 0.7.13: screenshots, pad skips intros and auto replays, R3 CRT filter, skip start-up and exit screens

From Jason's 0.7.11 tests (report 7) and requests (2026-11-04).

- **Screenshots (`glide2x/gl_shot.inc`, `[glide] Screenshots=1`, both builds):** Page Down saves the picture on
  the screen (as it is about to be shown: CRT filter, widescreen, overlay and pad box included) to
  `screenshots\NBA98_<date>_<time>.png` in the game folder. The window procedure counts the presses (the game
  never sees the key); the next present copies the back buffer to a staging texture and a worker thread writes
  the PNG with the Windows Imaging Component (24-bit). glide2x.log names each file.
- **Pad extras (`nba98core/pad_extras.inc`, `[input] PadExtras=1`):** a thread reads the pads 60 times a second; a
  press counts only after the buttons were let go.
  - Start, A, X or B skips the intros as a key does (an Esc press through keybd_event, only while the game's
    window is in front), until the game opens its first menu layout (a `*.lay` file, the main menu's
    `gsetup.lay`; Wine: the first press 4 s into intro.dct ended it at once).
  - Start, A, X or B skips an automatic replay as Esc does. The auto replay loop in 0x426DA8 (taken when the
    high word of 0x5E628A is set) runs a match frame (call 0x42CF14 at 0x426EAF, now marked by a thunk) and then,
    if the Esc key table byte 0x5D7015 is set, waits for it to be let go and ends the replay. While that call ran
    in the last quarter second, a press holds Esc in the key table for 150 ms. The Instant Replay screen
    (0x42645C, taken when the flag is clear) and the pause menu are not touched.
  - The right stick button (R3) switches the CRT filter between Off and Auto anywhere, saved to `[glide] CRT`
    and applied at once (stats `crt_mode`). R3 is not bound in the PlayStation layout.

- **0.7.12 fix (in 0.7.13):** glide2x.dll no longer links ole32 (it is loaded when a screenshot is written).
- **Skip the start-up and exit screens (0.7.13, `nba98core/skip_screens.inc`, User Interface page):** the front end
  is a screen state machine (state byte 0x5C8284, handler table 0x4C3E38; 1 = EA Sports movie and legal screen,
  2 = intro movie, 3 = title screen 1title0.fsh, 4 = Game Setup, 5 = credits, 0x48 = special thanks, 6 = exit).
  - `[nba98] SkipIntros=1` (default): state 1's `mov eax, offset easports.dct` at 0x4C41B3 jumps to our code,
    which clears 0x5C6650 as the movie path does and goes to state 3 the way every handler ends. Applies at the
    next start. Wine: title screen 2.5 s and Game Setup 5.8 s after start (about 16 to 49 s before).
  - `[nba98] SkipCredits=1` (default): Game Setup's quit (0x4DA6A8(0, 3), then `mov byte [esp+0x128], 5` at
    0x49DDFC) asks for state 6 instead of 5 (the byte at 0x49DE03, patched live from the menu), as our restart
    does. Wine: with it on the game exits without opening credits.lay; with it off the credits come up.
  - The User Interface page has two more rows, Skip Intros and Skip Exit Credits (9 rows).

## 0.7.14: pause screens full width

Jason's 0.7.13 screenshots (Ally, widescreen, CRT off): in the pause menu a 4:3 picture overlapped the 16:9 one. The
pause screen does not show a frozen picture: every time it redraws (opening it, its sub-screens such as Defensive
Matchups, coming back to it) the game renders the arena again in 3D, darkened (vertex colours about 25 to 60 of 255),
plus the 3D scoreboard, without a world render (no 0x42D118) and without regular swaps (Wine draw trace, frame 511:
419 runs from the same callers as the live world, 0x55C09B, 0x55B89A/0x55B8B2, 0x55BB04/0x55BB20). glide2x drew
the first of those frames full width (a swap within half a second, or the camera setup's `match_3d`) and every later
one in the 4:3 area, so the later frames were squeezed into the middle over the first one's sides.
- nba98core records the front-end screen state (byte 0x5C8284; 42 in Wine's exhibition) at each world render (stats
  `match_state`, version 11). glide2x draws 3D full width while the screen state is still that state (the HUD and
  the game's LFB pictures stay in the 4:3 area as before), so every pause redraw is full width, like the first.
- Debug: `NBA98_LFB_PROBE=1` in the environment logs every LFB read and write and every swap after a gap, with the
  screen state.

## 0.7.15: compiled as C++ (C++ move, step A)

No change in behaviour. The C sources were renamed (`.cpp` for the compiled files, `.inc` for the #included ones)
and built with g++ in the restricted subset ("C++ rules" above). Only what C++ requires changed:
- `common/nba98_cxx.h` (new): `extern "C"` for each file's own code, `CINTERFACE`, `COBJMACROS`,
  `D3D11_NO_HELPERS`.
- COM interface IDs are passed as references (`IID_ID3D11Texture2D` instead of `&IID_ID3D11Texture2D`; C++'s
  `REFIID` is a reference): `gl_d3d.cpp` (4), `gl_shot.inc` (3), `sysinfo.inc` (2).
- `_Static_assert` is `static_assert` (`timing.inc`).
- `gl_api.cpp` defines `min()` and `max()` again after its includes (C++'s math.h removes the windef.h macros).
- `tools/gen_glide_stubs.py` writes `glide2x/gl_stubs.cpp` with `extern "C"` stubs.
Checks against 0.7.14: export and import tables identical in all four files (names, ordinals, DLLs, functions),
Main file sizes identical, Debug files 1 to 4% larger (C++ debug information).

## 0.7.16: judder after pauses and skipped replays (display queue keeper)

Jason's 0.7.15 test (2026-11-05, JAY-PC, 4K 60 Hz, VSync on): the judder came back after skipping automatic
replays and went away again later. judder.log showed two steady states in live play, both with one step a frame:
- **queue full:** each Present waits about half a refresh (7 to 12 ms) for the frame before it to reach the screen;
  frame intervals 16.5 to 16.8 ms. Smooth.
- **queue empty:** Present returns at once (0.1 ms) and the step lock alone sets when each frame goes out; intervals
  14 to 19 ms, and frames that miss a refresh. Judder.
A long gap (pause, skipped replay) empties the queue and two quick frames fill it; it then stays that way. The
0.7.15 log switched at the 2nd, 4th and 6th replay skips; the 0.7.10 logs spent 622 of 834 live seconds with the queue
empty. Not a C++ change (0.7.10 does the same).
- **First fix, replaced in 0.7.17 (glide2x `queue_keep()`, `[glide] KeepQueue=1`):** in live play (step lock and display
  lock on, VSync on), after 30 frames in a row whose Present did not wait (under 1 ms) the same picture is presented
  once more, so a frame waits in the queue again. One frame shows twice, just after the gap. If presents still do not
  wait after three refills (a screen or driver that never makes them wait, for example variable refresh) the keeper
  stops for the session. glide2x.log: `display queue: ... frame shown once more to fill it (N so far)`.
- Debug `judder.log`: each second ends with `queue empty N/frames, refilled N`, and each refill has its own line.
- `present_rt()` was split: `compose_backbuffer()` draws the picture (CRT pass or plain, menu box), so the refill can
  draw it again.

## 0.7.17: judder fix the right way round (display queue drain)

Jason's 0.7.16 test (2026-11-05): still juddery, constantly; then 0.7.14 for comparison: smooth at the start, judder,
smooth again late in the quarter. The share of live seconds in which each Present waited (a frame in the queue):
0.7.11 on the Ally 0% (no judder), 0.7.10 13 to 25%, 0.7.15 72% and 0.7.14 68% (judder that came and went with it),
0.7.16 98% (the 0.7.16 keeper put a frame in the queue on purpose: judder all the time). So the judder is the state
with a frame waiting, not the empty one; 0.7.16 had it the wrong way round.
- **Fix (glide2x `queue_drain()` in `gl_d3d.cpp`, `[glide] EmptyQueue=1`, default; the 0.7.16 `KeepQueue` keeper is
  removed):** in live play (step lock and display lock on, VSync on), after 30 frames in a row whose Present waited
  (2 ms or more), glide2x waits for one more vertical blank (`IDXGIOutput::WaitForVBlank`) before the next frame, so
  the waiting frame reaches the screen and the queue is empty again. No frame is shown twice. Stops for the session if
  presents still wait after three drains. glide2x.log: `display queue: a frame waited for 30 frames ... waited one
  refresh to empty it (N so far)`.
- Debug `judder.log`: each second ends with `queue full N/frames, drained N`; each drain has its own line.

## 0.7.18: what reaches the screen (presentation statistics); 0.7.16 and 0.7.17 code removed

Jason's 0.7.17 test (2026-11-05): judder in places, smoother overall, but the frame time graph had more spikes. The
drain did not hold: in three stretches after replays the waiting frame came straight back after every drain (16 drains
in 8 s in the first), and each drain cost a 33 ms frame (28% of live seconds had a late frame). So neither keeping a
frame waiting (0.7.16) nor draining it (0.7.17) is the answer, and both guessed at what the screen did.
- **Removed:** the 0.7.16 keeper and the 0.7.17 drain (`KeepQueue`, `EmptyQueue`). Presenting is as in 0.7.15.
- **Added (glide2x `present_stats()` in `gl_d3d.cpp`, no change to drawing or timing):** after each Present the swap
  chain's frame statistics (`IDXGISwapChainMedia::GetFrameStatisticsMedia`, Windows 8.1 and later; else
  `IDXGISwapChain::GetFrameStatistics`) give the last present that reached the screen, the refresh it went out on, and
  how it was shown: composed by DWM, an overlay plane, or direct.
  - glide2x.log (both builds): `display: frames shown <mode> (was <mode>)` when the mode changes (first 100 changes).
  - Debug judder.log, per second: `screen: shown N, held 2+ N, never shown N, queued max N, <mode>`: frames that
    reached the screen, frames that stayed up for 2 or more refreshes (a repeat), frames never shown, and the most
    presents waiting behind the screen.

## 0.7.20: first public release fixes

Release branch `release-0.7` from `main` (0.7.19); no controller menus code (public release plan, R1).
- **INSTALL.DAT inside the game folder:** the generated copy is now `nba98data\INSTALL.DAT`, not
  `%TEMP%\NBALive98\INSTALL.DAT`, so a session writes nothing outside the game folder. A copy left in `%TEMP%` by
  an older build is deleted at start (`INSTALL.DAT: removed the old copy` in nba98core.log).
- **Roster check:** `nba98data\` can now exist with only INSTALL.DAT in it (roster additions off). A build counts as
  incomplete only when `nba98data\dbdata` exists without `roster.txt`, so switching the roster additions on later
  still builds the database.
- **Widescreen marked experimental:** the Display page row reads `Widescreen: Off (Experimental)` /
  `On (Experimental)`, and the ini note says the end-of-quarter stat screens stay 4:3 and not to use it in network games.

## 0.7.19: judder fix, third try (skip one present when frames wait in the queue)

Jason's 0.7.18 test (2026-11-05, judder about 2:20, better about 0:50 until another replay, better late in the
quarter). The presentation statistics (`IDXGISwapChainMedia`): the game was shown on an overlay plane the whole time,
and every frame reached the screen exactly once (none held, none lost) in both states. The judder matched the state
in which every Present blocked 7 to 14 ms (a frame waiting): 48 to 127 s, 137 to 194 s and 219 to 247 s of the
session; Present returned at once at 127 to 135 s, 195 to 208 s and from 247 s (the better stretches). While Present
blocks, the game's next frame starts wherever the block ends, and that drifts against the game's clock (the block went
from 13.7 ms to 6.7 ms and back over seconds). 0.7.17's one-refresh wait did not hold because the game then sent two
quick frames to catch up and refilled the queue.
- **Fix (glide2x `queue_watch()` and `present_rt()` in `gl_d3d.cpp`, `[glide] QueueSkip=1`, default):** in live play
  (step lock and display lock on, VSync on), after 30 frames in a row whose Present blocked (2 ms or more), the next
  finished frame is not presented: the waiting frame goes on screen and the queue is clear. One game step is never
  shown. Stops for the session if presents still block after three skips in a row. glide2x.log: `display queue:
  Present blocked for 30 frames ..., one frame not shown to clear it (N so far)`; judder.log `queue full N/frames,
  skipped N` and a line per skip.

## Layout
- `tools/` - generators and analysis scripts.
- `glide2x_log/` - Phase 0 tool: logging stand-in for glide2x.dll.

## glide2x_log

Sits between the game and the real Glide DLL and forwards every call unchanged.
Writes `glide2x_log.txt` next to itself with:
- how long after game start the Glide DLL was loaded
- every call to the setup functions (grGlideInit, grSstQueryBoards, grSstWinOpen, ...)
- the first 20 calls of every other Glide function, with arguments and the exe
  address that made the call
- frame interval stats (min/avg/max) every 300 buffer swaps, for the frame pacing problem
- call counts per function at exit

### Using it (use a copy of the install, not your working one)
1. Copy `Original Files` to a test folder.
2. In the test folder, rename `Glide2x.dll` (dgVoodoo's) to `glide2x_real.dll`.
3. Copy `glide2x_log/glide2x.dll` into the test folder.
4. Run `nba98.exe -3dfx` as normal.
5. Wait at the main menu for 10 seconds, then start an exhibition match, play a minute, quit to the menu, exit the game.
6. Send back `glide2x_log.txt`.

v2 adds a `TICK` line for every second with activity (swaps, triangles, LFB read/write locks,
lines, texture and palette uploads) and a frame interval histogram every 300 swaps, and
buffers its output so logging no longer slows the game.

v3 adds a stall watchdog. If the game stops making Glide calls for more than 300 ms while
in a 3D scene, it samples where the render thread is stuck every 250 ms (up to 12 samples per
stall) and logs `STALL` lines: the current instruction and likely return addresses as
module+offset (game exe addresses are plain VAs that match Ghidra), then `STALL END` with
the total duration. v3 also exports all 159 Glide 2.60 functions, because the 3dfx splash
DLL (3DfxSpl2.dll) links to glide2x.dll. The first log line reads `glide2x_log v3.4`. v3.1 samples every thread in the game during a stall (the thread that last swapped is marked [RENDER]); v3 sampled only the first thread that swapped, which was the window thread. v3.2 also logs the process CPU affinity, every thread the game creates and every SetThreadPriority call, shows each thread's priority and start address in STALL lines, runs the watchdog at TIME_CRITICAL and logs WATCHDOG STARVED if even it could not run. v3.3 adds per-thread CPU accounting: after each starvation or stall it logs which threads used the CPU (current and base priority, start address; priority 16+ is real-time range), and pins itself in memory because the game loads, unloads and reloads glide2x.dll. v3.4 adds an opt-in fix for the in-game freezes: put `glide2x_log.ini` (with `affinity=game`) next to the DLL and the process gets all CPUs back while only the game's own threads and the WINMM timer thread stay on the single CPU the compatibility shim chose. The freezes were the AMD driver's own worker thread (amdxx32.dll) spinning on that one CPU at time-critical priority.

### Building
Needs mingw-w64 (i686) and Python 3: `glide2x_log/build.sh`.
The thunk table and .def are generated from `glide2x_exports.txt` by `tools/gen_glide_thunks.py`.
`glide2x_log/test/` has a harness and a fake Glide DLL used to check forwarding under Wine.

## Step 1: launcher and core DLL (portable install, no CD, no compatibility shims)

- `launcher/` builds `NBALive98.exe`. It starts the unmodified `NBAWIN.EXE` (v1.1) from its own folder
  with `3dfx`, and loads `nba98core.dll` before any game code runs (process created suspended,
  LoadLibraryA queued as an APC on the main thread). Works the same under Wine/Proton.
- `nba98core/` builds `nba98core.dll`:
  - verifies the exe is the v1.1 build (PE timestamp 0x346B890A and the CD message at 0x5A1148), then
    patches the CD check branch at 0x42ED97 in memory (same change as the known-good no-CD exe)
  - redirects the game's `install.dat` open to a copy in `nba98data\INSTALL.DAT` (in the game folder, since 0.7.20) whose install root is
    the exe's folder; folder lines are copied byte for byte (the parser needs the retail fixed-width columns),
    CD lines become HD lines when the folder exists locally; the original file is never changed
  - clamps GlobalMemoryStatus to 512 MB (replaces the GlobalMemoryStatusLie shim)
  - pins only the game's threads (main thread, threads created by the exe, and the multimedia timer thread
    that runs the game's timer callback 0x5253A4) to one CPU, and gives the process all CPUs
    (replaces SingleProcAffinity without starving the GPU driver)
  - writes `logs\\nba98core.log` (Debug build, or `Log=1`); settings in `nba98.ini` (see `common/nba98.ini.in`)
- `dist/Main` and `dist/Debug` hold the built releases (see Building above).

To use: copy `NBAWIN.EXE` from `Patch v1.1` and all files from `dist/Main` (or `dist/Debug`) into the game folder and start
`NBALive98.exe`. The compatibility shim database (nba98.sdb) only matches `nba98.exe`, so it does not
apply to `NBAWIN.EXE`.

## Step 2: glide2x.dll (Glide 2 on Direct3D 11, replaces dgVoodoo)

- `glide2x/` builds `glide2x.dll` (32-bit): the full Glide 2.60 export list (159 names, so splash or
  overlay DLLs that link to Glide still load), 84 functions implemented, the rest log their first call.
- Rendering: one uber-shader that does the Glide colour/alpha/texture combine units, per-pixel
  perspective-correct texturing, 8-bit paletted textures looked up in the shader, alpha test, chroma key,
  Glide z-buffer and w-buffer depth with per-triangle depth bias, Glide's culling rule. Lines and points are
  drawn as 1-pixel quads.
- Menus (drawn by the CPU through grLfbLock): served from a CPU copy of the 640x480 16-bit screen and
  uploaded once per update, so a read lock never waits for the GPU. This is the fix for the slow
  line drawing menu option.
- Output (step 3): borderless window covering the monitor, the picture scaled to the largest 4:3 area and
  centred. 3D is rendered at that size (e.g. 1440x1080 on a 1080p monitor) unless `[glide] Width/Height`
  are set in `nba98.ini`. Frame pacing at 60 Hz by default (`FrameLimit`), or `VSync=1`.
- Mouse: the game reads the mouse in 640x480 window pixels, so the window is subclassed and mouse messages
  are mapped to the Glide screen; the exe's ClientToScreen and GetClientRect are redirected to match.
- Works with ReShade's dxgi.dll / d3d11.dll and other D3D11 injectors. Under Wine/Proton it runs on
  wined3d or DXVK.
- `glide2x.log` lists the device, output size and anything unusual. `test/gtest.c` is a stand-alone test.
- Bug found during testing: the game's GrHwConfiguration is smaller than the SDK one and its
  draw-triangle pointer sits right behind it in memory, so grSstQueryHardware must not clear the full SDK
  struct (it now writes 40 bytes).

nba98core.dll (Debug build, `nba98core/diag.inc`) also has diagnostics: `CrashLog=1` (default) writes the
exception address and call stack to `nba98core.log` plus a crash report and minidump in `crash\\`; `ThreadDump=N` logs where every thread is every N seconds; `WatchAddr=0x...` logs
every change of one dword in the game.

## eacsnd.dll on SDL3 (replaces DirectSound and the ALchemy dsound.dll)

- The original eacsnd.dll (9 KB, Watcom) is the game's only user of DSOUND.dll: it owns a DirectSound
  buffer, and the mixer lives in the exe. `eacsnd/` rebuilds it with the same six exports
  (`iSNDdllversion_`, `iSNDdirectsetfunctions_`, `iSNDdirectcaps_`, `iSNDdirectstart_`, `iSNDdirectstop_`,
  `iSNDdirectserve_`, Watcom register calling convention) on an SDL3 audio stream. So `dsound.dll`
  (ALchemy) is no longer needed, and Wine/Proton need no DirectSound.
- Like the original, all mixing happens in `serve()`, which the game calls from its multimedia timer
  under its sound lock: the queue is topped up to `LatencyMs` (default 40 ms) with the game's mixer
  (0x57245C), and the game's 100 Hz sound tick (music and commentary sequencing) is called between mix
  chunks at the same sample-accurate spacing as the original. Output format as the game asks
  (22050 Hz stereo 16-bit in practice); SDL converts to the device format.
- Needs `SDL3.dll` (32-bit, SDL 3.2.26, zlib licence) next to the exe; it is in `third_party/` and in both releases.
- `eacsnd.log`: device and format, and per minute the number of serve calls, underruns and the longest
  gap between serve calls (the crackle diagnosis).
- `test/stest.c` plays the exe's part (Watcom-convention mixer and tick) and checks register preservation.

nba98core.dll: `SafeTerminate=1` (default). The game kills its streaming worker thread (entry 0x5340AC)
with TerminateThread when a movie or stream stops. Killed while holding a lock, that lock stays taken and
the game freezes (reproduced under Wine at startup and when skipping the intro). The core DLL now counts
the exe's EnterCriticalSection/LeaveCriticalSection per thread and only ends a thread when it holds none
and is either blocked in a wait (terminated there) or running game code (sent to ExitThread).

nba98core.dll: `KeepDesktopMode=1` (default, 3dfx mode only). Even with Glide, the game creates a DirectDraw
object, takes exclusive full-screen control and switches the monitor to 640x480 16-bit (dgVoodoo used to absorb
this). The core DLL hooks the exe's DirectDrawCreate and patches the DirectDraw interfaces (also IDirectDraw2+
from QueryInterface): SetDisplayMode and RestoreDisplayMode do nothing, SetCooperativeLevel becomes normal.
Found on Jason's first Windows run: the window dropped to 640x480 because the display mode had changed.

## Gamepads (SDL3) with the PlayStation layout

- The game reads joysticks through WINMM (joyGetNumDevs, joyGetDevCapsA, joyGetPosEx) from its polling
  thread (0x4F3174, joysticks 0 to 3). nba98core.dll (`pad_input.inc`) replaces those three calls with SDL3
  gamepads: 4 joystick slots, hot-plug, triggers as buttons 11/12, D-pad and left stick on X/Y, buttons in
  XInput joystick order (1 A/Cross, 2 B/Circle, 3 X/Square, 4 Y/Triangle, 5 LB/L1, 6 RB/R1, 7 Back/Select,
  8 Start, 9 L3, 10 R3, 11 LT/L2, 12 RT/R2), so existing Xbox-pad configs keep their meaning.
- Controller config format (saves\ctrl98.dat, also the in-memory table at 0x624380): 4 records of 0x6D
  bytes, byte 0x1C = the record's device (1 keyboard, 2..5 joystick 1..4; must match every binding's
  device or the game's check at 0x4F3E74 throws the whole file away and rebuilds defaults), 20 binding dwords from 0x1D in this order: up, down, left,
  right, turbo, play calling, direct pass/switch, pause, shoot, pass, pivot, dunking (direct shoot), drive,
  crossover, steal, jump, switch, hand-check, last man back, face up. Binding = device nibble << 28
  (1 keyboard, 2..5 joystick 1..4) | scan code, or button mask (bit n = button n+1), or direction
  (0x10000 up, 0x20000 down, 0x40000 left, 0x80000 right; thresholds 0x6000/0xA000).
- Movement is digital: the game only tests directions against thresholds (0x4F3314) and moves in 8
  directions, with speed from the turbo button. There is no analogue path to map a stick onto.
  `Stick=8way` (default) turns the left stick into 45 degree sectors so diagonals behave like a D-pad;
  `Stick=raw` passes the stick through to the game's own thresholds.
- Defaults: the game builds default bindings (0x4F40C8, used on a first run, when ctrl98.dat is invalid,
  for the Default button on the Controllers screen, and when it re-defaults never-customised controllers
  after a device change) from 8 templates at 0x5CA2DD. It picks the joystick template by button count
  (4, 6, 8, 10; anything else, including our 12, gets the sparse template 0).
  `PadLayout=ps1` (default) overwrites the six joystick templates in memory with the PS1 layout, so every
  game default for a pad is the PS1 layout:
  Circle shoot, Cross pass, Triangle spin, Square crossover; Square steal, Triangle jump/block, Cross switch,
  Circle hand-check; R1 turbo, L1 direct pass/switch, L2 direct shoot / last man back, R2 drive / face up,
  Select play calling, Start pause (from the PS1 manual).
- At start-up (on the game's first joyGetNumDevs call, just before it loads ctrl98.dat) records that still
  hold untouched factory defaults (any original template, or the PS1 layout) move to the PS1 layout on
  gamepad N for controller N when that pad is connected; customised records are left alone. The first
  change keeps the old file as ctrl98.dat.bak. `PadLayout=pc` keeps the game's original defaults.
- Tested under Wine with an SDL virtual gamepad (developer switch: environment variable NBA98_TEST_PAD=<file>
  with "<SDL button mask hex> <x> <y>"): the Controllers screen shows a gamepad for controller 1 with
  "Joy 1 Button 1..12" bindings, and Start on the pad pauses a match. With no ctrl98.dat the game builds
  the PS1 layout for controller 1; with a customised file the record is kept and the Default button gives
  the PS1 layout.
- Not covered yet: the front-end menus are mouse driven and do not react to the pad.

## Cut players as free agents (0.2.0)

The CD has portraits, action shots, in-game heads and play-by-play name speech for 55 players who have no
database record (cut from the roster before release). nba98core.dll now adds 54 of them to the free agent
pool (team 29) by default. The 55th (art key ALSO___) is not identified yet.

- `nba98_cutplayers.dat` (in the release) holds only our own records: names, bios and 1996-97 stats from
  public sources, ratings from formulas fitted to EA's own players. No EA data is shipped. Format
  "NBA98ADD" v1: header (magic, version, section count), then per section the table name, a CRC-32 of the
  DBF field descriptors it was built for, record length, key offset and length, record count, records.
  Our values, shipped as they are (see `nba98db/README.md`).
- At start-up (`nba98core/roster_mod.inc`), when `CutPlayers=1` (default) and the .dat is valid, the core
  copies the user's `dbdata`, `origdb` and `saves` into `nba98data\` (leaving out the PLAYERS and CAREER
  index files so the game rebuilds them) and appends the records to PLAYERS.DBF and CAREER.DBF in
  `dbdata` and `origdb` and to PLAYERS.DBF in `saves\default`. The generated INSTALL.DAT then points the
  game's `dbdata`, `origdb` and `saves` folders at `nba98data\`. The original folders are never written.
- Each append checks the record length and field layout CRC against the user's table, skips a table that
  already holds the records, and refuses one that already has any of the PLAYERIDs (420 onwards). The
  file is written to a temporary name and then swapped in. `nba98data\roster.txt` marks a finished build;
  later starts reuse the folder, so saved seasons and roster edits stay there. If the folder exists
  without the marker, the game uses its original folders for that run. Delete `nba98data\` to rebuild.
- `CutPlayers=0` gives the original behaviour (no redirection). `ctrl98.dat` (gamepads) follows the saves
  folder in use.
- Tested under Wine (Main and Debug): the game reads the database from `nba98data`, rebuilds the PLAYERS
  index files itself, reaches the main menu and the Rosters screen; the second start reuses the folder;
  the original `dbdata`, `origdb` and `saves` are unchanged. The Free Agents screen could not be checked:
  in the headless test machine the game stops responding after that click with or without the mod (main
  thread waiting inside Wine's software Direct3D), so it still needs a check on Windows.

## Michael Jordan in the Roster Player slot (0.3.0)

- `nba98_jordan.dat` (built by `nba98db/build_jordan.py` from `nba98db/research/jordan.json`) turns the
  "Roster Player" records 303 (Chicago) and 404 (East All-Stars) into Michael Jordan: name, nickname,
  number 23, height 6-6, weight 216, birth date and place, North Carolina, 1984 draft (round 1, pick 3,
  Chicago), 12 seasons of experience, Create Player face `:KAAA23.<PLAYERID>` (head 11 of 12, D12), skin
  tone 5, body type 3, name speech REJORDA (play-by-play uses the surname), and the 1996-97 stat block.
  Ratings, team, roster position, flags and season stats are not touched. Record 303's career becomes his
  12 seasons 1984-85 to 1996-97 (the CD has one blank 1996-97 row).
- Data file format is now NBA98ADD v2 (v1 still read): each section has an operation. APPEND adds records
  (skipped when all their keys are already there, refused when only some are), MODIFY copies the bytes
  set in a per-section mask into the table's records with the same key, REPLACE swaps all rows with our
  keys for ours. All three are idempotent. After changing a table the core removes its index files
  (TABLE1/2.CDX, TABLE.CDX) so the game rebuilds them.
- Upgrade path: `nba98data\roster.txt` holds one line per data file with its CRC. A data file that is new
  or changed since then is applied to the existing `nba98data` at the next start, so a 0.2.0 folder with
  saved seasons picks up Jordan without a rebuild. If the update fails the folder is still used and the
  update is retried next start.
- `[nba98] Jordan=1` (default; D10). Switching it (or CutPlayers) off after it has been applied only logs
  that the data is already in `nba98data`; delete the folder to remove it.
- Tested under Wine with the real game: a 0.2.0 `nba98data` (with a cut player moved to Chicago to stand
  in for a played save) upgrades in place: records 303 and 404 change only in the 41 fields we own, every
  other PLAYERS record and every other CAREER row is byte-identical to before, 303 has 12 career rows.
  Fresh build, re-apply over a signed cut player, and Jordan=0 also behave as described; the game
  rebuilds the PLAYERS indexes and reaches the main menu. The face and the Chicago roster screen could not
  be checked on screen here (synthetic clicks stall in Wine's software Direct3D), so that is a Windows check.

## Player art importer (tools/nba98art.py, tools/nba98pic.py)
Builds a player's menu headshot and 3D face from the user's own files, on the user's machine. No art ships.

**Install instructions (Michael Jordan's 3D face).** The only art the importer brings in is Jordan's 3D face. The user supplies the free NBA Live 2000 demo, `NBADEMOG.EXE`; its demo\Art\INGAME\PLAYERS\plrdata.viv holds his 512x448 face. Nothing else is needed.
```
python tools/nba98art.py extract --demo2000 "<path>\NBADEMOG.EXE" --player MIJORDA --out work
python tools/nba98art.py build --name MJORDAN --face work\MIJORDA_face.png --head work\MIJORDA_head.bmp --plrdata "<game>\art\ingame\players\PLRDATA.VIV" --out out
```
- `--demo2000` takes the EXE itself (it is a zip, read directly) or the folder it was unpacked to.
- Use `out\0MJORDAN.FSH` and the `out\PLRDATA.VIV` copy; Jordan's PLAYERPKG is 0MJORDAN.FSH. `build` also needs a `--head` picture for the face package's small in-match picture. The demo's own portrait, auto-cut, serves for that.
- The menu headshot (L_PORT) and action shot (ACTSHOT) are not part of the import. They are optional pictures the user supplies, built with `tools/nba98pic.py` (portrait / action). `build` also writes a headshot BMP; ignore it if you have your own.
- `--smooth 0` is for a clean cut-out PNG used as `--head`.

Older route, from retail installs:
```
python tools/nba98art.py extract --live2000 "<Live 2000 folder>" --live2001 "<Live 2001 folder>" --player MIJORDA --out work
python tools/nba98art.py build --name MJORDAN --face work\MIJORDA_face.png --head work\MIJORDA_head.bmp --plrdata "<game>\art\ingame\players\PLRDATA.VIV" --out out
```

- `extract`: picks the best face texture (Live 2000 demo, then retail Live 2000 large 512x448, medium, small) and the best portrait (Live 2001 large, then Live 2000), and writes the portrait at 4x on magenta for hand editing.
- Automatic head cut (`extract` writes `<KEY>_head.bmp`):
  - It is tuned for one source, Michael Jordan's Live 2001 large portrait (1MIJORDA), the only headshot we import.
  - It keeps the original outline down to just below the ears.
  - Below that, it follows the jaw as a curve to a chin point under the mouth, dropping the neck and jersey.
  - It was fitted to Jason's Paint cut: 99.1% mask overlap, and the same headshot framing.
  - It is not reliable on other players' portraits; cut those by hand in Paint.
- `build` writes:
  - `<NAME>.BMP`: the headshot for L_PORT, put in art\frontend\foregrnd\ports.
  - `0<NAME>.FSH`: the face package for PLAYERPKG. It holds the 256x128 face texture in 254 colours, as EA's do, and the 84x96 in-match picture cut from the same head, with hard alpha.
  - `PLRDATA.VIV`: a copy of the player face archive with that entry added. The original is only read.
  - `<NAME>_check.png`: a preview to look at first.
- Headshot settings (nba98pic defaults for portraits, agreed 2026-10-29):
  - The head is cropped to its outline and fills the 103x118 frame from crown to chin (zoom 0.97).
  - A hand-cut outline is rounded off (smooth 3).
  - The edge fades over 3 to 4 px (feather 0.6), with edge pixels at 0.85 brightness.
  - The output is premultiplied 32-bit, so the game blends it softly.
- A Live 2000 face texture squeezed straight to 256x128 lines up with the Live 98 head (eyes, ears and mouth within 2 px). This was checked in game with Michael Jordan.
- Still to come: the core redirect, so the game reads the PLRDATA.VIV copy from nba98data instead of needing it in the game folder. Until then, test only in a spare copy of the install.
- Known quirk: a face package given to a light-skinned player (COLOR) gave pink, marbled arms. Jordan's own pairing is fine.


## Third-party components

Each third-party file keeps its licence file next to it in the repository and in every release.

| Component | Where | Licence | Licence file |
|---|---|---|---|
| SDL 3.2.26 (`SDL3.dll`, 32-bit, unmodified binary from libsdl.org) | `third_party/SDL3.dll` | zlib | `third_party/SDL3-LICENSE.txt` |
| DOSBox Staging CRT shaders crt-hyllian (The DOSBox Staging Team, Hyllian, Tyrells; mask by hunterk, public domain) and vga-1080p (The DOSBox Staging Team), ported to HLSL | `glide2x/gl_shaders.h` (`k_hlsl_crt`), built into `glide2x.dll` | MIT | `third_party/CRT-SHADERS-LICENSE.txt` (ships in every release) |

Any further third-party code goes in with its licence file and gets a row here and in the release notes.
