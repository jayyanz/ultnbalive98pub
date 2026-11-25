#!/usr/bin/env python3
"""nba98art - import player art (3D face texture and menu headshot) into NBA Live 98.

Builds, on the user's own machine and from the user's own files:
  * the menu headshot: a 32-bit BMP with EA-style soft edges (via nba98pic.py, the agreed
    "option C" settings), 103 x 118, for the player's L_PORT field;
  * the 3D face package: an FSH with the 256 x 128 face texture ("face") and the 84 x 96
    in-match picture ("port"), for the player's PLAYERPKG field;
  * a COPY of PLRDATA.VIV with that face package added (the original is never written).

Nothing EA made ships with this tool: every picture comes from files the user points it at.

Michael Jordan's 3D face (the agreed route): from the free NBA Live 2000 demo, NBADEMOG.EXE.
       nba98art.py extract --demo2000 "D:\\Downloads\\NBADEMOG.EXE" --player MIJORDA --out work
       nba98art.py build --name MJORDAN --face work\\MIJORDA_face.png --head <headshot cut-out>
                         --smooth 0 --plrdata "C:\\NBA Live 98\\art\\ingame\\players\\PLRDATA.VIV" --out out
   (the headshot is the user's own front-on cut-out PNG; the demo's 140x140 portrait is also
    extracted as a fallback)

Older route (Michael Jordan from retail NBA Live 2000 and 2001):

  1. Pull the source art out of your own installs:
       nba98art.py extract --live2000 "D:\\Games\\NBA Live 2000" --live2001 "D:\\Games\\NBA Live 2001"
                           --player MIJORDA --out work
     writes work\\MIJORDA_face.png (best face texture found) and work\\MIJORDA_edit.bmp
     (the portrait at 4x on a magenta background, ready for Paint).

  2. extract also writes work\\MIJORDA_head.bmp with the neck and jersey removed
     automatically (fitted to a hand cut in Paint).  Check it, and if needed touch it up in
     Paint: pure magenta (255,0,255) is transparent; pencil or hard brush; 24-bit BMP.
     The automatic cut is tuned for Jordan's Live 2001 portrait; cut other players by hand.

  3. Build the game files:
       nba98art.py build --name MJORDAN --face work\\MIJORDA_face.png --head work\\MIJORDA_head.bmp
                         --plrdata "C:\\NBA Live 98\\art\\ingame\\players\\PLRDATA.VIV" --out out
     writes out\\MJORDAN.BMP, out\\0MJORDAN.FSH and out\\PLRDATA.VIV (copy + new entry), plus
     out\\MJORDAN_check.png to look at before using them.

Any RGBA PNG (or magenta-keyed BMP) can be used for --head, and any picture laid out like an
EA face texture (face in the middle, ears either side, scalp at the top) for --face.
"""
import argparse, os, struct, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nba98pic

FACE_SIZE = (256, 128)        # Live 98 face texture
PORT_SIZE = (84, 96)          # Live 98 in-match picture (binary alpha, head fills the frame)
KEY = (255, 0, 255)


# ---------------------------------------------------------------- RefPack, VIV, FSH ----------
def refpack_unpack(s):
    """EA RefPack (QFS) decompressor."""
    if s[1] != 0xFB:
        raise ValueError('not refpack')
    n = 4 if s[0] & 0x80 else 3
    p = 2 + (n if s[0] & 0x01 else 0)
    size = int.from_bytes(s[p:p + n], 'big'); p += n
    out = bytearray()
    while p < len(s):
        b0 = s[p]
        if b0 < 0x80:
            b1 = s[p + 1]; p += 2; lit = b0 & 3; out += s[p:p + lit]; p += lit
            ln = ((b0 >> 2) & 7) + 3; off = ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = s[p + 1], s[p + 2]; p += 3; lit = b1 >> 6; out += s[p:p + lit]; p += lit
            ln = (b0 & 0x3F) + 4; off = ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = s[p + 1], s[p + 2], s[p + 3]; p += 4; lit = b0 & 3; out += s[p:p + lit]; p += lit
            ln = ((b0 & 0x0C) << 6) + b3 + 5; off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            p += 1; lit = ((b0 & 0x1F) << 2) + 4; out += s[p:p + lit]; p += lit; continue
        else:
            p += 1; lit = b0 & 3; out += s[p:p + lit]; p += lit; break
        for _ in range(ln):
            out.append(out[-off])
    return bytes(out[:size]) if size else bytes(out)


