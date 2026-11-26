# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: GPL-3.0-or-later

# The CLOCK screen's segment glyphs (lit segments only), as 16-level grey
# bitmaps: drawn 8x bigger and shrunk, so their edges use the OLED's grey levels. Writes the data for the demo
# (JS) and the firmware (C header), so both draw exactly the same pixels.
import base64, os, sys
from PIL import Image, ImageDraw

DIG = {'0': 'abcdef', '1': 'bc', '2': 'abdeg', '3': 'abcdg', '4': 'bcfg', '5': 'acdfg', '6': 'acdefg', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg', '-': 'g'}
L14 = {'S': 'a f g1 g2 c d', 'U': 'b c d e f', 'N': 'f e h m b c', 'M': 'f e h j b c', 'O': 'a b c d e f', 'T': 'a i l', 'E': 'a f g1 e d',
       'W': 'f e k m b c', 'D': 'a b c d i l', 'H': 'f e g1 g2 b c', 'F': 'a f g1 e', 'R': 'a f e g1 g2 b m', 'I': 'a i l d', 'A': 'e f a b c g1 g2'}
K = 8
HERE = os.path.dirname(os.path.abspath(__file__))           # tools/; the outputs go beside the sketch and the demo
SPDX = '// SPDX-FileCopyrightText: 2026 Aspect Wave LLC\n// SPDX-License-Identifier: GPL-3.0-or-later\n'

def seg7(ch, w, h, t, slant=0.07):
    big = Image.new('L', (w * K, h * K), 0); d = ImageDraw.Draw(big)
    x, y, W2, H2, T = K * 0.5, K * 0.5, (w - 1) * K, (h - 1) * K, t * K
    r, m, g = T / 2, y + H2 / 2, T * 0.12
    sk = lambda px, py: (px + slant * (y + H2 - py), py)
    hz = lambda x1, x2, yy: [(x1, yy), (x1 + r, yy - r), (x2 - r, yy - r), (x2, yy), (x2 - r, yy + r), (x1 + r, yy + r)]
    vt = lambda xx, y1, y2: [(xx, y1), (xx + r, y1 + r), (xx + r, y2 - r), (xx, y2), (xx - r, y2 - r), (xx - r, y1 + r)]
    L, R, Tp, B = x + r, x + W2 - r, y + r, y + H2 - r
    segs = {'a': hz(L + g, R - g, Tp), 'g': hz(L + g, R - g, m), 'd': hz(L + g, R - g, B),
            'f': vt(L, Tp + g, m - g), 'b': vt(R, Tp + g, m - g), 'e': vt(L, m + g, B - g), 'c': vt(R, m + g, B - g)}
    for k, pts in segs.items():
        if k in DIG[ch]: d.polygon([sk(*p) for p in pts], fill=255)
    return big.resize((w, h), Image.BOX)

def seg14(ch, w=11, h=16):
    big = Image.new('L', (w * K, h * K), 0); d = ImageDraw.Draw(big)
    x, y, W2, H2 = K, K, (w - 2) * K, (h - 2) * K
    hm, xm = y + H2 / 2, x + W2 / 2
    S = {'a': [(x, y), (x + W2, y)], 'd': [(x, y + H2), (x + W2, y + H2)], 'f': [(x, y), (x, hm)], 'e': [(x, hm), (x, y + H2)], 'b': [(x + W2, y), (x + W2, hm)], 'c': [(x + W2, hm), (x + W2, y + H2)],
         'g1': [(x, hm), (xm, hm)], 'g2': [(xm, hm), (x + W2, hm)], 'i': [(xm, y), (xm, hm)], 'l': [(xm, hm), (xm, y + H2)],
         'h': [(x, y), (xm, hm)], 'j': [(x + W2, y), (xm, hm)], 'k': [(x, y + H2), (xm, hm)], 'm': [(x + W2, y + H2), (xm, hm)]}
    for k in L14[ch].split(): d.line(S[k], fill=255, width=int(K * 1.7))
    return big.resize((w, h), Image.BOX)

# name, chars, size: big HH:MM, seconds, date, and the day's letters (A / P for 12-hour time is the box's own font)
SETS = [('big', '0123456789', lambda c: seg7(c, 18, 34, 4.6)), ('sec', '0123456789', lambda c: seg7(c, 11, 21, 3.6)),
        ('date', '0123456789-', lambda c: seg7(c, 10, 22, 3.6)), ('day', ''.join(L14), seg14)]
out = {}
for name, chars, fn in SETS:
    ims = [fn(c) for c in chars]; w, h = ims[0].size
    nib = bytearray()
    for im in ims: nib += bytes(min(15, round(v / 17)) for v in im.getdata())
    out[name] = (chars, w, h, nib)
js = 'const CLK = {' + ', '.join('%s: {c: %r, w: %d, h: %d, px: Uint8Array.from(atob(%r), c => c.charCodeAt(0))}' % (k, c, w, h, base64.b64encode(bytes(n)).decode()) for k, (c, w, h, n) in out.items()) + '};'
open(os.path.join(HERE, '..', 'docs', 'demo', 'clock_glyphs.js'), 'w').write(SPDX + '// made by tools/clock_glyphs.py: the CLOCK screen\'s segment digits, 0-15 grey per pixel\n' + js + '\n')
# firmware: two pixels to a byte
h = ['// made by tools/clock_glyphs.py: the CLOCK screen\'s segment digits, 4-bit grey, two pixels a byte (high nibble first)', '#pragma once']
for k, (c, w, hh, n) in out.items():
    pk = bytes((n[i] << 4) | (n[i + 1] if i + 1 < len(n) else 0) for i in range(0, len(n), 2))
    h.append('static const char CLK_%s_C[] = "%s"; static const uint8_t CLK_%s_W = %d, CLK_%s_H = %d;' % (k.upper(), c, k.upper(), w, k.upper(), hh))
    h.append('static const uint8_t CLK_%s[] PROGMEM = {%s};' % (k.upper(), ','.join(str(b) for b in pk)))
open(os.path.join(HERE, '..', 'life_logger', 'clock_glyphs.h'), 'w').write(SPDX + '\n'.join(h) + '\n')
print({k: (len(v[0]), v[1], v[2], len(v[3])) for k, v in out.items()})
