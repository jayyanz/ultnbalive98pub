#!/usr/bin/env python3
"""NBA Live 98 (PC) database investigation tool. Read-only: it never writes game files.

Commands
  dump    <file.DBF> <out.csv>          DBF table to CSV (all fields, deleted flag in _DEL)
  schema  <file.DBF>                    print field layout
  rom     <NBAWIN.EXE v1.1> <outdir>    export the hidden EA developer teams and players
                                        built into the exe to rom_teams.csv / rom_players.csv
                                        (players use PLAYERS.DBF column names)
  artcheck <install dir>                cross-check PLAYERS.DBF art and speech names
                                        against the VIV archives

Python 3, standard library only.
"""
import csv, os, re, struct, sys

# ---------------------------------------------------------------- DBF (dBase III)

def dbf_read(path):
    d = open(path, 'rb').read()
    n, hl, rl = struct.unpack_from('<IHH', d, 4)
    fields, off, p = [], 1, 32
    while d[p] != 0x0D:
        name = d[p:p + 11].split(b'\0')[0].decode('latin1')
        fields.append((name, chr(d[p + 11]), d[p + 16], d[p + 17], off))
        off += d[p + 16]
        p += 32
    recs = []
    for i in range(n):
        r = d[hl + i * rl: hl + (i + 1) * rl]
        recs.append((r[0:1] == b'*', {f[0]: r[f[4]:f[4] + f[2]].decode('latin1').strip() for f in fields}))
    return fields, recs

# ---------------------------------------------------------------- VIV (EA BIGF)

def viv_names(path):
    d = open(path, 'rb').read()
    if d[:4] not in (b'BIGF', b'BIG4'):
        raise ValueError(path + ': not a BIGF archive')
    n = struct.unpack_from('>I', d, 8)[0]
    p, out = 16, []
    for _ in range(n):
        p += 8
        e = d.index(b'\0', p)
        out.append(d[p:e].decode('latin1'))
        p = e + 1
    return out

# ---------------------------------------------------------------- exe (v1.1 only)

V11_TIMESTAMP = 0x346B890A
ROM_PLAYERS_VA, ROM_PLAYER_SIZE = 0x5CC160, 0x163   # 116 records
ROM_TEAMS_VA, ROM_TEAM_SIZE = 0x5D623C, 0x11B       # 10 records

# In-memory player struct (size 0x163), from the PLAYERS.DBF loader at 0x4DB3C0.
# (field, offset, kind, length) kind: b=uint8 sb=int8 w=int16 s=string
PLAYER_STRUCT = [
    ('NUMBER', 0x1E, 'b', 1), ('POSITION', 0x1F, 'b', 1), ('POSITION2', 0x20, 'sb', 1),
    ('HEIGHT', 0x21, 'b', 1), ('WEIGHT', 0x22, 'w', 2), ('YEARSEXP', 0x24, 'b', 1),
    ('SCHOOLNUM', 0x138, 'w', 2), ('COLOR', 0x27, 'b', 1), ('HAND', 0x28, 'b', 1),
    ('FATIGUE', 0x2B, 'b', 1), ('FGPBASE', 0x2C, 'b', 1), ('THREEPTBAS', 0x2D, 'b', 1),
    ('FTPBASE', 0x2E, 'b', 1), ('DNKABILITY', 0x2F, 'b', 1), ('STLABILITY', 0x30, 'b', 1),
    ('BLKABILITY', 0x31, 'b', 1), ('OREABILITY', 0x32, 'b', 1), ('DREABILITY', 0x33, 'b', 1),
    ('BALABILITY', 0x34, 'b', 1), ('OFFABILITY', 0x35, 'b', 1), ('DEFABILITY', 0x36, 'b', 1),
    ('SPEED', 0x37, 'b', 1), ('QUICK', 0x38, 'b', 1), ('JUMP', 0x39, 'b', 1),
    ('DRIBBLE', 0x3A, 'b', 1), ('DSTRENGTH', 0x3B, 'b', 1), ('DHARDY', 0x3C, 'b', 1),
    ('DSHOOTRANG', 0x3D, 'b', 1), ('NAME', 0x3E, 's', 20), ('FNAME', 0x52, 's', 20),
    ('NICKNAME', 0x66, 's', 26), ('ORIGTEAM', 0x80, 'w', 2), ('BIRTHDATE', 0x82, 's', 9),
    ('BIRTHTOWN', 0x8B, 's', 25), ('BIRTHSTATE', 0x115, 's', 25), ('DPLACE', 0xA4, 'w', 2),
    ('DROUND', 0xA6, 'b', 1), ('DOVERALL', 0xA7, 'b', 1), ('DRAFTYEAR', 0xA8, 'w', 2),
    ('GAMESSTART', 0xAA, 'b', 1), ('PLAYERID', 0xB5, 'w', 2), ('ROSTERPOS', 0xAD, 'b', 1),
    ('DRAFTEDBY', 0xAE, 's', 5), ('TEAM', 0xB3, 'w', 2), ('INJURYID', 0xB7, 'w', 2),
    ('ISPLAYABLE', 0xB9, 'b', 1), ('ISCREATED', 0xCC, 'b', 1), ('ISROM', 0xCD, 'b', 1),
    ('ISHIDDEN', 0xCE, 'b', 1), ('L_PORT', 0xCF, 's', 13), ('ACTSHOT', 0xDC, 's', 13),
    ('PLAYERPKG', 0x103, 's', 13), ('PLRSUBID', 0x112, 'w', 2), ('DUPECOUNT', 0x110, 'w', 2),
    ('SPCHHDR', 0xE9, 's', 13), ('SPCHDATA', 0xF6, 's', 13), ('STREAK', 0x12E, 'w', 2),
    ('INSIDESC', 0x130, 'w', 2), ('CLUTCH', 0x132, 'w', 2), ('BODYTYPE', 0x134, 'w', 2),
    ('RANGE2', 0x114, 'b', 1), ('OVERALLRTG', 0x162, 'b', 1), ('DUNKPACK', 0x136, 'b', 1),
    ('PREFDUNK', 0x137, 'b', 1),
]

