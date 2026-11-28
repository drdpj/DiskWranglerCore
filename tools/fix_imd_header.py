#!/usr/bin/env python3
# Part of DiskWranglerCore, used by Acorn Disk Wrangler, CP/M Disk Wrangler and FLEX Disk Wrangler.
# Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
"""Repair two faults in ImageDisk files from other programs, so the DiskWrangler tools read them.

  fix_imd_header.py [--dry-run] [--no-backup] FILE...

1. A first line that is not `IMD <version>: <date>`. Early dumpfloppy, for example, writes
   `dumpfloppy 1: 18/01/2013 19:10:22`. The DiskWrangler tools only take a file as ImageDisk when it starts with `IMD `.
2. Empty track records with an invalid mode byte at the end. HxC Floppy Emulator 2.12.1.1 writes records such as
   `80 28 00 00 00` (mode 0x80, cylinder 40, head 0, no sectors) after the last real track. They hold no data and the
   tools reject the file as malformed.

For each FILE:
- the file must parse as ImageDisk (header ended by 0x1A, then track records running exactly to the end of the file),
  apart from a run of sectorless track records with an invalid mode at the very end, which is cut off; anything else
  wrong and the file is left alone and reported;
- a file that already starts `IMD ` keeps its first line;
- the first line becomes `IMD 1.18: <date>`, keeping the old line's `dd/mm/yyyy hh:mm:ss` if it has one (else the file's
  modification time), and the old line is kept as the first line of the comment (`Original header: ...`);
- the original is saved as FILE.orig (refused if that exists) unless --no-backup.

Exit status: 0 if every file is fine or was fixed, 1 if any file could not be (not ImageDisk, or backup in the way).
"""
import datetime
import os
import re
import sys


def check_tracks(b, i):
    """Walk the track records from offset i; return (None, i) if they end exactly at EOF, else (error message, offset of
    the record that is wrong)."""
    n = 0
    while i < len(b):
        start = i
        if i + 5 > len(b):
            return f'track header at offset {i} is cut short', start
        mode, cyl, head, nsec, size = b[i:i + 5]
        if mode > 5 or (head & 0x3F) > 1 or size > 6:
            return f'track header at offset {i} is not valid', start
        i += 5 + nsec                       # sector numbering map
        if head & 0x80:
            i += nsec                       # cylinder map
        if head & 0x40:
            i += nsec                       # head map
        ssize = 128 << size
        for _ in range(nsec):
            if i >= len(b):
                return f'track {n} is cut short', start
            t = b[i]
            i += 1
            if t > 8:
                return f'sector record type {t} at offset {i - 1} is not valid', start
            if t:
                i += ssize if t % 2 else 1
        n += 1
    if i != len(b):
        return f'track {n - 1} runs past the end of the file', start
    return None, i


def empty_junk_tail(b, i):
    """True if b[i:] is one or more 5-byte sectorless track records with an invalid mode byte, running to EOF."""
    if i >= len(b) or (len(b) - i) % 5:
        return False
    for j in range(i, len(b), 5):
        mode, cyl, head, nsec, size = b[j:j + 5]
        if nsec != 0 or mode <= 5:
            return False
    return True


def fix(path, dry_run, backup):
    b = open(path, 'rb').read()
    end = b.find(b'\x1a')
    if end < 0:
        return False, 'no 0x1A end of header: not ImageDisk'
    err, at = check_tracks(b, end + 1)
    msgs = []
    if err:
        if not empty_junk_tail(b, at):
            return False, f'not a whole ImageDisk file ({err})'
        msgs.append(f'cut {(len(b) - at) // 5} empty track record(s) with an invalid mode ({len(b) - at} bytes '
                    f'at offset {at})')
        b = b[:at]
    if not b.startswith(b'IMD '):
        b, m = fix_signature(path, b, end)
        msgs.append(m)
    if not msgs:
        return True, 'nothing to fix'
    msg = '; '.join(msgs)
    if dry_run:
        return True, 'would ' + msg
    if backup:
        orig = path + '.orig'
        if os.path.exists(orig):
            return False, f'{orig} already exists; not changed'
        os.rename(path, orig)
    with open(path, 'wb') as f:
        f.write(b)
    return True, 'fixed: ' + msg


def fix_signature(path, b, end):
    hdr = b[:end]
    nl = hdr.find(b'\n')
    first = (hdr if nl < 0 else hdr[:nl]).rstrip(b'\r')
    rest = b'' if nl < 0 else hdr[nl + 1:]
    m = re.search(rb'(\d\d/\d\d/\d{4} \d\d:\d\d:\d\d)', first)
    if m:
        date = m.group(1)
    else:
        mt = datetime.datetime.fromtimestamp(os.path.getmtime(path))
        date = mt.strftime('%d/%m/%Y %H:%M:%S').encode()
    newhdr = b'IMD 1.18: ' + date + b'\r\n' + b'Original header: ' + first + b'\r\n' + rest
    return newhdr + b[end:], f'change first line {first.decode("latin-1")!r} -> {("IMD 1.18: " + date.decode())!r}'


def main(argv):
    args = [a for a in argv if not a.startswith('--')]
    opts = {a for a in argv if a.startswith('--')}
    bad_opts = opts - {'--dry-run', '--no-backup'}
    if bad_opts or not args:
        print(__doc__.strip().split('\n\n')[1], file=sys.stderr)
        return 2
    ok_all = True
    for p in args:
        ok, msg = fix(p, '--dry-run' in opts, '--no-backup' not in opts)
        ok_all &= ok
        print(f'{p}: {msg}')
    return 0 if ok_all else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
