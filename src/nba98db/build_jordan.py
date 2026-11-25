"""Builds out/nba98_jordan.dat: Michael Jordan in the "Roster Player" slot (scope item 39).

Inputs  research/jordan.json            public facts and season totals, with sources
        the user's own CD database      only for field layouts, SCHOOLS and TEAMS lookups
Output  out/nba98_jordan.dat            NBA98ADD v2, loaded by nba98core.dll (roster_mod.c)
        out/jordan_review.csv           every value written, field by field

Sections:
  PLAYERS  MODIFY   records 303 (Chicago) and 404 (East All-Stars), our fields only (mask).
                    Ratings, team, roster position, flags, season and playoff stats, portrait
                    and action shot keys are not in the mask, so the user's values stay.
  CAREER   REPLACE  all rows of PLAYERID 303 (the CD has one blank 1996-97 row) become one
                    row per season played, 1984-85 to 1996-97 (D9). The All-Star duplicate 404
                    gets no rows, like every other All-Star duplicate on the CD.

Records are built from blanks: bytes outside the mask are spaces and are never written into
the user's tables, so no EA data is carried in the file.
"""
import csv, json, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'tools'))
from dbf import read                     # noqa: E402
from paths import ORIGDB as CD           # noqa: E402

OP_APPEND, OP_MODIFY, OP_REPLACE = 0, 1, 2

# PLAYERS fields this file owns (everything else in the record is left as the user has it)
MODIFY_FIELDS = [
    'FNAME', 'NAME', 'NICKNAME', 'NUMBER', 'POSITION', 'POSITION2', 'HEIGHT', 'WEIGHT', 'YEARSEXP',
    'SCHOOLNUM', 'COLOR', 'BODYTYPE', 'HAND', 'BIRTHDATE', 'BIRTHTOWN', 'BIRTHSTATE',
    'DPLACE', 'DROUND', 'DOVERALL', 'DRAFTYEAR', 'DRAFTEDBY',
    'SPCHHDR', 'SPCHDATA', 'PLAYERPKG',
    # previous real season (1996-97) block
    'GAMES', 'GAMESSTART', 'MINUTES', 'FGMADE', 'FGATTEMPTS', 'THREEPTMAD', 'THREEPTATT', 'FTMADE', 'FTATTEMPTS',
    'OFFREB', 'DEFREB', 'ASSISTS', 'STEALS', 'TURNOVERS', 'BLOCKS', 'PERFOUL', 'EJECT',
]
SLOTS = [303, 404]          # Roster Player: Chicago, East All-Stars


def table(name):
    return read(CD + name)


def rows(t):
    return [{k: v.decode('latin1').strip() for k, v in r[1].items()} for r in t['recs']]


def encode(t, rec, blank=True):
    out = bytearray(b' ' * t['rl'])
    for name, typ, ln, dc, off in t['fields']:
        if name not in rec:
            continue
        v = rec[name]
        if typ == 'N':
            s = '' if v in ('', None) else (('%.*f' % (dc, float(v))) if dc else str(int(v)))
            if len(s) > ln: raise ValueError('%s=%s too wide (%d)' % (name, s, ln))
            s = s.rjust(ln)
        elif typ == 'L':
            s = (v or ' ')[:1]
        elif typ == 'D':
            s = (v or '').ljust(8)[:8]
        else:
            s = str(v or '')
            if len(s) > ln: raise ValueError('%s=%r too long (%d)' % (name, s, ln))
            s = s.ljust(ln)
        out[off:off + ln] = s.encode('latin1')
    return bytes(out)


def field(t, name):
    for f in t['fields']:
        if f[0] == name: return f
    raise KeyError(name)


def mask_for(t, names):
    m = bytearray(t['rl'])
    for n in names:
        _, _, ln, _, off = field(t, n)
        m[off:off + ln] = b'\x01' * ln
    return bytes(m)


def desc_crc(t):
    d = t['raw']
    return zlib.crc32(d[32:t['hl']]) & 0xffffffff


def write_dat(path, sections):
    """NBA98ADD v2: magic, u32 version 2, u32 nsect; per section: char table[16], u32 desc_crc,
    u16 reclen, u16 key_off, u8 key_len, u8 op (0 append, 1 modify, 2 replace), 2 pad, u32 count,
    then for op 1 a reclen-byte mask (1 = byte comes from us), then the records."""
    b = bytearray(b'NBA98ADD' + struct.pack('<II', 2, len(sections)))
    for name, crc, rl, koff, klen, op, mask, recs in sections:
        b += name.encode().ljust(16, b'\0') + struct.pack('<IHHBB2xI', crc, rl, koff, klen, op, len(recs))
        if op == OP_MODIFY:
            assert len(mask) == rl
            b += mask
        for r in recs:
            assert len(r) == rl
            b += r
    open(path, 'wb').write(b)


