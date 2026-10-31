#!/usr/bin/env python3
"""Zips a folder (dist/) with stable ordering. Refuses anything that looks like a GTA game file.
Usage: scripts/make_zip.py <folder> <out.zip>"""
import os, sys, zipfile, time

FORBIDDEN_EXT = {'.img', '.scm', '.dat', '.ide', '.ipl', '.col', '.dff', '.txd', '.ifp', '.set', '.b', '.cfg'}
FORBIDDEN_NAMES = {'gta_sa.exe', 'gta-sa.exe', 'gta_sa_compact.exe', 'eax.dll', 'ogg.dll', 'vorbis.dll', 'vorbisfile.dll'}

def main():
    src, out = sys.argv[1], sys.argv[2]
    files = []
    for root, dirs, names in os.walk(src):
        dirs.sort()
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = os.path.relpath(full, src).replace(os.sep, '/')
            low = n.lower()
            if low in FORBIDDEN_NAMES or os.path.splitext(low)[1] in FORBIDDEN_EXT:
                sys.exit(f'RECUSADO: {rel} parece arquivo do jogo; nao empacotado')
            files.append((rel, full))
    tmp = out + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        dirs_done = set()
        for rel, full in files:
            parts = rel.split('/')[:-1]
            for i in range(1, len(parts) + 1):
                d = '/'.join(parts[:i]) + '/'
                if d not in dirs_done:
                    dirs_done.add(d)
                    zi = zipfile.ZipInfo(d, time.localtime(os.path.getmtime(full))[:6]); zi.external_attr = 0o40755 << 16 | 0x10
                    z.writestr(zi, b'')
            z.write(full, rel)
    os.replace(tmp, out)
    print(f'{out}: {len(files)} arquivos')

if __name__ == '__main__':
    main()
