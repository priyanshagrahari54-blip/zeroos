# ZEROOS — REMAINING GAP CLOSURE INVENTORY
Status: Binding supplemental checklist
Rule: This file covers cross-cutting details that are easy to omit from subsystem checklists. It is not evidence of implementation.

## A. TOOLCHAIN / REPRODUCIBILITY
- [ ] Pin exact compiler, assembler, linker, objcopy, image-builder versions.
- [ ] Record compiler target triple and ABI.
- [ ] Verify reproducible archives/images across clean machines.
- [ ] Verify deterministic link ordering.
- [ ] Verify deterministic filesystem/image creation.
- [ ] Record source revision inside build artifacts.
- [ ] Generate SBOM for release artifacts.
- [ ] Record licenses for source and third-party components.
- [ ] Verify third-party license compatibility.
- [ ] Verify dependency provenance and hashes.
- [ ] Prevent accidental host-library linkage.
- [ ] Test build with network disabled after dependency acquisition.
- [ ] Keep generated files auditable.
- [ ] Verify release symbols correspond exactly to release binary.

## B. BOOT CHAIN / FIRMWARE EDGE CASES
- [ ] Test BIOS/legacy boot path if supported.
- [ ] Test UEFI path if supported.
- [ ] Define Secure Boot behavior.
- [ ] Define unsigned-image failure behavior.
- [ ] Test unusual memory maps.
- [ ] Test low-memory configurations.
- [ ] Test >4 GiB memory.
- [ ] Test large physical-address configurations.
- [ ] Test missing framebuffer.
- [ ] Test framebuffer formats other than the primary target.
- [ ] Test missing ACPI tables and malformed ACPI.
- [ ] Test APIC/x2APIC capability differences.
- [ ] Test systems with one CPU.
- [ ] Test systems where AP startup partially fails.
- [ ] Define watchdog behavior.
- [ ] Define boot timeout/recovery behavior.
- [ ] Preserve panic information across reboot when safe.
- [ ] Verify kernel image cannot overlap boot modules or firmware regions.

## C. CPU / ISA / RAS
- [ ] Define minimum x86-64 ISA baseline.
- [ ] Detect optional ISA extensions before use.
- [ ] Guard AVX/AVX2/other optional instructions.
- [ ] Define XSAVE/XCR0 policy.
- [ ] Test FPU state across threads.
- [ ] Test SIMD state across preemption.
- [ ] Define machine-check/error handling policy.
- [ ] Define corrected-error reporting.
- [ ] Define unrecoverable CPU error behavior.
- [ ] Detect unsupported CPU topology assumptions.
- [ ] Handle invariant-TSC absence if timekeeping depends on it.
- [ ] Validate APIC timer behavior across CPU power states.

## D. KERNEL LOCKING / MEMORY ORDERING
- [ ] Inventory every lock.
- [ ] Document lock rank/order.
- [ ] Detect lock-order inversion in debug builds.
- [ ] Define IRQ-safe locks.
- [ ] Define NMI-safe restrictions.
- [ ] Define sleepable vs atomic contexts.
- [ ] Verify no sleeping while holding forbidden spinlocks.
- [ ] Document memory-ordering requirements for atomics.
- [ ] Use acquire/release/seq-cst only where justified.
- [ ] Test weak-memory-order races on real SMP hardware where practical.
- [ ] Add lock contention metrics.
- [ ] Add deadlock detection/watchdog in diagnostic builds.

## E. RCU / RECLAMATION / DEFERRED WORK
- [ ] If RCU-like mechanisms exist, define grace-period semantics.
- [ ] Ensure readers cannot observe freed objects.
- [ ] Define deferred-free queues.
- [ ] Bound deferred work.
- [ ] Define workqueue concurrency.
- [ ] Define work cancellation semantics.
- [ ] Ensure worker teardown drains/cancels owned work.
- [ ] Test shutdown with pending callbacks.

## F. SYSCALL / ABI COMPATIBILITY
- [ ] Version public ABI where needed.
- [ ] Define structure padding/alignment.
- [ ] Define 32-bit integer width explicitly.
- [ ] Define time structure widths.
- [ ] Define endianness assumptions.
- [ ] Define reserved fields and forward compatibility.
- [ ] Reject unknown mandatory flags.
- [ ] Ignore unknown optional fields only where specified.
- [ ] Prevent kernel ABI leaks of uninitialized bytes.
- [ ] Zero padding before copying structures to user space.
- [ ] Verify syscall restart semantics consistently.

