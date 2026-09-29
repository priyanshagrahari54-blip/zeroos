#!/usr/bin/env python3
"""Every statistic a desktop core publishes must have a writer.

A `stats` field that nothing increments is not a statistic, it is a
promise the header makes and the code forgets: the dictionary published
a `rejected` counter for its whole life and never incremented it, and
the only way to notice is to read every path in the module and ask what
it did not do.  This check asks that question mechanically, from the
headers, so a statistic declared tomorrow is covered by it immediately.

It is deliberately generous about *where* the write happens: the owning
module, another core that maintains the counter on its behalf, or the
session binding.  What it insists on is that a write exists somewhere.

Usage: tools/stats_write_check.py   (exit 0 = every statistic is written)
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADERS = sorted(glob.glob(os.path.join(
    ROOT, "userspace/desktop/include/zeroos/desktop/*.h")))
SOURCES = os.path.join(ROOT, "userspace/desktop/src")

# A field that is meant to stay at zero is not an orphan: it is a canary,
# and the suite (and the guest, for this one) asserts that it stays there.
CANARY = {
    ("search", "full_scans"): "must stay 0 -- no whole-disk rescans; asserted "
                              "on the host by test_search.c and in the guest by "
                              "the session's search step",
}

TYPES = (r"(?:uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|"
         r"int64_t|int|unsigned|signed|size_t|bool|_Bool|char|float|double)")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def stats_fields(text):
    """Declarator names inside `struct { ... } stats;` / `struct zd_*stats`."""
    names = []
    blocks = re.findall(r"struct\s*\{([^}]*)\}\s*stats\s*;", text)
    blocks += re.findall(r"struct\s+zd_\w*stats\s*\{([^}]*)\}\s*;", text)
    for block in blocks:
        for decl in re.split(r"[;,]", block):
            found = re.match(
                r"^(?:%s\s+)*([a-z_][a-z_0-9]*)\s*(\[[^\]]*\])?$" % TYPES,
                decl.strip())
            if found:
                names.append(found.group(1))
    return names


def is_written(code, field):
    """A write is an increment/decrement or an assignment to the field."""
    postfix = re.search(
        r"stats\s*(?:\.|->)\s*%s\s*(?:\+\+|--|\+=|-=|=|\|=|&=|\^=)" % re.escape(field),
        code)
    prefix = re.search(
        r"(?:\+\+|--)\s*[\w.\[\]>-]*stats\s*(?:\.|->)\s*%s\b" % re.escape(field),
        code)
    return bool(postfix or prefix)


def main():
    orphans = []
    checked = 0
    skipped = []
    for header in HEADERS:
        module = os.path.basename(header)[:-2]
        source = os.path.join(SOURCES, module + ".c")
        with open(header) as handle:
            fields = sorted(set(stats_fields(strip_comments(handle.read()))))
        if not fields:
            continue
        if not os.path.exists(source):
            skipped.append(module)
            continue
        with open(source) as handle:
            code = strip_comments(handle.read())
        checked += len(fields)
        for field in fields:
            if is_written(code, field):
                continue
            if (module, field) in CANARY:
                continue
            orphans.append((module, field, source))

    if orphans:
        print("unwritten statistics: %d of %d declared fields have no writer "
              "in the module that declares them" % (len(orphans), checked))
        for module, field, source in orphans:
            print("  %s: stats.%s (%s writes it nowhere)" % (module, field,
                                                             os.path.relpath(source, ROOT)))
        print("\nA statistic nothing writes is a promise the code does not "
              "keep. Either write it, or stop declaring it.")
        return 1

    print("stats write check: %d declared statistics across %d cores, every one "
          "written by the core that declares it%s"
          % (checked, len(HEADERS),
             " (%d canaries exempted)" % len(CANARY) if CANARY else ""))
    if skipped:
        print("  no source for: %s" % ", ".join(skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
