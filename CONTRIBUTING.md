# Contributing to ultnbalive98

## The rule: no EA assets or EA intellectual property

Everything in this project is informed by studying the game, so "derived from the game" is not the test.
The test is whether a file contains EA's own material.

- **Not allowed:**
  - EA's art, audio, video, text and data files, or bytes copied from them. That includes game files,
    database records, archive contents and byte patterns taken from EA files. Code that must recognise an
    EA file checks its size and a CRC instead.
  - EA's rating system, or a model that reproduces it.
  - Anything made from those assets: spliced or decoded speech, upscaled art, converted pictures.
  - These are only ever generated on the user's machine, from the user's own files.
- **Allowed:** our own code, addresses and format knowledge, values we determined ourselves (ratings,
  skin tone matches, body types), public facts, and lists of function names.

## Never commit

`.gitignore` blocks these; check the file list by hand as well.

- The game folders: `Original Files/`, `Original ISO Extract/`, `Patch v1.1/`, and any game exe.
- `Personal Art/`, `mj.dat`, and anything made from personal or EA art.
- Game-format and derived files: `*.BMP`, `*.FSH`, `*.VIV`, `*.DBF`, `*.CDX`, `*.DCT`, `*.ASF`, `*.BNK`,
  speech banks (`*.HDR`, game `*.DAT`), decoded or spliced speech (`*.wav`, MicroTalk test files), and
  screenshots and frame captures.
- Dumps of EA data: `rom_players.csv`, `rom_teams.csv`, `unused_player_art.csv`, and the ratings model
  (`ratings_model3.pkl`).
- Runtime output: `logs/`, `crash/` (`*.dmp`), `nbatemp/`, `nba98data/`.
- Built output: `dist/`, `build/`, release zips and built DLLs. Releases are published separately.

Our own roster mod files (`src/nba98db/out/nba98_*.dat`) and `src/third_party/SDL3.dll` are the only
`.dat` and `.dll` files allowed.

## Third-party code

Third-party code or binaries keep their own licence file next to them, and get a row in the
"Third-party components" table in `README.md` and in the release notes.

## Before every push

1. List every file in the commit and check each one against the rule and the never-commit list above.
2. Show Jason the file list and wait for his approval.
3. Push to a branch, never to `main`. Jason reviews the branch on GitHub before it goes into `main`.