## G. PROCESS RESOURCE ACCOUNTING
- [ ] Define per-process CPU accounting.
- [ ] Define memory RSS/virtual-size accounting.
- [ ] Define file-descriptor accounting.
- [ ] Define IPC accounting.
- [ ] Define child-process resource accounting.
- [ ] Define quotas/limits where needed.
- [ ] Define runaway-process containment.
- [ ] Define per-user resource limits if multi-user mode is supported.
- [ ] Ensure accounting cannot overflow.

## H. CRASH DUMPS / PANIC FORENSICS
- [ ] Define panic reason codes.
- [ ] Include CPU/task/process identifiers.
- [ ] Include register state.
- [ ] Include stack trace where valid.
- [ ] Include scheduler state.
- [ ] Include memory allocator state.
- [ ] Include recent fault context.
- [ ] Validate crash data before parsing.
- [ ] Rate-limit repeated crash persistence.
- [ ] Define sensitive-data redaction.
- [ ] Define crash-dump retention.
- [ ] Test panic while locks/interrupts are held.
- [ ] Test panic after allocator corruption.
- [ ] Test panic from multiple CPUs.

## I. TIME / CALENDAR / LOCALE
- [ ] Separate monotonic time from wall clock.
- [ ] Define RTC initialization.
- [ ] Define timezone storage.
- [ ] Define daylight-saving rules if user-facing calendar support exists.
- [ ] Define NTP/time synchronization policy.
- [ ] Prevent clock adjustment from breaking timers.
- [ ] Define timestamp precision.
- [ ] Define locale fallback.
- [ ] Define Unicode normalization expectations.
- [ ] Test malformed UTF-8.
- [ ] Test RTL text.
- [ ] Test mixed-script text.
- [ ] Test font fallback.
- [ ] Test keyboard layouts.

## J. USER / SESSION / AUTHENTICATION
- [ ] Define user identity model.
- [ ] Define login/session lifecycle.
- [ ] Define session lock/unlock.
- [ ] Define credential storage boundary.
- [ ] Define password/key derivation if passwords exist.
- [ ] Rate-limit authentication attempts.
- [ ] Define recovery path for lost credentials.
- [ ] Define logout semantics.
- [ ] Revoke session capabilities on logout.
- [ ] Ensure background services cannot retain unauthorized user capabilities.
- [ ] Test fast-user-switching if supported.

## K. IPC SECURITY
- [ ] Authenticate IPC peer identity.
- [ ] Validate capability/handle ownership.
- [ ] Prevent confused-deputy behavior.
- [ ] Prevent handle reuse attacks.
- [ ] Bound message size.
- [ ] Bound queue depth.
- [ ] Validate shared-memory offsets/lengths.
- [ ] Define cross-user IPC policy.
- [ ] Define privileged-service exposure.
- [ ] Fuzz IPC parsers and state machines.

## L. FILESYSTEM SECURITY
- [ ] Define ownership and permission bits.
- [ ] Define ACL behavior if supported.
- [ ] Define symlink-following rules for privileged operations.
- [ ] Prevent symlink/path races in security-sensitive operations.
- [ ] Define special-file policy.
- [ ] Define device-node creation authority.
- [ ] Define mount-option security.
- [ ] Prevent untrusted filesystem metadata from causing kernel memory corruption.
- [ ] Fuzz path parsing and directory records.
- [ ] Test malicious filesystem images.

## M. STORAGE RELIABILITY
- [ ] Detect media errors.
- [ ] Distinguish transient from permanent I/O failures.
- [ ] Retry only operations safe to retry.
- [ ] Preserve write ordering across failures.
- [ ] Define flush timeout.
- [ ] Detect device disappearance.
- [ ] Define degraded behavior.
- [ ] Test full-disk behavior.
- [ ] Test metadata exhaustion separately from data exhaustion.
- [ ] Test extremely fragmented files.
- [ ] Test millions of directory entries where practical.
- [ ] Test large files.
- [ ] Test sparse files if supported.

## N. NETWORK SECURITY
- [ ] Define random-source requirements for network security.
- [ ] Define ephemeral-port allocation.
- [ ] Prevent port allocation races.
- [ ] Bound connection tables.
- [ ] Defend against SYN/resource exhaustion where relevant.
- [ ] Validate ICMP/ICMPv6 inputs.
- [ ] Define MTU/PMTU behavior.
- [ ] Define checksum-offload assumptions.
- [ ] Define hardware offload fallback.
- [ ] Define MAC/address-change handling.
- [ ] Define network namespace policy if supported.
- [ ] Ensure firewall rules cannot be bypassed by alternate paths.

