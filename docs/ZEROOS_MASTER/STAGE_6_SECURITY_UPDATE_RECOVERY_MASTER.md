# ZEROOS STAGE 6 — SECURITY, PACKAGES, UPDATES & RECOVERY

Status: NEXT EXECUTION STAGE — specification and implementation contract. This document does not mark any item implemented.

## Mission
Turn security policy and recovery design into real enforcement at the OS boundaries. Stage 6 must be production architecture from the first implementation: no toy sandbox, fake permission dialog, simulated firewall, or throwaway updater.

## 6.1 Security model
- Define principal, credential, capability, permission, security-context and object-ownership models.
- Establish kernel/user boundary and least privilege.
- Every privileged operation must have a real enforcement point.
- Capability handles must be unforgeable and lifetime-bound.
- Credentials must not be trusted merely because a caller claims them.
- Permission checks must occur at the resource boundary, not only in UI.

## 6.2 Memory and execution hardening
- NX where supported.
- W^X for executable mappings.
- ASLR for eligible user processes.
- stack canaries/guards.
- CFI/hardening where compatible with the native toolchain.
- syscall validation and argument sanitization.
- user-pointer validation and TOCTOU-aware design.
- fault isolation so malformed user input cannot panic the kernel.

## 6.3 Sandbox
Create a real policy-enforcement architecture covering filesystem, IPC, devices, network, process creation, memory-sensitive operations and privileged services. Default deny where a capability is absent. Sandboxed processes must receive explicit denial errors and diagnostics.

## 6.4 Firewall
Bind firewall decisions to the actual packet path. Define stateful rules, interface/profile scope, inbound/outbound policy, service identity, logging and fail-safe behavior. A UI rule that does not affect packets is not considered implemented.

## 6.5 Package security
Define package identity, metadata, dependency graph, signature, trusted roots, revocation, version policy, quarantine and uninstall cleanup. Verify before activation. Never execute unverified package payloads as trusted system code.

## 6.6 Transactional updates
Required state machine:
DISCOVER -> DOWNLOAD -> VERIFY -> STAGE -> PREFLIGHT -> SNAPSHOT -> ACTIVATE -> HEALTH-CHECK -> COMMIT
Failure path:
FAIL -> ISOLATE -> ROLLBACK -> VERIFY-RECOVERY -> REPORT
Updates must be resumable, cancellable and power-loss safe.
Snapshot replacement must preserve the active rollback point until the new
snapshot is captured and verified. Rollback must restore the exact snapshot
bound to that update, not whichever user snapshot happens to be newest.

## 6.7 Recovery
Recovery environment must operate without normal desktop services and provide:
- boot diagnosis
- filesystem check/repair
- snapshot selection
- update rollback
- driver disable
- package quarantine
- network recovery
- recovery terminal
- logs/support bundle
- reset/reinstall boundaries

## 6.8 Privacy/secrets
Secret storage must have explicit key lifecycle, access control, zeroization where applicable, and no plaintext secrets in logs. Camera/microphone/location/device access must be permissioned and auditable.

## 6.9 Security testing
Required classes: unit, negative, privilege-boundary, fuzz, malformed input, race, fault injection, power loss, rollback, replay/wrong-key, package tampering, sandbox escape, network policy and long-duration soak.

## 6.10 Resource contract
Security must not create permanent polling. Verification, scanning, telemetry and policy evaluation are event-driven, batched and cancellable. Dormant protection services must sleep until events require work.

## Exit gate
Stage 6 is complete only when real enforcement, signed transactional updates, deterministic rollback, recovery outside the normal desktop, and security evidence are all present. Documentation alone cannot pass the gate.