def viv_read(path):
    """List of (name, bytes) from a BIGF archive."""
    d = open(path, 'rb').read()
    if d[:4] not in (b'BIGF', b'BIGH', b'BIG4'):
        sys.exit('%s is not a VIV/BIG archive' % path)
    n = struct.unpack('>I', d[8:12])[0]; p = 16; out = []
    for _ in range(n):
        off, sz = struct.unpack('>II', d[p:p + 8]); p += 8
        e = d.index(b'\0', p); out.append((d[p:e].decode('latin1'), d[off:off + sz])); p = e + 1
    return out


def viv_write(path, items):
    hdr = 16 + sum(8 + len(n) + 1 for n, _ in items)
    off = (hdr + 3) & ~3; dirb = b''; datab = b''; cur = off
    for n, data in items:
        dirb += struct.pack('>II', cur, len(data)) + n.encode('latin1') + b'\0'
        pad = (-len(data)) % 4; datab += data + b'\0' * pad; cur += len(data) + pad
    with open(path, 'wb') as f:
        f.write((b'BIGF' + struct.pack('>III', off + len(datab), len(items), off) + dirb).ljust(off, b'\0') + datab)   # header size field: EA stores the aligned value


def fsh_images(f):
    """Decode an FSH (raw or RefPack) to [(tag, RGBA image)] for the types EA used here."""
    if f[1:2] == b'\xfb':
        f = refpack_unpack(f)
    if f[:4] != b'SHPI':
        raise ValueError('not an FSH')
    n = struct.unpack_from('<I', f, 8)[0]; out = []
    for i in range(n):
        tag = f[16 + i * 8:20 + i * 8].decode('latin1'); o = struct.unpack_from('<I', f, 20 + i * 8)[0]
        t = f[o]; w, h = struct.unpack_from('<HH', f, o + 4); px = o + 16
        nxt = int.from_bytes(f[o + 1:o + 4], 'little')
        if t == 0x7B:
            pal = None; q = o
            while nxt:
                q += nxt; nxt = int.from_bytes(f[q + 1:q + 4], 'little')
                if f[q] == 0x2A:
                    pal = [(f[q + 16 + k * 4 + 2], f[q + 16 + k * 4 + 1], f[q + 16 + k * 4], f[q + 16 + k * 4 + 3]) for k in range(256)]
                    break
                if f[q] == 0x24:
                    cnt = struct.unpack_from('<H', f, q + 4)[0] or 256   # 0x24: 24-bit palette, stored R, G, B
                    pal = [(f[q + 16 + k * 3], f[q + 16 + k * 3 + 1], f[q + 16 + k * 3 + 2], 255) for k in range(min(cnt, 256))]
                    pal += [(0, 0, 0, 255)] * (256 - len(pal))
                    break
            pal = pal or [(k, k, k, 255) for k in range(256)]
            im = Image.new('RGBA', (w, h)); im.putdata([pal[b] for b in f[px:px + w * h]])
        elif t == 0x78:
            c = np.frombuffer(f[px:px + w * h * 2], '<u2').reshape(h, w).astype(int)
            rgb = np.dstack([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31])
            im = Image.fromarray(np.dstack([rgb, np.full((h, w), 255)]).astype(np.uint8), 'RGBA')
        elif t == 0x7D:
            im = Image.fromarray(np.frombuffer(f[px:px + w * h * 4], np.uint8).reshape(h, w, 4)[..., [2, 1, 0, 3]].copy(), 'RGBA')
        elif t == 0x7F:
            im = Image.fromarray(np.frombuffer(f[px:px + w * h * 3], np.uint8).reshape(h, w, 3)[..., ::-1].copy(), 'RGB').convert('RGBA')
        else:
            continue
        out.append((tag, im))
    return out


