# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the browser demo, docs/index.html: the minified dashboard, the pretend box (demo_hub.js answers the
dashboard's /api calls; demo_box.js draws the box and its screen) and the made-up data (seed.json).

From the repo root:
    npm install terser
    node tools/minify.js dashboard/dashboard.html dashboard_built.html
    python3 docs/demo/build_demo.py dashboard_built.html

The box's 5x7 text font is read from the Adafruit GFX library's glcdfont.c (BSD-2-Clause, see
THIRD_PARTY_NOTICES.md). It is looked for in the usual Arduino libraries folders; give its path with
--gfx path/to/glcdfont.c if it is somewhere else.
"""
import argparse, base64, hashlib, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('dashboard', help='the minified dashboard (from tools/minify.js)')
ap.add_argument('--gfx', help='path to Adafruit_GFX_Library/glcdfont.c')
ap.add_argument('--out', default=os.path.join(HERE, '..', 'index.html'))
a = ap.parse_args()

gfx = a.gfx or next((p for p in (os.path.expanduser(x) for x in (
    '~/Arduino/libraries/Adafruit_GFX_Library/glcdfont.c',
    '~/Documents/Arduino/libraries/Adafruit_GFX_Library/glcdfont.c')) if os.path.exists(p)), None)
if not gfx: raise SystemExit('glcdfont.c not found: install the Adafruit GFX library or pass --gfx')

html = open(a.dashboard, encoding='utf-8').read()
seed = json.load(open(os.path.join(HERE, 'seed.json'), encoding='utf-8'))
seed['ver'] = hashlib.sha1(json.dumps(seed, sort_keys=True).encode()).hexdigest()[:10]   # a new demo build starts its visitors over
src = open(gfx, encoding='utf-8').read()
body = src[src.index('{', src.index('font[]')) + 1: src.rindex('}')]
font = bytes(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{2}', body))
assert len(font) == 1280, len(font)
clk = '\n'.join(l for l in open(os.path.join(HERE, 'clock_glyphs.js'), encoding='utf-8').read().split('\n') if not l.startswith('//')).strip()
hub = open(os.path.join(HERE, 'demo_hub.js'), encoding='utf-8').read().replace('/*__SEED__*/null', json.dumps(seed, separators=(',', ':')))
box = open(os.path.join(HERE, 'demo_box.js'), encoding='utf-8').read().replace('/*__FONT__*/', base64.b64encode(font).decode()).replace('/*__CLK__*/', clk)
esc = lambda s: s.replace('</', '<\\/')
assert html.count('<head>') == 1 and html.count('</body>') == 1
html = html.replace('<head>', '<head>\n<script>\n' + esc(hub) + '\n</script>', 1)
html = html.replace('</body>', '<script>\n' + esc(box) + '\n</script>\n</body>', 1)
html = re.sub(r'<title>[^<]*</title>', '<title>Life Logger demo · Aspect Wave</title>', html, count=1)
html = html.replace('<head>', '<head>\n<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns=%27http://www.w3.org/2000/svg%27 viewBox=%270 0 32 32%27%3E%3Crect width=%2732%27 height=%2732%27 rx=%276%27 fill=%27%231F3A5F%27/%3E%3Ctext x=%2716%27 y=%2723%27 font-size=%2719%27 text-anchor=%27middle%27 fill=%27white%27 font-family=%27Arial%27 font-weight=%27bold%27%3EL%3C/text%3E%3C/svg%3E">\n<meta name="description" content="Try Life Logger, the carry-around maintenance logbook, in your browser with made-up data.">', 1)
open(a.out, 'w', encoding='utf-8').write(html)
print(os.path.normpath(a.out), len(html.encode()) // 1024, 'kB')
