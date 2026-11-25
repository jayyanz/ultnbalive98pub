#!/usr/bin/env python3
"""nba98pic - turn a cut-out picture into an NBA Live 98 portrait or action shot
with EA-style soft (anti-aliased) edges.

How the game draws picture transparency (measured, see claude/nba98-player-pictures.md):
  * The frontend blitter blends PREMULTIPLIED alpha:  screen = src + (1 - a) * background.
  * EA's own art is premultiplied (no colour channel ever exceeds alpha) and has a
    1 to 2 pixel alpha ramp on its edges, which is what you get when a sharp, high
    resolution cut-out is shrunk down to 103x118.
  * A 32-bit BMP dropped into userports\\ (or useraction\\) uses the same blend, so no
    exe change and no VIV repack is needed.  Straight (non-premultiplied) colour does
    NOT work: a white pixel at any alpha above 0 draws solid white.

Recipe used here:
  1. Load an RGBA image whose alpha channel is the cut-out (PNG from any editor or
     background remover).  The bigger the source, the better the edge.
  2. Premultiply, then shrink to the game size with an area filter, so edge pixels
     get partial coverage (the anti-aliasing).
  2b. Resampling is done per premultiplied channel in float (bilinear up, box down):
     Pillow's RGBA resize premultiplies again and Lanczos rings, which left a light halo.
  2c. Partly transparent edge pixels are darkened (--fringe, default 0.7), as EA's are:
     their edge pixels are about 0.7 of the face brightness, a soft dark outline.
  2d. --smooth N rounds off a stepped hand-cut outline (e.g. a Paint edit) at source
     resolution before scaling.  EA-like result: edge ramp 1 px (about 60%) or 2 px,
     outer pixel alpha about 100, with --feather 0 (the default).
  3. Optionally feather the alpha a little more (--feather, in game pixels).  Blurring
     the premultiplied channels together keeps colour and alpha consistent.
  4. Clamp colour <= alpha, force the top-left pixel to (0,0,0,0) (the loader keys on
     it), and write a 32-bit BI_RGB BMP, bottom-up, BGRA.

Usage:
  nba98pic.py portrait in.png OUT.BMP [--feather 0.6] [--zoom 1.0] [--dx 0] [--dy 0]
  nba98pic.py action   in.png OUT.BMP ...
  add --preview prev.png to also write a 4x preview composited over grey and red.
"""
import argparse, struct, sys
import numpy as np
from PIL import Image

SIZES = {'portrait': (103, 118), 'action': (146, 205)}


def premultiply(im):
    a = np.asarray(im.convert('RGBA')).astype(np.float32) / 255.0
    a[..., :3] *= a[..., 3:4]
    return a


def resize_premult(p, size):
    """Resize premultiplied float RGBA with a box (area) filter, channel by channel."""
    out = [np.asarray(Image.fromarray(p[..., c], 'F').resize(size, Image.BOX)) for c in range(4)]
    return np.dstack(out)


def blur_premult(p, sigma):
    """Separable Gaussian blur of all four premultiplied channels together."""
    if sigma <= 0:
        return p
    r = max(1, int(np.ceil(sigma * 3)))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    q = np.pad(p, ((r, r), (r, r), (0, 0)))
    q = sum(k[i] * q[:, i:i + p.shape[1] , :] for i in range(2 * r + 1))
    q = sum(k[i] * q[i:i + p.shape[0], :, :] for i in range(2 * r + 1))
    return q


