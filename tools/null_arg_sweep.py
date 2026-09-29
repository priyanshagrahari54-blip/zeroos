#!/usr/bin/env python3
"""Generated NULL-argument sweep over every desktop entry point.

Every function declared in `userspace/desktop/include/zeroos/desktop/*.h`
is called exactly once, with 0 for every pointer argument and 0 for every
scalar, inside a child process.  A module that forgets to check the
pointer it was handed then announces itself as a crash instead of waiting
to be found by a user.

This is not a substitute for the suites -- it proves only the narrowest
thing: that no entry point reads through a pointer it was given as null.
It earns its place anyway, because it covers entry points the suites do
not reach and it covers new ones the moment they are declared.  Three
null dereferences that no test had touched came out of it:

  * `zd_wm_focus()` counted the refusal on the window manager before it
    had checked that the manager existed, so the one path whose job is to
    survive a bad request was the one that could not survive it;
  * `zd_notify_dismiss()` and `zd_notify_defer()` looked the notification
    up first and checked the centre afterwards, so an id that was merely
    plausible -- not the invalid one, which short-circuits -- was enough
    to read through a null centre on the way to reporting "not found".

The sweep drives each call twice, once with 0 for every scalar and once
with a small non-zero value, because a guard that only holds for id 0 is
not a guard: the notify bug above survived the all-zero pass.

The libc-equivalent primitives (`zd_memset`, `zd_memcpy`, `zd_memmove`,
`zd_memcmp`) are deliberately skipped.  Reading or writing through a
caller-supplied pointer is their contract, exactly as it is for libc, and
handing them a null pointer with a non-zero length is a caller bug the
kernel/ABI boundary does not need to absorb.
"""

import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Every header directory that declares an entry point, with the sources
# needed to link the calls.  The sweep is only as broad as this list.
MODULES = [
    ("desktop", ["userspace/desktop/include/zeroos/desktop/*.h"],
     ["userspace/desktop/src/*.c", "kernel/crypto.c"]),
    ("compat", ["userspace/compat/include/zeroos/compat/*.h"],
     ["userspace/compat/src/*.c"]),
    # The host-testable driver and protocol cores; the header list mirrors
    # HARDWARE_CORE_SUITES in the Makefile, which is what proves these
    # sources link without the rest of the kernel.
    ("hardware",
     ["kernel/audio_core.h", "kernel/crypto.h", "kernel/dhcp_core.h",
      "kernel/display_core.h", "kernel/dma.h", "kernel/dns_core.h",
      "kernel/driver_core.h", "kernel/input_core.h", "kernel/net_arp.h",
      "kernel/net_conntrack.h", "kernel/net_core.h", "kernel/net_ipv6.h",
      "kernel/net_l2.h", "kernel/net_route.h", "kernel/net_stack.h",
      "kernel/net_transport.h", "kernel/netif.h", "kernel/usb_core.h",
      "kernel/net_socket.h", "kernel/mouse_core.h", "kernel/scancode_core.h",
      "kernel/resource_core.h"],
     ["kernel/crypto.c", "kernel/dhcp_core.c", "kernel/display_core.c",
      "kernel/dma.c", "kernel/dns_core.c", "kernel/driver_core.c",
      "kernel/input_core.c", "kernel/mouse_core.c", "kernel/net_arp.c",
      "kernel/net_conntrack.c", "kernel/net_core.c", "kernel/net_ipv6.c",
      "kernel/net_l2.c", "kernel/net_route.c", "kernel/net_stack.c",
      "kernel/net_transport.c", "kernel/net_socket.c", "kernel/netif.c",
      "kernel/resource_core.c", "kernel/scancode_core.c",
      "kernel/usb_core.c", "kernel/audio_core.c"]),
]
INCLUDE_DIRS = ["userspace/include", "userspace/desktop/include",
                "userspace/compat/include", "kernel"]

# Primitives whose contract is a buffer the caller has to provide: there
# is no length to validate and no error to report, so handing them null is
# a caller bug exactly as it is for libc.  The crypto primitives take
# fixed-size keys and nonces and process them unconditionally for the same
# reason.
SKIP = {"zd_memset", "zd_memcpy", "zd_memmove", "zd_memcmp",
        "zeroos_poly1305_init", "zeroos_poly1305", "zeroos_poly1305_key_gen",
        "zeroos_chacha20_block", "zeroos_chacha20_xor"}


def split_params(text):
    """Split a parameter list on top-level commas only."""
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return [p for p in parts if p and p != "void"]


def declarations():
    out = []
    for module, patterns, _ in MODULES:
        out.extend(declarations_in(module, patterns))
    return out


