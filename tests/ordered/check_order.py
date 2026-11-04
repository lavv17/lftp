#!/usr/bin/env python3
"""Check that the entries of a directory were created in the expected order.

The actual order is either the creation (birth) time of each entry, or with
--readdir the order the filesystem lists them in (on FAT this is the order
the entries were written, which is what car stereos and players use).

The expected order is mirror's: subdirectories first (they are made before
any file is transferred), then files, each group sorted by KEY:

  name      byte order              natural   "track 2" before "track 10"
  oldest    oldest mtime first      newest    newest mtime first
  size      smallest first          KEY-desc  any of these reversed

lftp copies the mtime and size, so they are read from the target itself.

usage: check_order.py DIR KEY [--readdir] [-v]
exit 0 when the order matches, 1 when it does not.
"""
import functools
import os
import subprocess
import sys


def natural_cmp(a, b):
    # a line-by-line port of natural_strcmp in src/FileSet.cc
    p, q = a.encode(), b.encode()
    i = j = 0
    dig = lambda c: 48 <= c <= 57
    while i < len(p) and j < len(q):
        if dig(p[i]) and dig(q[j]):
            while i < len(p) and p[i] == 48:
                i += 1
            while j < len(q) and q[j] == 48:
                j += 1
            si, sj = i, j
            while i < len(p) and dig(p[i]):
                i += 1
            while j < len(q) and dig(q[j]):
                j += 1
            if i - si != j - sj:
                return -1 if i - si < j - sj else 1
            if p[si:i] != q[sj:j]:
                return -1 if p[si:i] < q[sj:j] else 1
            continue
        if p[i] != q[j]:
            return -1 if p[i] < q[j] else 1
        i += 1
        j += 1
    if i < len(p) or j < len(q):
        return 1 if i < len(p) else -1
    return (p > q) - (p < q)


natural_key = functools.cmp_to_key(natural_cmp)


def birth_times(d, names):
    paths = [os.path.join(d, n) for n in names]
    out = subprocess.run(["stat", "-c", "%.9W", "--"] + paths,
                         capture_output=True, text=True, check=True).stdout.split()
    bt = {n: int(t.replace(".", "")) for n, t in zip(names, out)}
    if 0 in bt.values():
        sys.exit("this filesystem does not record creation time; use --readdir")
    return bt


def expected_sequences(d, names, key):
    """The orders mirror must keep, as lists of names.

    name/natural: mirror:order puts files first and folders last (pattern
    "*/"), so it is one list; -desc reverses all of it. The other keys rank
    files by their own size or date; a folder's size or date is not the
    one mirror sorted by, so files and folders are checked as two lists.
    """
    base, _, desc = key.partition("-")
    st = {n: os.lstat(os.path.join(d, n)) for n in names}
    if base == "natural":
        k = natural_key
    elif base == "name":
        k = lambda n: n.encode()
    elif base == "size":
        k = lambda n: (st[n].st_size, n.encode())
    elif base == "oldest":
        k = lambda n: (st[n].st_mtime_ns, n.encode())
    elif base == "newest":
        k = lambda n: (-st[n].st_mtime_ns, n.encode())
    else:
        sys.exit(f"unknown key {key}")
    isdir = lambda n: os.path.isdir(os.path.join(d, n))
    dirs = sorted([n for n in names if isdir(n)], key=k)
    files = sorted([n for n in names if not isdir(n)], key=k)
    if base in ("name", "natural"):
        seqs = [files + dirs]
    else:
        seqs = [files, dirs]
    if desc:
        seqs = [list(reversed(q)) for q in seqs]
    return seqs


def main():
    d, key = sys.argv[1], sys.argv[2]
    readdir = "--readdir" in sys.argv
    verbose = "-v" in sys.argv
    listed = os.listdir(d)
    if readdir:
        when = {n: i for i, n in enumerate(listed)}   # exact, no ties
        how = "listing order"
    else:
        when = birth_times(d, listed)                 # ties possible: coarse clock
        how = "creation time"
    bad = []
    for seq in expected_sequences(d, listed, key):
        latest = None
        for i, n in enumerate(seq):
            # wrong only if made strictly before something that should come earlier
            if latest is not None and when[n] < when[latest]:
                bad.append((n, latest))
            if latest is None or when[n] > when[latest]:
                latest = n
    ties = len(when) - len(set(when.values()))
    tie_note = f", {ties} share a timestamp with another" if ties else ""
    print(f"{d}: {len(listed)} entries, {len(bad)} out of order ({key}, by {how}{tie_note})")
    for n, latest in bad[: (len(bad) if verbose else 12)]:
        print(f"  {n!r} was created before {latest!r}")
    return 1 if bad else 0


sys.exit(main())