def main():
    j = json.load(open(os.path.join(HERE, 'research', 'jordan.json'), encoding='utf-8'))
    pt, ct = table('PLAYERS.DBF'), table('CAREER.DBF')
    schools = {r['SCHOOLNAME']: int(r['SCHOOLID']) for r in rows(table('SCHOOLS.DBF'))}
    abbrevs = {r['ABBREV'] for r in rows(table('TEAMS.DBF'))}
    if j['college'] not in schools: sys.exit('college %r not in SCHOOLS.DBF' % j['college'])
    if j['draft']['team_abbrev'] not in abbrevs: sys.exit('team %r not in TEAMS.DBF' % j['draft']['team_abbrev'])

    cols = j['seasons_cols'].split()
    seasons = {int(y): dict(zip(cols, v)) for y, v in j['seasons'].items()}
    for y, s in seasons.items():                         # points identity, per season
        if s['PTS'] != 2 * s['FGM'] + s['3PM'] + s['FTM']: sys.exit('points do not add up in %d' % y)
    if j['years_exp'] != len(seasons): sys.exit('years_exp %d but %d seasons' % (j['years_exp'], len(seasons)))
    last = seasons[max(seasons)]
    f = j['appearance']['face']
    face = ':%c%c%c%c%d.%%03d' % (65 + f['head'], 65 + f['hair'], 65 + f['facial_hair'], 65 + f['accessories'], j['number'])

    base = dict(
        FNAME=j['fname'], NAME=j['name'], NICKNAME=j['nickname'], NUMBER=j['number'],
        POSITION=j['position'], POSITION2=j['position2'], HEIGHT=j['height_in'], WEIGHT=j['weight_lb'],
        YEARSEXP=j['years_exp'], SCHOOLNUM=schools[j['college']], COLOR=j['appearance']['color'],
        BODYTYPE=j['appearance']['bodytype'], HAND=j['hand'], BIRTHDATE=j['birthdate'],
        BIRTHTOWN=j['birthtown'], BIRTHSTATE=j['birthstate'],
        DPLACE=j['draft']['pick'], DROUND=j['draft']['round'], DOVERALL=j['draft']['pick'],
        DRAFTYEAR=j['draft']['year'], DRAFTEDBY=j['draft']['team_abbrev'],
        SPCHHDR=j['speech_key'] + '.HDR', SPCHDATA=j['speech_key'] + '.DAT',
        GAMES=last['G'], GAMESSTART=last['GS'], MINUTES=last['MIN'], FGMADE=last['FGM'], FGATTEMPTS=last['FGA'],
        THREEPTMAD=last['3PM'], THREEPTATT=last['3PA'], FTMADE=last['FTM'], FTATTEMPTS=last['FTA'],
        OFFREB=last['ORB'], DEFREB=last['DRB'], ASSISTS=last['AST'], STEALS=last['STL'],
        TURNOVERS=last['TOV'], BLOCKS=last['BLK'], PERFOUL=last['PF'], EJECT=0)
    assert set(base) | {'PLAYERPKG'} == set(MODIFY_FIELDS), set(MODIFY_FIELDS) ^ (set(base) | {'PLAYERPKG'})

    precs, review = [], []
    for pid in SLOTS:
        r = dict(base, PLAYERPKG=face % (pid % 1000), PLAYERID=pid)   # the game's own face code uses PLAYERID mod 1000
        precs.append(encode(pt, r))
        review += [('PLAYERS', pid, k, r[k]) for k in MODIFY_FIELDS]

    crecs = []
    for y in sorted(seasons):
        s = seasons[y]
        r = dict(YEAR=y, TEAM=j['career_team'], GAMES=s['G'], GAMESSTART=s['GS'], MINUTES=s['MIN'],
                 FGMADE=s['FGM'], FGATTEMPTS=s['FGA'], THREEPTMAD=s['3PM'], THREEPTATT=s['3PA'],
                 FTMADE=s['FTM'], FTATTEMPTS=s['FTA'], OFFREB=s['ORB'], DEFREB=s['DRB'], ASSISTS=s['AST'],
                 STEALS=s['STL'], TURNOVERS=s['TOV'], BLOCKS=s['BLK'], POINTS=s['PTS'], EJECT=0,
                 PERFOUL=s['PF'], PLAYERID=SLOTS[0])
        crecs.append(b' ' + encode(ct, r)[1:])
        review += [('CAREER', SLOTS[0], '%02d-%02d' % (y, (y + 1) % 100), ' '.join('%s %s' % (k, s[k]) for k in cols))]

    _, _, pkl, _, pko = field(pt, 'PLAYERID')
    _, _, ckl, _, cko = field(ct, 'PLAYERID')
    os.makedirs(os.path.join(HERE, 'out'), exist_ok=True)
    write_dat(os.path.join(HERE, 'out', 'nba98_jordan.dat'), [
        ('PLAYERS', desc_crc(pt), pt['rl'], pko, pkl, OP_MODIFY, mask_for(pt, MODIFY_FIELDS), precs),
        ('CAREER', desc_crc(ct), ct['rl'], cko, ckl, OP_REPLACE, None, crecs),
    ])
    with open(os.path.join(HERE, 'out', 'jordan_review.csv'), 'w', newline='', encoding='utf-8') as fo:
        w = csv.writer(fo)
        w.writerow(['TABLE', 'PLAYERID', 'FIELD', 'VALUE'])
        w.writerows(review)
    print('Jordan: PLAYERS modify %d records (%d fields), CAREER replace with %d seasons' %
          (len(precs), len(MODIFY_FIELDS), len(crecs)))


if __name__ == '__main__':
    main()