def fsh_8bit_block(idx, w, h, pal_rgba, cx=0, cy=0):
    """One 0x7B image block followed by its attached 0x2A palette block (EA layout)."""
    img = bytes([0x7B]) + (16 + len(idx)).to_bytes(3, 'little') + struct.pack('<HHHHHH', w, h, cx, cy, 0, 0) + idx
    pal = bytes([0x2A, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0]) + b''.join(bytes([b, g, r, a]) for r, g, b, a in pal_rgba)
    return img + pal


def fsh_write(entries):
    """SHPI 'GIMX' file from [(tag, block bytes)]."""
    n = len(entries); off = 16 + 8 * n; dirb = b''; body = b''
    for tag, blk in entries:
        dirb += tag.encode('latin1') + struct.pack('<I', off + len(body)); body += blk
    total = off + len(body)
    return b'SHPI' + struct.pack('<II', total, n) + b'GIMX' + dirb + body


# ---------------------------------------------------------------- the two pictures -----------
def build_face_texture(src):
    """Any face texture (Live 2000 512 x 448 is the best source) -> 256 x 128, 254 colours.
    Live 2000 and Live 98 lay the face out the same way, so a straight resize lines up
    (eyes, ear centres and mouth within 2 px of Live 98's layout, checked in game)."""
    im = src.convert('RGB').resize(FACE_SIZE, Image.LANCZOS)
    q = im.quantize(254, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)  # EA faces use 0..253
    pal = q.getpalette()[:254 * 3]; pal += [0] * (254 * 3 - len(pal))
    pal_rgba = [tuple(pal[k * 3:k * 3 + 3]) + (255,) for k in range(254)] + [(0, 0, 0, 255)] * 2
    return bytes(q.tobytes()), pal_rgba, q.convert('RGB')


def build_port(head):
    """84 x 96 in-match picture: the same head cut-out, crown to chin like EA's (median
    left 6, right 77, top 0, bottom 95), hard alpha, index 0 transparent."""
    im = nba98pic.load_cutout(head) if isinstance(head, str) else head
    bb = im.getbbox(); im = im.crop(bb)
    p = nba98pic.fit(im, PORT_SIZE, 0.99, 0, 0)
    p = nba98pic.resize_premult(np.clip(p, 0, 1), PORT_SIZE)
    a = p[..., 3]; op = a >= 0.5
    rgb = np.where(op[..., None], p[..., :3] / np.maximum(a[..., None], 1e-3), 0)
    rgb = (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)
    q = Image.fromarray(rgb, 'RGB').quantize(255, method=Image.MEDIANCUT, dither=Image.NONE)
    idx = np.asarray(q).astype(np.int32) + 1; idx[~op] = 0
    pal = q.getpalette()[:255 * 3]; pal += [0] * (255 * 3 - len(pal))
    pal_rgba = [(0, 0, 0, 0)] + [tuple(pal[k * 3:k * 3 + 3]) + (255,) for k in range(255)]
    prev = np.zeros((PORT_SIZE[1], PORT_SIZE[0], 4), np.uint8); prev[op, :3] = rgb[op]; prev[op, 3] = 255
    return idx.astype(np.uint8).tobytes(), pal_rgba, Image.fromarray(prev, 'RGBA')


