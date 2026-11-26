# SPDX-FileCopyrightText: 2026 Aspect Wave LLC
# SPDX-License-Identifier: GPL-3.0-or-later
# .lbk = "LOGBOOK1 {json}\n" + gzipped dashboard + firmware .bin
import gzip, json, sys
html, binf, out, ver = sys.argv[1:5]
gz = gzip.compress(open(html, 'rb').read(), compresslevel=9, mtime=0)
b = open(binf, 'rb').read()
assert b[0] == 0xE9
assert len(b) <= 0x180000, 'firmware bigger than its 1.5 MB app slot (partitions.csv)'
head = 'LOGBOOK1 ' + json.dumps({'fw': ver, 'ui': len(gz), 'bin': len(b)}, separators=(',', ':')) + '\n'
open(out, 'wb').write(head.encode() + gz + b)
print(head.strip(), '->', len(head) + len(gz) + len(b), 'bytes')
