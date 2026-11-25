# NBA Live 98: database and modding investigation (2026-10-28)

Scope: custom rosters, importing career stats, hidden players and teams, player attributes, and how records link to art and speech. Static analysis of the CD data, Jason's install and Patch v1.1 NBAWIN.EXE. Nothing has been changed in the game yet. Tool: src/nba98db/nba98db.py (read-only), exports in src/nba98db/out/.

## 1. Where the data lives
- `dbdata\` is the live database (dBase III .DBF via the CodeBase library, plus CodeBase .CDX compound indexes). `origdb\` is the pristine copy used to restore original rosters. The CD's DBDATAE (English), DBDATAI, DBDATAS hold the initial files; ORIGDB* add SEASONL/M/S (long, medium and short season schedules).
- `saves\default\PLAYERS.DBF` and `TEAMS.DBF`: the default roster. `saves\PLAYERS.nnn` / `TEAMS.nnn`: saved rosters or seasons (full DBF copies, same size, nnn is the slot).
- Jason's dbdata\PLAYERS.DBF differs from the CD only in OVERALLRTG on 14 records: the game recalculates the overall rating itself and fills blanks. Modded records can leave OVERALLRTG empty.
- The CD ships no .CDX files, so the game builds indexes when they are missing. Rule for external edits: delete the matching *.CDX after editing a DBF (PLAYERS1/2, CAREER1/2, TEAMS1/2 and so on). To confirm in game.
- Database files must be writable (CodeBase opens them read/write; read-only copies give "CODEBASE ERROR").

## 2. Tables
Character fields are space padded, numbers are right-aligned text, logicals T/F, dates YYYYMMDD.

### PLAYERS.DBF (414 records, 614 bytes each)
- Key: PLAYERID (0 to 419, unique; CAREER links by it; the exe errors "Could not locate player ID"). Gaps: 96, 137, 156, 231, 289, 383.
- Identity: NAME (surname), FNAME, NICKNAME, NUMBER, BIRTHDATE, BIRTHTOWN, BIRTHSTATE, SCHOOLNUM (-> SCHOOLS.SCHOOLID), YEARSEXP, draft (DPLACE, DROUND, DOVERALL, DRAFTYEAR, DRAFTEDBY as team abbreviation text, "--" for undrafted).
- Team: TEAM and ORIGTEAM (-> TEAMS.TEAMNUM), ROSTERPOS (depth order, 0 = first). Max 15 per team (TXT_ROSTER_FULL; the easter egg copy also stops at 15).
- POSITION: 0 C, 1 PF, 2 SF, 3 SG, 4 PG. POSITION2: second position or -1.
- Body: HEIGHT inches, WEIGHT lb, COLOR skin tone 0 to 5, BODYTYPE 0 to 8, HAND 0 right / 1 left (David Robinson is 1), DUNKPACK 0 to 6, PREFDUNK.
- Ratings (0 to 99 unless noted), in edit-screen order: FGPBASE field goals, THREEPTBAS 3 pt, FTPBASE free throws, DNKABILITY dunking, STLABILITY stealing, BLKABILITY blocking, OREABILITY / DREABILITY off/def rebounds, BALABILITY ball handling, OFFABILITY / DEFABILITY off/def awareness, DSTRENGTH strength, SPEED, QUICK, JUMP, DRIBBLE, DSHOOTRANG shooting range (8 to 25), FATIGUE (20 to 94), CLUTCH, INSIDESC inside shooting, DHARDY (toughness/durability, not on the edit screen), RANGE2 (0 to 17). OVERALLRTG is derived.
- Flags: ISCREATED (made in Create Player), ISHIDDEN (easter egg player), ISROM (shipped player; "restore" filters use ISROM = .T.), ISPLAYABLE (overridden to true at load whenever INJURYID is 0, so effectively "plays through injury"), INJURYID/INJURYDATE/RECOVRDATE (-> INJURY.DBF), STREAK.
- Art and speech links: L_PORT, ACTSHOT, PLAYERPKG, SPCHHDR, SPCHDATA (see section 5).
- PLRSUBID / DUPECOUNT: duplicate-player support; when PLRSUBID is not 0 the game appends "(n)" to the surname.
- SCRATCH1/2: working fields (draft and free-agent pools use SCRATCH1 = 1, 2, -1).
- Stats: the unprefixed block (GAMES, MINUTES, FGMADE ... EJECT) is the previous real season (1996-97, matches CAREER YEAR 96). S* = current game season, P* = playoffs, filled by the game.
- Teams 30 and 31 (All-Stars) hold duplicate records of the stars (IDs 384 to 410) with ORIGTEAM set to the real team.
- "Roster Player" (ID 303, Chicago, and ID 404, East All-Stars): the unlicensed player placeholder, almost certainly Michael Jordan's slot. Has an in-game head (0ROPLAYE) but no portrait.

### CAREER.DBF (2,030 rows, 388 players)
- One row per player per season: YEAR (2 digits, season start: 96 = 1996-97, earliest 78), TEAM as text ("SA", or "ATL-MIA" for traded seasons, "KANSAS CITY" for old franchises), GAMES, GAMESSTART, MINUTES, FG/3PT/FT made and attempts, OFFREB, DEFREB, ASSISTS, STEALS, TURNOVERS, BLOCKS, POINTS, EJECT, PERFOUL, PLAYERID.
- Import: one row per season from any stats source, keyed by our PLAYERID; also copy the latest season into the PLAYERS unprefixed stat block. Open question: YEAR for 2000 and later (field is N4, so 100+ fits; the display format "%d-%d" needs testing, may show "100-101").

### TEAMS.DBF (32 records)
- TEAMNUM 0 to 28 NBA (alphabetical, Atlanta to Washington), 29 Free Agents, 30 East All-Stars, 31 West All-Stars.
- TEAMNAME, CITYNAME, ABBREV (3 letters, used for DRAFTEDBY), TEAMABR2 (2 letters, the in-game art prefix), SHPNAME (logo FSH), LOGOBG (ball/sphere FSH), PCOLOR*/SCOLOR* (RGB), CONFERENCE (0 to 3 are the four divisions, 4 All-Star, 5 FA), LOCATIONID (-> LOCATION.DBF city art and map position), SEED, team ratings (SCORING, REBPOS, BALLCTRL, DEFENSE, OVERALL plus RAW*), MAINPLAY1-3 / DEFPLAY1-3 (default plays), HOMEADV, RTGADJUST, SPCHHDR/SPCHDATA (team name speech), season, playoff and stat fields.
- TEAMCODE: 0 NBA, 1 West All-Star, 2 East All-Star, 3 Free Agents, 4 and 5 appear to be custom teams (Create Player offers TEAMCODE 0, 3, 4, 5; not confirmed).

### Others
- TEAMINFO.DBF: team history text (TEAM, HDRID, ITEMNUM, TEXT). SCHOOLS.DBF: 404 colleges. LOCATION.DBF: 34 cities. INJURY.DBF: 229 injury types with rating penalties. USERS.DBF: user profiles and stats. SCHEDULE.DBF: active schedule (copied from SEASONL/M/S).

## 3. Hidden players and teams (built into the exe, not the database)
- NBAWIN.EXE v1.1 carries 10 hidden EA Canada teams (VA 0x5D623C, 10 x 0x11B bytes) and 116 hidden players (VA 0x5CC160, 116 x 0x163 bytes, the game's own in-memory player struct). Exported to out/rom_teams.csv and out/rom_players.csv with PLAYERS.DBF column names.
- Unlock (code at 0x442E88 and 0x4E5408, called from the custom team screen at 0x445647): create a custom team and set City and Team name to one of the pairs below (not case-sensitive). The game adds the matching team and its players and shows TXT_EASTEREGG.

| City | Team | Abbrev | Players |
|---|---|---|---|
| EA | Europals | EA1 | 12 (likely localisation staff) |
| Hitmen | Coders | EA3 | 12 |
| Hitmen | Earplugs | EA4 | 11 |
| Hitmen | AllSorts | EA5 | 12 |
| Hitmen | Idlers | EA6 | 12 |
| Hitmen | Pixels | EA7 | 12 |
| QA | DBuggers | EA8 | 12 |
| QA | Campers | EA9 | 12 |
| QA | Testtubes | EA0 | 12 |
| TNT | Blasters | QA1 | 9 (TNT/broadcast crew incl. Ernie Johnson, Verne Lundquist) |

- Hidden players have ISHIDDEN = 1, PLAYERID 0 (assigned when added), team numbers 80 to 91, full ratings, nicknames and birth details. All 116 have in-game heads in PLRDATA.VIV; 72 have portraits, 67 action shots, 54 to 59 have name speech.
- Hidden team art: logos 1BOAR, 1CLOWN, 1MONKEY, 1RAVEN, 1ALIEN, 1SPIDER, 1PHAROAH, 1EAGLE (in FLOGOS/RLOGOS.VIV), matching balls in RBALLS.VIV, uniforms Y1 to Y8 in TEAMDATA.VIV, team speech CUS.HDR/DAT.
- Struct offsets (player struct 0x163) are documented in the tool (PLAYER_STRUCT), taken from the DBF-to-struct loader at 0x4DB3C0. Stats words at 0x00 to 0x1D.
- To "unlock permanently" without the easter egg: append these rows to PLAYERS.DBF with new PLAYERIDs and a real TEAM, or add the teams to TEAMS.DBF. Untested.

## 4. Unused player art (cut players)
- The archives hold art for about 230 players with no database record (out/unused_player_art.csv). 55 have a complete set: portrait, action shot, in-game head and play-by-play speech (e.g. ADCALDW, DIRADJA, GRANTHO, MAROSE_, JIKING_). These can become real players by adding PLAYERS records that point at them. Done for 54 of them in 0.2.0: see section 7.
- 0DERODM0 to 5: Dennis Rodman hair colour variants, chosen at run time ("0derodm" + digit).
- 26 shipped players lack a portrait or action shot (fall back to noport*.fsh), e.g. Tracy McGrady, Kelvin Cato, Bruce Bowen, Stephen Jackson; 4 lack name speech. Run `nba98db.py artcheck <install>` for the list.

## 5. How records link to art and speech
- Key: first 2 letters of FNAME + first 5 of NAME, upper case, padded with "_" to 7 (Aaron McKie = AAMCKIE, Bob Sura = BOSURA_). Exe format "%d%.7s.fsh". The link is the stored field, not the name, so any key works if the files exist.
- L_PORT "1KEY.FSH": portrait in art\frontend\foregrnd\ports\PORTS.VIV.
- ACTSHOT "1KEY.FSH": action photo in art\frontend\foregrnd\action\ASHOTS.VIV.
- PLAYERPKG "0KEY.FSH": in-game head/face texture package in art\ingame\players\PLRDATA.VIV (709 entries).
- SPCHHDR/SPCHDATA "KEY.HDR"/"KEY.DAT": name speech in speech\gamespch\PLYRHDRS/PLYRDATA.VIV (game commentary) and speech\playspch\PLYRHDRS/PLYRDATA.VIV (play-by-play, 659 names). netspch is the net game announcer.
- Teams: TEAMABR2 prefix in art\ingame\teams\TEAMDATA.VIV: hom/awy (uniforms), sun (alternate, some teams), cmn, crt.fsh + crt.bin (court texture and geometry), crd, 3cd/3ct, bbd. xx = default custom team, fr = free agents. SHPNAME logo in FLOGOS (large), RLOGOS (sphere), NLOGOS (neon); LOGOBG in RBALLS; overlay logos loose in logos\overlay; city backgrounds in backgrnd\cities ("1%c%ccolor.fsh").
- VIV is EA's BIGF archive (big-endian header, offset, size, name). FSH is EA's SHPI image format.

## 6. What each modding goal needs
1. Custom rosters (trades, new players, attributes): plain DBF edits of PLAYERS (and TEAMS for names, colours, ratings). Keep field widths, unique PLAYERIDs, max 15 per team, delete CDX files after editing (confirmed under Wine: the game rebuilds missing PLAYERS index files). Doable now.
2. Career stats import: build CAREER rows per season plus the PLAYERS last-season block from a modern stats source. Doable now; test the YEAR display for 2000+.
3. Hidden players and teams: the easter egg should work in the shipped game (not yet tried); permanent unlock is a DBF append from rom_players.csv.
4. Player attributes: all ratings are plain fields (section 2).
5. Linked art: existing art can be pointed at freely. New faces need FSH writing and VIV repacking, or better, a loose-file override in nba98core.dll (hook the VIV open/read so `mods\art\...\1KEY.FSH` wins over the archive). Same approach would cover speech.
6. Modern 30-team league: needs new TEAMS rows, team art in every archive, speech, schedule, and almost certainly exe changes (NBA team count and All-Star/FA numbers are fixed). Ghidra work.

## 7. Cut players as free agents
`out/nba98_cutplayers.dat` (loaded by nba98core.dll, see ../README.md) and `out/cutplayers_review.csv`
(every value with its source and a FLAGS column) are our values, shipped as they are. The tool that builds
them, and the ratings models it uses, are kept in the private development repository.

- Players: 54 of the 55 complete art sets, PLAYERIDs 420 onwards in surname order, TEAM 29 (free agents),
  ISROM T. Bio, college, draft, jersey and season totals come from `research/*.json` (public sources such
  as Basketball-Reference, RealGM and Stats Crew; the source is recorded per player). Jersey "00" is
  stored as -1, as EA does. Hand changes go in `cutplayers_overrides.json`.
- CAREER: one row per NBA season (315 rows), YEAR = season start year mod 100, multi-team seasons as
  "TM1-TM2".
- Ratings: our own estimates from each player's public stats (the models are not part of this repository).
  EA rated players from the previous season's stats, so the same is done here (1996-97, or the last
  season played). Seasons are pooled back until 500 minutes; under 500 minutes the result is blended
  towards EA's low-minute average for the position. Three tiers, shown in the CSV:
  - formula (cross-validated R² 0.8 or better): FATIGUE, THREEPTBAS, FTPBASE, STL, BLK, ORE, DRE, BAL,
    DRIBBLE, DSHOOTRANG
  - estimate (0.6 to 0.8): FGPBASE, DNK, OFF, SPEED, QUICK, DSTRENGTH, DHARDY, INSIDESC
  - weak estimate, review by hand: DEF, JUMP, CLUTCH, RANGE2
- Skin tone (COLOR) is read from the face palette of the player's own in-game head texture (97% exact
  against EA's values). BODYTYPE (26% exact) and DUNKPACK (68% exact) are model guesses and flagged.
- Known gaps (all flagged in the CSV): ALSO___ is not identified (listen to its speech file or name it,
  then add a research entry); Schintzius's jersey number is unknown (0 for now); handedness is assumed
  right except Tisdale and Marciulionis (left); generic positions (G, F, F-C) were resolved by height.

## 8. Michael Jordan in the Roster Player slot (build_jordan.py)
Builds `out/nba98_jordan.dat` and `out/jordan_review.csv` from `research/jordan.json` (sources listed in it).
Run `python build_jordan.py` from this folder (finds the CD tables in `../../Original ISO Extract/NBA98/ORIGDBE`, override with NBA98_ORIGDB; needs no extra packages).

- PLAYERS MODIFY on PLAYERIDs 303 and 404. Fields owned (mask): FNAME, NAME, NICKNAME, NUMBER, POSITION,
  POSITION2, HEIGHT, WEIGHT, YEARSEXP, SCHOOLNUM, COLOR, BODYTYPE, HAND, BIRTHDATE, BIRTHTOWN, BIRTHSTATE,
  DPLACE, DROUND, DOVERALL, DRAFTYEAR, DRAFTEDBY, SPCHHDR, SPCHDATA, PLAYERPKG and the 1996-97 stat block
  (GAMES, GAMESSTART, MINUTES, FG, 3PT, FT, OFFREB, DEFREB, ASSISTS, STEALS, TURNOVERS, BLOCKS, PERFOUL,
  EJECT). Ratings stay (D6). Portrait and action shot keys stay ROPLAYE, so the game shows its no-portrait art.
- PLAYERPKG is the Create Player face code ":KAAA23.%03d" with the PLAYERID: the exe's own builder
  (0x48C860) uses struct offset 0xB5, which is PLAYERID, mod 1000.
- CAREER REPLACE on PLAYERID 303: 12 rows (1993-94 not played). 404 gets none, like every All-Star duplicate.
- Conventions checked on the shipped data: YEARSEXP counts seasons played (Brian Shaw 8, missing his year
  in Italy), DPLACE = DOVERALL = overall pick, career TEAM "CHI", DRAFTEDBY "Chi".
- Season totals cross-checked: they sum to the career totals (1072 G, 32292 PTS) and match ESPN's 1984-98
  totals; points equal 2 x FGM + 3PM + FTM in every season.
- Choices to confirm: nickname "His Airness"; weight 216 (NBA.com, his Bulls-era listing; StatsCrew and
  Basketball-Reference show a later 195/198).

## Open items
- In-game test (Windows): Jordan on the Chicago roster, his face in a match, player card and career screen. Free Agents screen confirmed by Jason 2026-10-28.
- In-game test: the easter egg codes; YEAR >= 100 display; appended hidden players.
- Check whether the game reads loose files before VIVs (nba98core LogFileOpens=1 will show it); otherwise add the override hook.
- Write the editor side: CSV to DBF writer that keeps the exact header and widths, plus FSH decode/encode and VIV pack.