## O. RANDOMNESS / ENTROPY
- [ ] Define early-boot entropy source.
- [ ] Do not expose cryptographic randomness before readiness.
- [ ] Mix multiple entropy sources where available.
- [ ] Define entropy failure behavior.
- [ ] Separate fast PRNG from cryptographic RNG.
- [ ] Test repeated boot randomness.
- [ ] Test VM entropy-starvation behavior.
- [ ] Protect RNG state from ordinary logs.

## P. GRAPHICS SECURITY / ROBUSTNESS
- [ ] Validate GPU command/resource handles.
- [ ] Validate buffer sizes.
- [ ] Validate shader/resource references.
- [ ] Prevent untrusted applications from mapping privileged GPU memory.
- [ ] Bound GPU queues.
- [ ] Handle hung GPU.
- [ ] Define GPU reset ownership.
- [ ] Recover compositor after GPU reset.
- [ ] Ensure one bad graphics client cannot corrupt another client's surface.
- [ ] Fuzz image/font/shader parsers exposed to untrusted content.

## Q. DISPLAY / INPUT UX DETAILS
- [ ] Define cursor ownership and hardware/software cursor fallback.
- [ ] Define keyboard layout switching.
- [ ] Define IME/input-method boundary if multilingual text entry is supported.
- [ ] Define touch support if hardware provides it.
- [ ] Define pointer acceleration policy.
- [ ] Define key-repeat timing.
- [ ] Define clipboard format limits.
- [ ] Define clipboard lifetime across application exit.
- [ ] Define selection ownership.
- [ ] Define focus restoration after app crash.
- [ ] Define modal-dialog accessibility.
- [ ] Define reduced-motion behavior.
- [ ] Define high-contrast behavior.
- [ ] Define scaling behavior at 1366x768 and external FHD.

## R. DESKTOP RELIABILITY
- [ ] Shell restart without reboot.
- [ ] Window manager restart/recovery.
- [ ] Compositor restart/recovery where architecture permits.
- [ ] Preserve unsaved user state only when explicitly supported.
- [ ] Prevent notification storms.
- [ ] Prevent runaway search indexing.
- [ ] Prevent runaway thumbnail generation.
- [ ] Bound thumbnail cache.
- [ ] Bound icon cache.
- [ ] Bound font/glyph cache.
- [ ] Handle inaccessible files gracefully.
- [ ] Handle removable-media disappearance gracefully.

## S. PACKAGE / APPLICATION SANDBOX
- [ ] Define package identity.
- [ ] Define version comparison.
- [ ] Define dependency constraints.
- [ ] Define optional dependencies.
- [ ] Define conflicts/replacements.
- [ ] Define per-app writable data directory.
- [ ] Define executable/resource separation.
- [ ] Define application permissions.
- [ ] Define uninstall cleanup.
- [ ] Define orphaned data policy.
- [ ] Define package cache garbage collection.
- [ ] Prevent package scripts from gaining unintended privilege.
- [ ] Define install-time resource limits.

## T. BROWSER / UNTRUSTED CONTENT
- [ ] Define origin identity.
- [ ] Define same-origin isolation.
- [ ] Define storage quotas.
- [ ] Define permission persistence.
- [ ] Define download quarantine.
- [ ] Define executable-download handling.
- [ ] Define popup/background limits.
- [ ] Define renderer process memory limits.
- [ ] Define crash-loop handling.
- [ ] Define browser update rollback.
- [ ] Test malformed HTML/CSS/JS/media/fonts/PDFs.
- [ ] Test renderer escape assumptions with sandbox tests.

## U. WINDOWS / ANDROID RUNTIME OPERATIONAL DETAILS
- [ ] Define runtime startup cost.
- [ ] Keep runtime dormant until workload starts.
- [ ] Define runtime process isolation.
- [ ] Define foreign-code memory permissions.
- [ ] Define syscall/API translation boundary.
- [ ] Define unsupported-call diagnostics.
- [ ] Define runtime shutdown cleanup.
- [ ] Define per-app resource limits.
- [ ] Define graphics translation cache.
- [ ] Define filesystem mapping security.
- [ ] Define network mapping security.
- [ ] Define runtime update compatibility.
- [ ] Build versioned compatibility matrices rather than generic claims.