def declarations_in(module, patterns):
    out = []
    paths = []
    for pattern in patterns:
        paths.extend(glob.glob(os.path.join(ROOT, pattern)))
    for path in sorted(set(paths)):
        name = os.path.basename(path)
        if name == "desktop.h":
            continue  # it only re-includes the others
        text = re.sub(r"/\*.*?\*/", "", open(path).read(), flags=re.S)
        for m in re.finditer(r"\b([a-z_]\w+)\s*\(([^;{}]*)\)\s*;", text):
            fn, params = m.group(1), m.group(2).strip()
            # A declaration begins its line; a call site inside one of the
            # header's own inline helpers would otherwise be swept as if
            # it were an entry point -- and its arguments are variables,
            # not types, so the sweep would hand an integer to a pointer.
            line_start = text.rfind("\n", 0, m.start()) + 1
            ret = text[line_start:m.start()]
            if not re.match(r"^[A-Za-z_][\w ]*\*?\s*$", ret):
                continue
            if "typedef" in ret or not ret.strip():
                continue          # a function-pointer typedef, not a function
            if "..." in params:
                continue          # variadic: cannot generate a safe call
            if not split_params(params):
                continue          # no arguments to null out
            if fn in SKIP:
                continue
            out.append((module, fn, params))
    return out


def argument_for(param, scalar, structs):
    if "*" in param or "[" in param:
        return "0"
    m = re.match(r"(?:const\s+|volatile\s+)*((?:struct|union|enum)\s+\w+)",
                 param)
    if m:
        key = re.sub(r"[^A-Za-z0-9_]", "_", m.group(1))
        structs[key] = m.group(1)
        return "zero_" + key
    return scalar


def generate(decls):
    calls = []
    structs = {}
    for header, fn, params in decls:
        args = [argument_for(p, "@SCALAR@", structs)
                for p in split_params(params)]
        calls.append((header, fn, ",".join(args)))
    return calls, structs


TEMPLATE = """#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
{includes}
{structs}
static void run_child(const char *what, void (*fn)(void), int *crashes) {{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {{
        fclose(stderr);
        fn();
        fflush(stdout);
        _exit(0);
    }} else {{
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {{
            printf("  CRASH: %s (wait status %d)\\n", what, status);
            ++*crashes;
        }}
    }}
}}

{calls}
int main(void) {{
    int crashes = 0;
{drivers}
    printf("null-arg sweep: %d entry points x %d scalar values, %d crash(es)\\n",
           {count}, {passes}, crashes);
    return crashes ? 1 : 0;
}}
"""


def relative_include(path):
    """The `<...>` spelling of a header, given the include search path."""
    for directory in INCLUDE_DIRS:
        full = os.path.join(ROOT, directory)
        if path.startswith(full + os.sep):
            return os.path.relpath(path, full).replace(os.sep, "/")
    return os.path.basename(path)


def sweep_module(name, patterns, src_patterns, cc):
    """Build and run the sweep for one module; returns the crash count."""
    decls = declarations_in(name, patterns)
    if not decls:
        print("null-arg sweep: no declarations for module %s" % name)
        return 1
    calls, structs = generate(decls)

    paths = []
    for pattern in patterns:
        paths.extend(glob.glob(os.path.join(ROOT, pattern)))
    includes = "\n".join("#include <%s>" % relative_include(p)
                         for p in sorted(set(paths)))
    struct_defs = "\n".join("static %s zero_%s;" % (t, k)
                             for k, t in sorted(structs.items()))

    srcs = []
    for pattern in src_patterns:
        if glob.has_magic(pattern):
            srcs.extend(sorted(glob.glob(os.path.join(ROOT, pattern))))
        else:
            srcs.append(os.path.join(ROOT, pattern))

    call_defs, drivers, scalars = [], [], ["0", "3"]
    idx = 0
    for module, fn, args in calls:
        for scalar in scalars:
            call_defs.append(
                "static void call_%d(void) { (void)%s(%s); }"
                % (idx, fn, args.replace("@SCALAR@", scalar)))
            drivers.append('    run_child("%s (%s, scalars=%s)", call_%d, '
                           "&crashes);" % (fn, module, scalar, idx))
            idx += 1

    source = TEMPLATE.format(includes=includes, structs=struct_defs,
                             calls="\n".join(call_defs),
                             drivers="\n".join(drivers),
                             count=len(calls), passes=len(scalars))
    with tempfile.TemporaryDirectory() as tmp:
        c_path = os.path.join(tmp, "null_arg_sweep.c")
        bin_path = os.path.join(tmp, "null_arg_sweep")
        with open(c_path, "w") as handle:
            handle.write(source)
        build = [cc, "-std=c11", "-O1", "-w"]
        build += ["-I" + os.path.join(ROOT, d) for d in INCLUDE_DIRS]
        build += [c_path] + srcs + ["-o", bin_path]
        proc = subprocess.run(build, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
        if proc.returncode != 0:
            sys.stdout.write(proc.stdout.decode("utf-8", "replace"))
            print("null-arg sweep: %s build failed" % name)
            return 1
        run = subprocess.run([bin_path], stdout=subprocess.PIPE)
        out = run.stdout.decode("utf-8", "replace")
        sys.stdout.write(out)
        if run.returncode != 0:
            return 1
    return 0


def main():
    cc = os.environ.get("CC", "cc")
    failed = []
    for name, patterns, src_patterns in MODULES:
        if sweep_module(name, patterns, src_patterns, cc) != 0:
            failed.append(name)
    if failed:
        print("null-arg sweep: FAIL (%s)" % ", ".join(failed))
        return 1
    print("null-arg sweep: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