def feather_alpha(p, sigma):
    """Soften only the edge: blur the alpha, keep the picture detail sharp.
    Straight colour is recovered (c / a), bled outward into transparent pixels so the
    widened edge has real colour, then premultiplied again by the softened alpha."""
    if sigma <= 0:
        return p
    a = p[..., 3:4]
    col = np.where(a > 1e-3, p[..., :3] / np.maximum(a, 1e-3), 0)
    have = (a[..., 0] > 1e-3)
    for _ in range(int(np.ceil(sigma * 3)) + 1):            # bleed edge colour outward
        acc = np.zeros_like(col); n = np.zeros(have.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            acc += np.roll(col * have[..., None], (dy, dx), (0, 1)); n += np.roll(have, (dy, dx), (0, 1))
        fill = (~have) & (n > 0)
        col[fill] = acc[fill] / n[fill][:, None]; have = have | fill
    a2 = blur_premult(np.repeat(a, 4, 2), sigma)[..., :1]
    return np.dstack([np.clip(col, 0, 1) * a2, a2])


def smooth_outline(im, sigma):
    """Round off a hand-cut (pencil, stepped) outline at source resolution: blur the mask
    and threshold it again, so the shape is smooth before it is scaled down.  Only the
    outline moves (by at most about sigma pixels); colours are untouched."""
    a = np.asarray(im).astype(np.float32)
    m = (a[..., 3] >= 128).astype(np.float32)
    mb = blur_premult(np.repeat(m[..., None], 4, 2), sigma)[..., 0]
    new = (mb >= 0.5)
    col = a[..., :3].copy()
    have = m > 0
    for _ in range(int(np.ceil(sigma)) + 2):                # give newly included pixels a colour
        acc = np.zeros_like(col); n = np.zeros(have.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            acc += np.roll(col * have[..., None], (dy, dx), (0, 1)); n += np.roll(have, (dy, dx), (0, 1))
        fill = (~have) & (n > 0)
        col[fill] = acc[fill] / n[fill][:, None]; have = have | fill
    out = np.dstack([col, new * 255.0]).astype(np.uint8)
    out[~new] = 0
    return Image.fromarray(out, 'RGBA')


def fit(im, size, zoom, dx, dy):
    """Scale the source to fill the target height (times zoom), centre it, apply offsets
    (in game pixels).  Returns a source-resolution RGBA canvas with the target aspect."""
    tw, th = size
    sw, sh = im.size
    # work at an integer multiple of the target so the final box filter is clean
    k = max(1, int(np.ceil(max(sw / tw, sh / th))))
    cw, ch = tw * k, th * k
    scale = ch / sh * zoom
    nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
    # Resample each premultiplied channel as float.  Pillow's own RGBA resize premultiplies
    # again internally, and Lanczos rings at a hard cut-out edge (bright halo pixels).
    filt = Image.BILINEAR if scale >= 1 else Image.BOX
    p = premultiply(im)
    chans = [np.asarray(Image.fromarray(p[..., c], 'F').resize((nw, nh), filt)) for c in range(4)]
    src = np.clip(np.dstack(chans), 0, 1)
    canvas = np.zeros((ch, cw, 4), np.float32)
    ox, oy = (cw - nw) // 2 + dx * k, (ch - nh) // 2 + dy * k
    x0, y0, x1, y1 = max(ox, 0), max(oy, 0), min(ox + nw, cw), min(oy + nh, ch)
    canvas[y0:y1, x0:x1] = src[y0 - oy:y1 - oy, x0 - ox:x1 - ox]
    return canvas  # premultiplied


KEY = (255, 0, 255)          # magenta: transparent in a hand-edited 24-bit BMP (Paint)

# Agreed EA-matching settings for portraits ("option C", 2026-10-29): measured against EA's
# portraits, the head fills the frame crown to chin, edge fade 3 to 4 px, edge pixels 0.85.
PORTRAIT = dict(zoom=0.97, smooth=3.0, feather=0.6, fringe=0.85)


def load_cutout(path):
    """RGBA cut-out from a PNG with alpha, or a 24-bit image keyed with pure magenta."""
    im = Image.open(path)
    if im.mode in ('RGBA', 'LA') or 'transparency' in im.info:
        return im.convert('RGBA')
    a = np.asarray(im.convert('RGB'))
    key = (a[..., 0] >= 250) & (a[..., 1] <= 5) & (a[..., 2] >= 250)
    if not key.any():
        sys.exit('%s has no alpha channel and no magenta (255,0,255) background. '
                 'Cut the player out first.' % path)
    out = np.dstack([a, np.where(key, 0, 255).astype(np.uint8)])
    out[key, :3] = 0
    return Image.fromarray(out, 'RGBA')


def build(path, kind, feather=None, zoom=None, dx=0, dy=0, fringe=None, smooth=None, crop=True):
    if kind == 'portrait':
        d = PORTRAIT
    else:
        d = dict(zoom=1.0, smooth=0.0, feather=0.6, fringe=0.85)
    feather = d['feather'] if feather is None else feather
    zoom = d['zoom'] if zoom is None else zoom
    fringe = d['fringe'] if fringe is None else fringe
    smooth = d['smooth'] if smooth is None else smooth
    im = load_cutout(path) if isinstance(path, str) else path.convert('RGBA')
    if crop:                                              # frame the head itself, not the canvas
        bb = im.getbbox()
        if bb: im = im.crop(bb)
    if smooth > 0:
        im = smooth_outline(im, smooth)
    size = SIZES[kind]
    p = fit(im, size, zoom, dx, dy)
    p = np.clip(p, 0, 1)
    p = resize_premult(p, size)
    p = feather_alpha(np.clip(p, 0, 1), feather)
    p[..., :3] = np.minimum(p[..., :3], p[..., 3:4])      # premultiplied invariant
    # EA's edge pixels are darker than the face (about 0.7 of the interior brightness), which
    # reads as a soft dark outline against any background.  Darken partly transparent pixels.
    edge = (p[..., 3] > 0) & (p[..., 3] < 0.999)
    p[edge, :3] *= fringe
    out = (p * 255 + 0.5).astype(np.uint8)
    out[out[..., 3] == 0] = 0                             # fully transparent = 0,0,0,0
    if out[0, 0, 3]:
        print('warning: top-left pixel is not transparent; forcing it (the loader keys on it)')
    out[0, 0] = 0
    return Image.fromarray(out, 'RGBA')


def write_bmp32(im, path):
    """32-bit BI_RGB BMP, bottom-up, BGRA (alpha in the 4th byte)."""
    w, h = im.size
    px = im.tobytes('raw', 'BGRA')
    data = b''.join(px[y * w * 4:(y + 1) * w * 4] for y in range(h - 1, -1, -1))
    hdr = struct.pack('<2sIHHI', b'BM', 54 + len(data), 0, 0, 54)
    ih = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 32, 0, len(data), 2835, 2835, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + ih + data)


def preview(im, path):
    """Composite the premultiplied result the way the game does, over two backgrounds."""
    p = np.asarray(im).astype(np.float32)
    a = p[..., 3:4] / 255.0
    tiles = []
    for bg in ((90, 90, 90), (150, 20, 25)):
        tiles.append(np.clip(p[..., :3] + (1 - a) * np.array(bg, np.float32), 0, 255))
    w, h = im.size
    Image.fromarray(np.hstack(tiles).astype(np.uint8)).resize((w * 8, h * 4), Image.NEAREST).save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('kind', choices=SIZES)
    ap.add_argument('src')
    ap.add_argument('out', help='8.3 name ending in .BMP, e.g. MJORDAN.BMP')
    ap.add_argument('--feather', type=float, help='alpha blur in game pixels (portrait default 0.6)')
    ap.add_argument('--fringe', type=float, help='brightness of partly transparent edge pixels (default 0.85; 1 = none)')
    ap.add_argument('--smooth', type=float, help='round off a stepped hand-cut outline, in source pixels (portrait default 3)')
    ap.add_argument('--zoom', type=float, help='head size in the frame (portrait default 0.97)')
    ap.add_argument('--dx', type=int, default=0)
    ap.add_argument('--dy', type=int, default=0)
    ap.add_argument('--preview')
    a = ap.parse_args()
    import os
    base = os.path.basename(a.out)
    stem, _, ext = base.upper().partition('.')
    if ext != 'BMP' or not (1 <= len(stem) <= 8):
        sys.exit('output name must be 8.3 and end in .BMP (the game only loads loose files named *bmp)')
    im = build(a.src, a.kind, a.feather, a.zoom, a.dx, a.dy, a.fringe, a.smooth)
    write_bmp32(im, a.out)
    if a.preview:
        preview(im, a.preview)
    al = np.asarray(im)[..., 3]
    print('%s: %dx%d, %d opaque, %d soft-edge, %d transparent pixels' % (
        a.out, im.size[0], im.size[1], (al == 255).sum(), ((al > 0) & (al < 255)).sum(), (al == 0).sum()))


if __name__ == '__main__':
    main()