## V. GAMING / PERFORMANCE ENGINEERING
- [ ] Define performance mode vs balanced/power-save policy.
- [ ] Define CPU frequency/governor interaction.
- [ ] Define GPU power/performance policy.
- [ ] Define background suppression scope.
- [ ] Define shader compilation scheduling.
- [ ] Define asset-cache eviction.
- [ ] Define frame-time percentile reporting (not only average FPS).
- [ ] Record 1%, 0.1% lows where meaningful.
- [ ] Record CPU/GPU utilization separately.
- [ ] Record thermal clock behavior.
- [ ] Record memory pressure and paging.
- [ ] Record load times separately from runtime FPS.
- [ ] Repeat tests after cold boot and warm cache.
- [ ] Keep benchmark configurations reproducible.

## W. ZERO AI SAFETY / AGENT ENGINEERING
- [ ] Define immutable system capabilities AI cannot grant itself.
- [ ] Separate planning from privileged execution.
- [ ] Require capability check at execution time, not only planning time.
- [ ] Re-check authorization after asynchronous waits.
- [ ] Define confirmation policy for destructive actions.
- [ ] Define transaction/rollback for multi-step system changes.
- [ ] Define action provenance.
- [ ] Define user-visible action history.
- [ ] Define secret redaction from model context.
- [ ] Define prompt-injection defenses for files/web/content.
- [ ] Treat external content as untrusted instructions.
- [ ] Define tool-call allowlists.
- [ ] Define network-domain restrictions where appropriate.
- [ ] Define maximum autonomous chain length.
- [ ] Define timeout/deadline.
- [ ] Define emergency stop.
- [ ] Test confused-deputy scenarios.
- [ ] Test malicious document asking AI to execute commands.
- [ ] Test malicious webpage asking AI to expose files.
- [ ] Test privilege-escalation prompts.
- [ ] Test rollback after partial action failure.

## X. PRIVACY
- [ ] Define data collection policy.
- [ ] Default to minimum telemetry.
- [ ] Make diagnostic export explicit.
- [ ] Redact personal paths/secrets where possible.
- [ ] Define browser history/cookie storage policy.
- [ ] Define AI conversation retention policy.
- [ ] Define AI context boundary.
- [ ] Prevent one user's private data from entering another user's context.
- [ ] Define deletion semantics.
- [ ] Test deleted data is not accidentally retained in indexes/caches/logs.

## Y. BACKUP / DATA PORTABILITY
- [ ] Define user-data backup format.
- [ ] Define configuration export.
- [ ] Define restore semantics.
- [ ] Define backup integrity verification.
- [ ] Define incremental backup behavior if supported.
- [ ] Define encryption/key handling.
- [ ] Test restore onto clean installation.
- [ ] Test restore after filesystem corruption.
- [ ] Never imply snapshot is a backup unless independently stored.

## Z. OBSERVABILITY / OPERATIONS
- [ ] Define health states for every long-lived service.
- [ ] Define startup dependency graph.
- [ ] Define restart policy.
- [ ] Define crash-loop backoff.
- [ ] Define watchdog policy.
- [ ] Define liveness/readiness diagnostics.
- [ ] Define resource leak detectors for debug builds.
- [ ] Define latency histograms where useful.
- [ ] Define trace sampling.
- [ ] Define persistent diagnostics size limit.
- [ ] Define support bundle format.
- [ ] Define reproducible bug-report capture.

## AA. TEST INFRASTRUCTURE
- [ ] Every test has a unique identifier.
- [ ] Every test declares prerequisites.
- [ ] Every test declares expected output.
- [ ] Every test declares timeout.
- [ ] Every test emits machine-readable result.
- [ ] Tests distinguish infrastructure failure from product failure.
- [ ] QEMU tests record command line.
- [ ] Hardware tests record machine identity/configuration.
- [ ] Fuzzers retain minimized crashing inputs.
- [ ] Regression corpus is versioned.
- [ ] Randomized tests record seeds.
- [ ] Flaky tests are quarantined with an owner and issue, never silently ignored.
- [ ] CI artifacts have retention policy.
- [ ] Certification tests cannot be bypassed by a documentation-only flag.

## AB. FORMAL INVARIANTS / PROOF-ORIENTED CHECKS
- [ ] Enumerate scheduler invariants.
- [ ] Enumerate allocator invariants.
- [ ] Enumerate page-table invariants.
- [ ] Enumerate reference-count invariants.
- [ ] Enumerate queue invariants.
- [ ] Enumerate filesystem transaction invariants.
- [ ] Enumerate IPC invariants.
- [ ] Assert cheap invariants in debug builds.
- [ ] Turn critical invariants into executable tests.
- [ ] Add model/state-machine tests for complex protocols.
- [ ] Document which properties are tested versus formally proven.

