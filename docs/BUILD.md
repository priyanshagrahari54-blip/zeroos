# ZEROOS build and validation

The freestanding kernel needs GCC and binutils; ISO creation needs GRUB
Multiboot2 tooling, xorriso and mtools. Guest tests need `qemu-system-x86_64`.
Native tests run on an x86-64 host with the standard C toolchain and `timeout`.

```sh
make build/zeroos.elf            # warning-as-error kernel build only
make host-test                   # five native suites; 5-second cap each
python3 tests/boot_regressions.py --static
make                            # ELF + bootable ISO
make run                        # interactive unbounded development boot
```

Reproduce the fault-instrumented CI suite from a clean build:

```sh
make clean
make host-test
make EXTRA_CFLAGS=-DZEROOS_TEST_FAULTS
python3 tests/boot_regressions.py # nine guest fault/feature cases, 12 s each
python3 tests/boot_matrix.py      # five TCG guests + two KVM cases when available; 45 s each
```

The matrix includes 32/128/512/768 MiB configurations, qemu64/max CPUs, explicit
PCID-off coverage and a repeated baseline. It records the actual detected PCID
mode rather than assuming `-cpu max` supplies the feature. Where `/dev/kvm`
is available, it also runs `host` and `host,invpcid=off` to cover PCID with and
without INVPCID. Missing KVM is reported, not counted as a hardware pass. CI sets
`ZEROOS_REQUIRE_PCID=1`: both positive PCID modes are mandatory for a green
workflow. A local baseline-only run without that variable is not a complete
certification run. CI additionally
keeps the default guest alive for a 60-second regression window, requires all
integration markers and rejects panic/failure/triple-fault diagnostics.

Use a clean build when changing compiler flags: Make does not encode flags in
object dependencies. Alternative builds can use `BUILD=build/<name>` to avoid
reusing objects. Generated images, binaries and diagnostic logs belong under
ignored `build/`, not in Git. CI uploads diagnostics on both success and failure
and posts a bounded validation report to the tracking issue.

Passing compilation is not guest validation. See VALIDATION.md for tested
commits, exact runs, scope and certification status.
