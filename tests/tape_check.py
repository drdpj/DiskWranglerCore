#!/usr/bin/env python3
# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
#
# tape_check.py: an independent reader of tape images, written from the UEF 0.10 and TZX 1.20 specifications and the
# rules in diskwrangler/tape/records.h, sharing no code with the core (gzip is Python's own). It prints a tape the way
# `tapetest --dump` does, and compares the two.
#
#   python3 -I tape_check.py --dump FILE              this reader's dump
#   python3 -I tape_check.py TAPETEST [FILE|DIR ...]  tapetest's hand-made tapes (--write-samples) and every tape
#                                                     given, dumped by both; any difference fails. Exit 0: all agree.
import gzip
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

UEF_MAGIC = b"UEF File!\0"
TZX_MAGIC = b"ZXTape!\x1a"
TAIL = -1
TSTATES = 3500000.0


def h16(b):
    return hashlib.sha1(b).hexdigest()[:16]


def le(b, at, n):
    v = 0
    for i in range(n):
        if at + i < len(b):
            v |= b[at + i] << (8 * i)
    return v


def f32(b, at):
    return struct.unpack_from("<f", b, at)[0]


# ----- the file

def tzx_len(i, b, at):
    """Body length of TZX block id i whose body starts at b[at], or None if the file ends first."""
    def have(n):
        return at + n <= len(b)
    u8 = lambda o: b[at + o]
    u16 = lambda o: le(b, at + o, 2)
    u24 = lambda o: le(b, at + o, 3)
    u32 = lambda o: le(b, at + o, 4)
    n = None
    fixed = {0x12: 4, 0x20: 2, 0x23: 2, 0x24: 2, 0x22: 0, 0x25: 0, 0x27: 0, 0x34: 8, 0x5A: 9}
    if i in fixed:
        n = fixed[i]
    elif i == 0x10:
        n = 4 + u16(2) if have(4) else None
    elif i == 0x11:
        n = 0x12 + u24(0x0F) if have(0x12) else None
    elif i == 0x13:
        n = 1 + 2 * u8(0) if have(1) else None
    elif i == 0x14:
        n = 0x0A + u24(7) if have(0x0A) else None
    elif i == 0x15:
        n = 8 + u24(5) if have(8) else None
    elif i in (0x16, 0x17):                 # length [00,01,02,03]: the whole block
        n = (u32(0) if u32(0) >= 4 else None) if have(4) else None
    elif i in (0x18, 0x19, 0x2A, 0x2B):
        n = 4 + u32(0) if have(4) else None
    elif i in (0x21, 0x30):
        n = 1 + u8(0) if have(1) else None
    elif i == 0x26:
        n = 2 + 2 * u16(0) if have(2) else None
    elif i in (0x28, 0x32):
        n = 2 + u16(0) if have(2) else None
    elif i == 0x31:
        n = 2 + u8(1) if have(2) else None
    elif i == 0x33:
        n = 1 + 3 * u8(0) if have(1) else None
    elif i == 0x35:
        n = 0x14 + u32(0x10) if have(0x14) else None
    elif i == 0x40:
        n = 4 + u24(1) if have(4) else None
    else:                                   # extension rule
        n = 4 + u32(0) if have(4) else None
    if n is None or at + n > len(b):
        return None
    return n


def read_tape(path):
    raw = open(path, "rb").read()
    gz = raw[:3] == b"\x1f\x8b\x08"
    plain = gzip.decompress(raw) if gz else raw
    suffix = os.path.splitext(path)[1].lower().lstrip(".")
    chunks = []
    readonly = False
    if plain.startswith(UEF_MAGIC) and len(plain) >= 12:
        kind, ver = "UEF", (plain[11], plain[10])
        p = 12
        while p < len(plain):
            if p + 6 > len(plain) or p + 6 + le(plain, p + 2, 4) > len(plain):
                chunks.append((TAIL, plain[p:]))
                readonly = True
                break
            n = le(plain, p + 2, 4)
            chunks.append((le(plain, p, 2), plain[p + 6:p + 6 + n]))
            p += 6 + n
    elif not gz and plain.startswith(TZX_MAGIC) and len(plain) >= 10:
        kind, ver = "TZX", (plain[8], plain[9])
        p = 10
        while p < len(plain):
            n = tzx_len(plain[p], plain, p + 1)
            if n is None:
                chunks.append((TAIL, plain[p:]))
                readonly = True
                break
            chunks.append((plain[p], plain[p + 1:p + 1 + n]))
            p += 1 + n
    elif not gz and suffix == "tap":
        kind, ver = "TAP", (0, 0)
        p = 0
        while p < len(plain):
            n = le(plain, p, 2) if p + 2 <= len(plain) else None
            if n is None or p + 2 + n > len(plain):
                chunks.append((TAIL, plain[p:]))
                readonly = True
                break
            chunks.append((0, plain[p + 2:p + 2 + n]))
            p += 2 + n
    else:
        raise ValueError("not a tape")
    return kind, gz, ver, chunks, readonly