def build_face_fsh(face_src, head_src):
    fidx, fpal, face_prev = build_face_texture(face_src)
    pidx, ppal, port_prev = build_port(head_src)
    fsh = fsh_write([('face', fsh_8bit_block(fidx, *FACE_SIZE, fpal)),
                     ('port', fsh_8bit_block(pidx, *PORT_SIZE, ppal, PORT_SIZE[0] // 2, PORT_SIZE[1] // 2))])
    return fsh, face_prev, port_prev


# ---------------------------------------------------------------- automatic head cut ---------
# Tuned for one source: Michael Jordan's Live 2001 large portrait (1MIJORDA), the only headshot
# we import.  Not reliable on other players' portraits (cut those by hand in Paint).
# Mimics a hand cut in Paint on EA's head-and-shoulders portraits (Live 2000/2001): keep the
# original outline down to just below the ears, then follow the jaw as a curve to a chin point
# under the mouth, removing the neck and jersey.  Fitted to Jason's Paint edit of Jordan (mask
# overlap 99.1%; the headshot comes out the same size and framing): chin at crown + 1.38 x head
# width (ears included), curve exponent 1.75 (between a V and an ellipse).
CHIN_K, JAW_N = 1.38, 1.75


def _blur(a, sigma):
    """Separable Gaussian blur of a 2D array (edges repeated)."""
    r = max(1, int(np.ceil(3 * sigma))); k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2); k /= k.sum()
    q = np.pad(a, r, mode='edge')
    q = sum(k[i] * q[i:i + a.shape[0], :] for i in range(2 * r + 1))
    return sum(k[i] * q[:, i:i + a.shape[1]] for i in range(2 * r + 1))


def head_landmarks(mask, rgb):
    """mask: HxW bool cut-out including the shoulders; rgb: HxWx3 float.
    Returns crown row, ear-to-ear extent, jaw start row and the mouth point."""
    H, W = mask.shape
    rows = [np.nonzero(mask[y])[0] for y in range(H)]
    width = np.array([r.max() - r.min() + 1 if len(r) else 0 for r in rows])
    if not width.any():
        raise ValueError('empty cut-out')
    crown = int(np.nonzero(width)[0][0])
    M, ear_y = 0, crown                      # widest row (the ears) before the outline narrows
    for y in range(crown, H):
        if width[y] >= M: M, ear_y = width[y], y
        elif width[y] < 0.9 * M: break
    y = ear_y; neck_y, neck_w = ear_y, M    # narrowest row (neck) before the shoulders widen past the ears
    while y < H and width[y] <= M:
        if width[y] < neck_w: neck_y, neck_w = y, width[y]
        y += 1
    if neck_w > 0.95 * M:
        raise ValueError('no neck under the ears (is this a head-and-shoulders portrait?)')
    thr = M - 0.75 * (M - neck_w)
    y0 = next(y for y in range(ear_y, neck_y + 1) if width[y] <= thr)
    left = min(rows[y].min() for y in range(crown, y0) if len(rows[y]))
    right = max(rows[y].max() for y in range(crown, y0) if len(rows[y]))
    Wh = right - left + 1
    # mouth: the strongest bright-to-dark edge going down in the lower middle of the face
    L = _blur(rgb.mean(2), max(1.0, H / 138.0)); g = L[1:] - L[:-1]
    ys = slice(int(crown + 0.95 * Wh), min(H - 1, int(crown + 1.3 * Wh)))
    xs = slice(int(left + 0.3 * Wh), int(right - 0.3 * Wh) + 1)
    sub = np.where(mask[ys, xs], g[ys, xs], 0)
    my = int(np.argmin(sub.min(1))) + ys.start
    row = g[my, xs]; cols = np.nonzero(row < row.min() * 0.4)[0] + xs.start
    mx = float(cols.mean()) if len(cols) else (left + right) / 2.0
    return dict(crown=crown, left=left, right=right, Wh=Wh, y0=y0, mouth=(mx, my))


def auto_head_cut(mask, rgb, chin_k=CHIN_K, n=JAW_N):
    """Head-only mask from a head-and-shoulders mask (see the comment above)."""
    lm = head_landmarks(mask, rgb)
    H, W = mask.shape
    y0 = lm['y0']; cx = lm['mouth'][0]; cy = lm['crown'] + chin_k * lm['Wh']
    r = np.nonzero(mask[y0])[0]; xl, xr = r.min(), r.max()
    yy, xx = np.mgrid[0:H, 0:W].astype(float)
    t = np.clip((yy - y0) / max(cy - y0, 1), 0, 1)
    half = (1 - t ** n) ** (1 / n)
    inside = np.where(xx < cx, xx >= cx - (cx - xl) * half, xx <= cx + (xr - cx) * half) & (yy <= cy)
    return mask & ((yy < y0) | inside), lm


def autocut_file(src, dst):
    """Magenta-keyed (or RGBA) head-and-shoulders picture -> magenta-keyed head-only BMP."""
    im = nba98pic.load_cutout(src)
    a = np.asarray(im).astype(np.float32)
    mask = a[..., 3] >= 128
    head, lm = auto_head_cut(mask, a[..., :3])
    out = a[..., :3].astype(np.uint8).copy(); out[~head] = KEY
    Image.fromarray(out, 'RGB').save(dst)
    return lm


# ---------------------------------------------------------------- source extraction ----------
def find_file(root, *parts):
    """Case-insensitive path lookup under root."""
    cur = root
    for part in parts:
        if not os.path.isdir(cur):
            return None
        hit = [x for x in os.listdir(cur) if x.lower() == part.lower()]
        if not hit:
            return None
        cur = os.path.join(cur, hit[0])
    return cur


def viv_entry(path, name):
    for n, data in viv_read(path):
        if n.lower() == name.lower():
            return data
    return None


def demo_plrdata(path):
    """NBA Live 2000 demo: plrdata.viv from the self-extracting NBADEMOG.EXE (a zip) or from
    a folder it was unpacked to.  Returns (bytes of the VIV, label) or (None, None)."""
    import zipfile, io
    if os.path.isfile(path):
        try:
            z = zipfile.ZipFile(path)
        except zipfile.BadZipFile:
            sys.exit('%s is not the NBA Live 2000 demo (NBADEMOG.EXE)' % path)
        hit = [n for n in z.namelist() if n.lower().replace('\\', '/').endswith('art/ingame/players/plrdata.viv')]
        return (z.read(hit[0]), path + ':' + hit[0]) if hit else (None, None)
    for root in (path, os.path.join(path, 'demo')):
        v = find_file(root, 'art', 'ingame', 'players', 'plrdata.viv')
        if v:
            return open(v, 'rb').read(), v
    return None, None


def viv_bytes_entry(data, name):
    n = struct.unpack('>I', data[8:12])[0]; p = 16
    for _ in range(n):
        off, sz = struct.unpack('>II', data[p:p + 8]); p += 8
        e = data.index(b'\0', p); nm = data[p:e].decode('latin1'); p = e + 1
        if nm.lower() == name.lower():
            return data[off:off + sz]
    return None


def extract(a):
    os.makedirs(a.out, exist_ok=True)
    key = a.player.upper()
    face = None
    demo_viv = None
    if a.demo2000:   # the free NBA Live 2000 demo: has the 512x448 face and the 140x140 portrait
        demo_viv, label = demo_plrdata(a.demo2000)
        f = demo_viv and viv_bytes_entry(demo_viv, '0%s.fsh' % key)
        if f:
            face = max((im for _, im in fsh_images(f)), key=lambda im: im.size[0] * im.size[1])
            print('face texture: %s %dx%d from %s' % ('0%s.fsh' % key, face.size[0], face.size[1], label))
    if face is None and a.live2000:   # largest texture set first
        for parts in (('art', 'ingame', 'largetextures', 'plrdata2.viv'),
                      ('art', 'ingame', 'mediumtextures', 'plrdata1.viv'),
                      ('art', 'ingame', 'players', 'plrdata0.viv')):
            v = find_file(a.live2000, *parts)
            f = v and viv_entry(v, '0%s.fsh' % key)
            if f:
                face = max((im for _, im in fsh_images(f)), key=lambda im: im.size[0] * im.size[1])
                print('face texture: %s %dx%d from %s' % ('0%s.fsh' % key, face.size[0], face.size[1], v))
                break
    if face is None:
        print('no face texture found (give --demo2000 or --live2000, or supply your own picture to build --face)')
    else:
        face.convert('RGB').save(os.path.join(a.out, '%s_face.png' % key))

    port = None
    sources = []
    if a.live2001:
        sources.append(('Live 2001 large portrait', find_file(a.live2001, 'art', 'ports', 'lrgport.viv')))
    if a.live2000:
        sources.append(('Live 2000 portrait', find_file(a.live2000, 'art', 'ingame', 'players', 'plrdata0.viv')))
    if demo_viv:
        sources.append(('Live 2000 demo portrait', demo_viv))
    for label, v in sources:
        f = v and (viv_bytes_entry(v, '1%s.fsh' % key) if isinstance(v, bytes) else viv_entry(v, '1%s.fsh' % key))
        if f:
            port = fsh_images(f)[0][1]; print('portrait: %s %dx%d (%s)' % ('1%s.fsh' % key, port.size[0], port.size[1], label))
            break
    if port is None:
        print('no portrait found (give --live2001 or --live2000, or supply your own cut-out to build --head)')
        return
    # straight colour on magenta, 4x, for editing in Paint (the stepped outline is smoothed later)
    p = np.asarray(port).astype(np.float32); al = p[..., 3:4] / 255
    rgb = np.where(al > 0, p[..., :3] / np.maximum(al, 1e-3), 0).clip(0, 255).astype(np.uint8)
    w, h = port.size; S = 4
    big = np.asarray(Image.fromarray(rgb, 'RGB').resize((w * S, h * S), Image.LANCZOS)).copy()
    m = np.asarray(Image.fromarray((p[..., 3] >= 128).astype(np.uint8) * 255, 'L').resize((w * S, h * S), Image.BILINEAR))
    big[m < 128] = KEY
    out = os.path.join(a.out, '%s_edit.bmp' % key)
    Image.fromarray(big, 'RGB').save(out)
    print('wrote %s (portrait at 4x on magenta, as found)' % out)
    auto = os.path.join(a.out, '%s_head.bmp' % key)
    try:
        autocut_file(out, auto)
        print('wrote %s (neck and jersey removed automatically). Use it with build --head, or touch it '
              'up in Paint first (magenta 255,0,255 = transparent; save as 24-bit BMP)' % auto)
    except ValueError as ex:
        print('automatic head cut not possible (%s): paint the neck and jersey in magenta in %s instead' % (ex, out))


# ---------------------------------------------------------------- build ----------------------
def check_sheet(headshot, face_prev, port_prev, path):
    """Headshot over grey and team red (the game's premultiplied blend), face texture, port."""
    p = np.asarray(headshot).astype(np.float32); al = p[..., 3:4] / 255
    tiles = [np.clip(p[..., :3] + (1 - al) * np.array(bg, np.float32), 0, 255).astype(np.uint8)
             for bg in ((90, 90, 90), (150, 20, 25))]
    S = 3; sheet = Image.new('RGB', (2 * 103 * S + 256 * 2 + 84 * S + 60, max(118 * S, 256) + 40), (40, 40, 40))
    d = ImageDraw.Draw(sheet); x = 10
    for t, label in zip(tiles, ('headshot on grey', 'headshot on red')):
        sheet.paste(Image.fromarray(t).resize((103 * S, 118 * S), Image.NEAREST), (x, 30)); d.text((x, 10), label, fill=(255, 255, 255)); x += 103 * S + 10
    sheet.paste(face_prev.resize((512, 256), Image.NEAREST), (x, 30)); d.text((x, 10), '3D face texture (256x128, shown 2x)', fill=(255, 255, 255)); x += 522
    bg = Image.new('RGBA', port_prev.size, (90, 90, 90, 255)); bg.alpha_composite(port_prev)
    sheet.paste(bg.convert('RGB').resize((84 * S, 96 * S), Image.NEAREST), (x, 30)); d.text((x, 10), 'in-match picture', fill=(255, 255, 255))
    sheet.save(path)


def build(a):
    name = a.name.upper()
    if not (1 <= len(name) <= 7 and name.isalnum()):
        sys.exit('--name must be 1 to 7 letters or digits (the face file is 0<name>.FSH, an 8.3 name)')
    os.makedirs(a.out, exist_ok=True)
    head = nba98pic.load_cutout(a.head)
    out_bmp = os.path.join(a.out, name + '.BMP')
    hs = nba98pic.build(head, 'portrait', smooth=a.smooth)
    nba98pic.write_bmp32(hs, out_bmp)
    print('headshot: %s (put in art\\frontend\\foregrnd\\ports, set L_PORT = %s.BMP)' % (out_bmp, name))
    if a.face:
        fsh, face_prev, port_prev = build_face_fsh(Image.open(a.face), head)
        fname = '0%s.FSH' % name
        open(os.path.join(a.out, fname), 'wb').write(fsh)
        print('face package: %s (set PLAYERPKG = %s)' % (os.path.join(a.out, fname), fname))
        if a.plrdata:
            if os.path.abspath(a.plrdata) == os.path.abspath(os.path.join(a.out, 'PLRDATA.VIV')):
                sys.exit('--out must not be the game folder: the original PLRDATA.VIV is never written')
            items = [x for x in viv_read(a.plrdata) if x[0].upper() != fname]
            items.append((fname, fsh)); items.sort(key=lambda x: x[0].upper())
            viv_write(os.path.join(a.out, 'PLRDATA.VIV'), items)
            print('archive copy: %s (%d entries, original untouched)' % (os.path.join(a.out, 'PLRDATA.VIV'), len(items)))
        check_sheet(hs, face_prev, port_prev, os.path.join(a.out, name + '_check.png'))
        print('check: %s' % os.path.join(a.out, name + '_check.png'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('extract', help='pull a player\'s face texture and portrait from your Live 2000/2001 installs')
    e.add_argument('--demo2000', help='NBA Live 2000 demo: NBADEMOG.EXE itself, or the folder it was unpacked to (recommended source)')
    e.add_argument('--live2000', help='retail NBA Live 2000 folder'); e.add_argument('--live2001', help='retail NBA Live 2001 folder')
    e.add_argument('--player', required=True, help='EA art key, e.g. MIJORDA (from 1MIJORDA.fsh / 0MIJORDA.fsh)')
    e.add_argument('--out', required=True)
    c = sub.add_parser('autocut', help='remove the neck and jersey (tuned for Jordan\'s Live 2001 portrait)')
    c.add_argument('src', help='RGBA PNG, or 24-bit BMP with magenta background')
    c.add_argument('out', help='head-only BMP on magenta (editable in Paint)')
    b = sub.add_parser('build', help='make the headshot BMP, face package and archive copy')
    b.add_argument('--name', required=True, help='up to 7 letters, e.g. MJORDAN -> MJORDAN.BMP and 0MJORDAN.FSH')
    b.add_argument('--head', required=True, help='head cut-out: RGBA PNG, or 24-bit BMP with magenta background')
    b.add_argument('--face', help='face texture picture (e.g. <key>_face.png from extract)')
    b.add_argument('--plrdata', help='your game\'s art\\ingame\\players\\PLRDATA.VIV (read only; a copy is written)')
    b.add_argument('--smooth', type=float, help='round off a stepped hand-cut outline (default 3); use 0 for a clean cut-out PNG')
    b.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.cmd == 'extract':
        extract(a)
    elif a.cmd == 'autocut':
        lm = autocut_file(a.src, a.out)
        print('wrote %s (crown row %d, head width %d, jaw from row %d, chin under x %.0f)' % (
            a.out, lm['crown'], lm['Wh'], lm['y0'], lm['mouth'][0]))
    else:
        build(a)


if __name__ == '__main__':
    main()