TEAM_STRUCT = [  # partial, from the easter egg lookup at 0x442E88 / 0x4E5408
    ('TEAMNUM', 0x00, 'w', 2), ('TEAMNAME', 0x02, 's', 15), ('CITYNAME', 0x11, 's', 16),
    ('ABBREV', 0x21, 's', 6), ('LOGO', 0x27, 's', 13), ('BALL', 0x9B, 's', 13),
    ('SPCHHDR', 0xA8, 's', 13), ('SPCHDATA', 0xB5, 's', 13), ('TEAMABR2', 0xC2, 's', 3),
]


def _field(rec, off, kind, ln):
    if kind == 's':
        return rec[off:off + ln].split(b'\0')[0].decode('latin1')
    if kind == 'b':
        return rec[off]
    if kind == 'sb':
        return struct.unpack_from('<b', rec, off)[0]
    return struct.unpack_from('<h', rec, off)[0]


def exe_rom_tables(exe):
    d = open(exe, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    if struct.unpack_from('<I', d, pe + 8)[0] != V11_TIMESTAMP:
        raise SystemExit('not NBAWIN.EXE v1.1 (timestamp mismatch); offsets are for v1.1 only')
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    sec = pe + 24 + struct.unpack_from('<H', d, pe + 20)[0]
    secs = [struct.unpack_from('<IIII', d, sec + i * 40 + 8) for i in range(nsec)]  # vsize, va, rawsize, raw

    def v2f(va):
        rva = va - 0x400000
        for vs, a, rs, raw in secs:
            if a <= rva < a + rs:
                return rva - a + raw
        raise ValueError(hex(va))

    teams, players = [], []
    o = v2f(ROM_TEAMS_VA)
    for i in range(10):
        r = d[o + i * ROM_TEAM_SIZE: o + (i + 1) * ROM_TEAM_SIZE]
        teams.append({f: _field(r, off, k, ln) for f, off, k, ln in TEAM_STRUCT})
    o = v2f(ROM_PLAYERS_VA)
    for i in range(116):
        r = d[o + i * ROM_PLAYER_SIZE: o + (i + 1) * ROM_PLAYER_SIZE]
        players.append({f: _field(r, off, k, ln) for f, off, k, ln in PLAYER_STRUCT})
    return teams, players

# ---------------------------------------------------------------- commands

def cmd_dump(dbf, out):
    fields, recs = dbf_read(dbf)
    with open(out, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['_DEL'] + [x[0] for x in fields])
        for dl, r in recs:
            w.writerow(['*' if dl else ''] + [r[x[0]] for x in fields])
    print(f'{len(recs)} records -> {out}')


def cmd_schema(dbf):
    fields, recs = dbf_read(dbf)
    print(f'{dbf}: {len(recs)} records')
    for name, t, ln, dc, off in fields:
        print(f'  {name:<11} {t} {ln:3d}.{dc}  @{off}')


def cmd_rom(exe, outdir):
    os.makedirs(outdir, exist_ok=True)
    teams, players = exe_rom_tables(exe)
    for name, rows in (('rom_teams.csv', teams), ('rom_players.csv', players)):
        with open(os.path.join(outdir, name), 'w', newline='', encoding='utf-8') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    print(f'{len(teams)} teams, {len(players)} players -> {outdir}')


def cmd_artcheck(root):
    def find(*parts):
        p = root
        for part in parts:
            hits = [x for x in os.listdir(p) if x.lower() == part.lower()]
            if not hits:
                return None
            p = os.path.join(p, hits[0])
        return p

    fields, recs = dbf_read(find('dbdata', 'PLAYERS.DBF'))
    arcs = {
        'L_PORT': find('art', 'frontend', 'foregrnd', 'ports', 'PORTS.VIV'),
        'ACTSHOT': find('art', 'frontend', 'foregrnd', 'action', 'ASHOTS.VIV'),
        'PLAYERPKG': find('art', 'ingame', 'players', 'PLRDATA.VIV'),
        'SPCHHDR': find('speech', 'playspch', 'PLYRHDRS.VIV'),
    }
    names = {k: {n.upper() for n in viv_names(v)} if v else set() for k, v in arcs.items()}
    for dl, r in recs:
        if dl:
            continue
        miss = [k for k in arcs if r[k] and r[k].upper() not in names[k]]
        if miss:
            print(f"{r['PLAYERID']:>4} {r['FNAME']} {r['NAME']}: missing {', '.join(f'{k}={r[k]}' for k in miss)}")


def main(a):
    if len(a) >= 3 and a[0] == 'dump':
        return cmd_dump(a[1], a[2])
    if len(a) >= 2 and a[0] == 'schema':
        return cmd_schema(a[1])
    if len(a) >= 3 and a[0] == 'rom':
        return cmd_rom(a[1], a[2])
    if len(a) >= 2 and a[0] == 'artcheck':
        return cmd_artcheck(a[1])
    print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