# ----- pieces

def serial(bits=8, parity="N", stop=1, extra=0, atom=0, explicit=0, baud=1200, base=1200.0):
    return ("serial", bits, parity, stop, extra, atom, explicit, baud, base)


def pulse(std=0, pilot=0, pulses=0, s1=0, s2=0, zero=0, one=0, used=8, pause=0):
    return ("pulse", std, pilot, pulses, s1, s2, zero, one, used, pause)


def std_coding(data, pause):
    return pulse(1, 2168, 8063 if data and data[0] < 128 else 3223, 667, 735, 855, 1710, 8, pause)


def coding_text(c):
    if c[0] == "serial":
        _, bits, par, stop, extra, atom, explicit, baud, base = c
        return "serial %d%s%d extra=%d atom=%d bits=%d baud=%d base=%.3f" % (bits, par, stop, extra, atom, explicit, baud, base)
    _, std, pilot, pulses, s1, s2, zero, one, used, pause = c
    return "pulse std=%d pilot=%dx%d sync=%d,%d bit=%d,%d used=%d pause=%d" % (std, pilot, pulses, s1, s2, zero, one, used, pause)


def frame_bits(body):
    """&0102: 8N1 bytes from the stored bits (least significant first); (bytes, problem)."""
    if not body:
        return b"", False
    n = min((len(body) - 1) * 8, len(body) * 8 - body[0])
    bit = lambda i: (body[1 + i // 8] >> (i % 8)) & 1
    out = bytearray()
    i = 0
    while i < n:
        if bit(i):
            i += 1
            continue
        if i + 10 > n:
            return bytes(out), True
        v = sum(bit(i + 1 + k) << k for k in range(8))
        if not bit(i + 9):
            return bytes(out), True
        out.append(v)
        i += 10
    return bytes(out), False


def uef_pieces(chunks):
    out = []                                   # [type, first, last, bytes, coding, seconds, stop, problem]
    base, baud = 1200.0, 1200
    run_open = False
    modal = (0x0113, 0x0115, 0x0117, 0x0101, 0x0103)
    for i, (cid, b) in enumerate(chunks):
        c = serial(baud=baud, base=base)
        if cid == 0x0113 and len(b) >= 4:
            base = f32(b, 0)
        elif cid == 0x0117 and len(b) >= 2:
            baud = le(b, 0, 2)
        if cid in modal:
            if run_open:
                out[-1][2] = i
            else:
                out.append(["info", i, i, None, None, 0, False, False])
            continue
        data = None
        problem = False
        if cid == 0x0100:
            data = b
        elif cid == 0x0102:
            data, problem = frame_bits(b)
            c = serial(baud=baud, base=base, explicit=1)
        elif cid == 0x0104 and len(b) >= 3:
            stop = struct.unpack_from("<b", b, 2)[0]
            c = serial(b[0], chr(b[1]), abs(stop), int(stop < 0), baud=baud, base=base)
            data = b[3:]
        elif cid == 0x0105:
            data = b
            c = serial(atom=1, baud=baud, base=base)
        if data is not None:
            if run_open and out[-1][4] == c and not out[-1][7] and not problem:
                out[-1][3] += data
                out[-1][2] = i
            else:
                out.append(["data", i, i, bytes(data), c, 0, False, problem])
                run_open = True
            continue
        run_open = False
        cycles = 4 if baud == 300 else 1
        if cid == 0x0110:
            out.append(["tone", i, i, None, None, le(b, 0, 2) / (2 * base), False, False])
        elif cid == 0x0111:
            out.append(["tone", i, i, None, None, (le(b, 0, 2) + le(b, 2, 2)) / (2 * base) + 10.0 * cycles / base, False, False])
        elif cid == 0x0114:
            s = 0.0
            for k in range(le(b, 0, 3)):
                if 5 + k // 8 >= len(b):
                    break
                s += 1 / (2 * base) if (b[5 + k // 8] >> (7 - k % 8)) & 1 else 1 / base
            out.append(["tone", i, i, None, None, s, False, False])
        elif cid == 0x0112:
            out.append(["gap", i, i, None, None, le(b, 0, 2) / (2 * base), False, False])
        elif cid == 0x0116:
            out.append(["gap", i, i, None, None, f32(b, 0) if len(b) >= 4 else 0.0, False, False])
        else:
            out.append(["info", i, i, None, None, 0, False, False])
    return out


def tzx_pieces(kind, chunks):
    out = []
    for i, (cid, b) in enumerate(chunks):
        if kind == "TAP" and cid == 0:
            out.append(["data", i, i, b, std_coding(b, 1000), 0, False, False])
        elif cid == TAIL:
            out.append(["info", i, i, None, None, 0, False, False])
        elif cid == 0x10:
            out.append(["data", i, i, b[4:], std_coding(b[4:], le(b, 0, 2)), 0, False, False])
        elif cid == 0x11:
            c = pulse(0, le(b, 0, 2), le(b, 10, 2), le(b, 2, 2), le(b, 4, 2), le(b, 6, 2), le(b, 8, 2), b[12], le(b, 13, 2))
            out.append(["data", i, i, b[0x12:], c, 0, False, False])
        elif cid == 0x14:
            c = pulse(0, 0, 0, 0, 0, le(b, 0, 2), le(b, 2, 2), b[4], le(b, 5, 2))
            out.append(["data", i, i, b[0x0A:], c, 0, False, False])
        elif cid == 0x12:
            out.append(["tone", i, i, None, None, le(b, 0, 2) * le(b, 2, 2) / TSTATES, False, False])
        elif cid == 0x13:
            out.append(["tone", i, i, None, None, sum(le(b, 1 + 2 * k, 2) for k in range(b[0])) / TSTATES, False, False])
        elif cid == 0x20:
            out.append(["gap", i, i, None, None, le(b, 0, 2) / 1000.0, le(b, 0, 2) == 0, False])
        elif cid in (0x15, 0x16, 0x17, 0x18, 0x19):
            out.append(["signal", i, i, None, None, 0, False, False])
        else:
            out.append(["info", i, i, None, None, 0, False, False])
    return out


def dump(path):
    kind, gz, ver, chunks, readonly = read_tape(path)
    lines = ["kind %s gzip %d version %d.%d chunks %d readonly %d" % (kind, int(gz), ver[0], ver[1], len(chunks), int(readonly))]
    for i, (cid, b) in enumerate(chunks):
        lines.append("chunk %d id %d len %d %s" % (i, cid, len(b), h16(b)))
    ps = uef_pieces(chunks) if kind == "UEF" else tzx_pieces(kind, chunks)
    for i, (t, first, last, data, coding, secs, stop, problem) in enumerate(ps):
        s = "piece %d %s %d-%d" % (i, t, first, last)
        if t == "data":
            s += " bytes %d %s %s%s" % (len(data), h16(data), coding_text(coding), " problem" if problem else "")
        if t in ("tone", "gap"):
            s += " seconds %.6f" % secs
        if stop:
            s += " stop"
        lines.append(s)
    return lines


def tapes_in(arg):
    if os.path.isdir(arg):
        return sorted(os.path.join(arg, n) for n in os.listdir(arg)
                      if os.path.splitext(n)[1].lower() in (".uef", ".tzx", ".cdt", ".tap"))
    return [arg]


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--dump":
        print("\n".join(dump(sys.argv[2])))
        return 0
    if len(sys.argv) < 2:
        print(__doc__ if __doc__ else "usage: tape_check.py TAPETEST [FILE|DIR ...]")
        return 2
    tapetest = sys.argv[1]
    bad = 0
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([tapetest, "--write-samples", tmp], check=True, timeout=60)
        files = tapes_in(tmp)
        for a in sys.argv[2:]:
            files += tapes_in(a)
        for f in files:
            total += 1
            mine = dump(f)
            r = subprocess.run([tapetest, "--dump", f], capture_output=True, text=True, timeout=60)
            theirs = r.stdout.splitlines()
            if mine != theirs:
                bad += 1
                print("DIFFER: %s" % f)
                for k in range(max(len(mine), len(theirs))):
                    a = mine[k] if k < len(mine) else "<none>"
                    b = theirs[k] if k < len(theirs) else "<none>"
                    if a != b:
                        print("  tape_check: %s\n  tapetest:   %s" % (a, b))
                        break
            else:
                print("agree: %s (%d lines)" % (os.path.basename(f), len(mine)))
    print("%d of %d tapes read the same by both readers" % (total - bad, total))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