## AC. DOCUMENTATION CONSISTENCY
- [ ] No document claims a feature stronger than its evidence.
- [ ] Architecture and code agree.
- [ ] Hardware document lists tested devices.
- [ ] Roadmap reflects actual stage.
- [ ] Requirements remain separate from certification evidence.
- [ ] Performance numbers have workload/hardware/configuration.
- [ ] Compatibility claims have versioned matrices.
- [ ] Security limitations are explicit.
- [ ] Unsupported features are listed.
- [ ] Every certification item links to evidence.
- [ ] Retired designs are marked historical rather than silently rewritten.

## AD. RELEASE / SUPPLY-CHAIN SECURITY
- [ ] Release artifacts are signed.
- [ ] Signing keys are not stored in source control.
- [ ] CI signing is isolated.
- [ ] Release provenance is recorded.
- [ ] Artifact hashes are published.
- [ ] Build inputs are traceable.
- [ ] Compromised dependency response exists.
- [ ] Emergency update/revocation path exists.
- [ ] Rollback protection does not prevent emergency recovery.
- [ ] Recovery images are independently verified.

## AE. INSTALLER / FIRST BOOT
- [ ] Define disk partitioning policy.
- [ ] Define destructive-operation confirmation.
- [ ] Validate target disk identity.
- [ ] Define bootloader installation transaction.
- [ ] Define filesystem creation transaction.
- [ ] Define interrupted-install recovery.
- [ ] Define first-boot initialization.
- [ ] Ensure initialization is resumable/idempotent.
- [ ] Define default user creation.
- [ ] Define default security settings.
- [ ] Define first-boot resource usage.
- [ ] Do not index entire disk synchronously during first boot.

## AF. SHUTDOWN / REBOOT
- [ ] Define service shutdown ordering.
- [ ] Stop new work before draining.
- [ ] Drain/cancel timers and workers.
- [ ] Flush persistent state according to policy.
- [ ] Flush storage barriers where required.
- [ ] Close network services cleanly.
- [ ] Stop graphics/compositor safely.
- [ ] Stop AI actions and revoke capabilities.
- [ ] Ensure no user process can indefinitely block shutdown without policy.
- [ ] Define forced-shutdown escalation.
- [ ] Test reboot during active I/O.
- [ ] Test reboot during active IPC.
- [ ] Test reboot during active AI action.
- [ ] Test reboot during GPU activity.

## AG. FINAL GAP POLICY
- [ ] Before Stage 10, perform a repository-wide search for TODO/FIXME/stub/panic placeholders/unreachable placeholders and classify every result.
- [ ] Search for empty success paths and ignored return values in privileged code.
- [ ] Search for unchecked user-controlled lengths.
- [ ] Search for unchecked allocation results.
- [ ] Search for missing cleanup on every error branch.
- [ ] Search for lock acquisition without matching release.
- [ ] Search for reference increment without decrement.
- [ ] Search for timer registration without cancellation.
- [ ] Search for DMA mapping without unmapping.
- [ ] Search for device bind without teardown.
- [ ] Search for queue insertion without ownership transfer.
- [ ] Search for background loops without sleep/event wait.
- [ ] Search for busy-polling paths and justify every one.
- [ ] Search for debug-only behavior accidentally required by release.
- [ ] Search for feature flags that bypass security or validation.
- [ ] Search for claims in docs unsupported by tests.
- [ ] Re-run the entire evidence matrix after final integration.

## MASTER COMPLETION RULE
Nothing in this inventory becomes complete because it is written down. For each item: inspect code -> identify owner -> design contract -> implement -> negative test -> stress/fault test -> measure -> document evidence -> integrate -> re-run affected certification gates.


---

## Cross-Cutting Master Contract

See [`ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md`](./ZEROOS_MASTER_CONTRACT_AND_FEATURE_EXPANSION.md) for the mandatory cross-cutting engineering contract and the expanded ZEROOS feature/platform catalog. Applicable requirements cover ownership/lifetime/concurrency, boot/firmware, hardware certification, networking, packages, SDK, observability, accounts, backup/recovery, privacy, supply-chain security, accessibility/i18n, virtualization, power-loss certification, performance/compatibility labs, ZERO AI safety, and additional product features. This is a specification link only; implementation status remains evidence-based.
