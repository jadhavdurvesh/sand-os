# DMJ Technology Integration Architecture

Status: architecture baseline. This document maps the independent DMJ technology repositories to the SAND OS layers where adapters belong. It does not claim that any external repository is already linked or runnable in SAND.

## Core rule

SAND OS remains the platform and security authority. DMJ projects remain independent products/libraries with their own repositories and release cycles. Integration is performed through versioned Veyra contracts and tested adapters. Do not copy whole projects into the kernel merely to call them "native".

Use an in-house component only when its real interface, dependencies, license, memory footprint and security properties fit the target. Prefer a small library or protocol adapter over starting an entire service for a simple local operation.

## Layer map

| SAND layer | Responsibility | Technologies that belong here |
|---|---|---|
| Host build and CI | Inspect binaries, validate generated artifacts, model-check design contracts | AETHER, ARGUS |
| Kernel / ring 0 | CPU, memory, interrupts, process isolation, device control, syscall enforcement | No general-purpose DMJ application framework. Only small audited low-level primitives if there is a compelling need |
| Veyra kernel ABI | Stable syscall, handle, capability, buffer and error contracts | SAND-owned contracts; OCTET schemas may describe message payloads but OCTET is not itself a syscall ABI |
| DeviceCore and drivers | Hardware enumeration, driver lifecycle, DMA/IRQ/resource ownership | EREBUS hooks only when packet filtering/network stack exists; hardware-specific drivers remain SAND modules |
| Native services / user space | Service APIs, IPC, events, storage, network and policy services | CONTRACT, OCTET, FROSTBYTE, PHANTOM, ORBIT, ANANTA, EREBUS |
| Network service layer | IP/UDP, sockets, secure transport, typed remote operations | DATAGRAM, CALLGRAPH, with OCTET serialization; never in the boot path or kernel core |
| Desktop / application layer | Desktop assistant, browser, device UI, user workflows | Citadel, NOCTIS, MAINTAIN AI, DMJScript, LEXORA, Relay, DMJ LLM |
| Verification and governance | Verify finite-state policies, ABI conformance and release artifacts | ARGUS, DUSRM, AETHER plus SAND's own tests |

## In-house technology placement

### AETHER — build-time executable inspection
- **Placement:** host-side toolchain and CI, not a boot-time kernel dependency.
- **Use:** inspect supported ELF64 build artifacts and report bounded structural metadata before packaging.
- **Boundary:** inspection is not signature verification, malware detection or proof that an application is safe. It does not replace the SAPP parser.

### ARGUS — formal policy verification
- **Placement:** developer workstation and CI.
- **Use:** model-check finite state machines for capability grants, consent/confirmation, package install/launch, trust-root rotation, service lifecycle and failure recovery.
- **Boundary:** ARGUS results support design assurance; runtime checks must still be enforced by SAND.

### OCTET — structured serialization
- **Placement:** user-space IPC and service protocol library, after Veyra IPC and bounded message buffers exist.
- **Use:** encode versioned structured messages for Citadel, CONTRACT and CALLGRAPH adapters.
- **Boundary:** serialization does not provide transport, authentication, authorization or encryption. Validate schema and message lengths at every receiver.

### FROSTBYTE — byte-stream framing
- **Placement:** user-space stream IPC/network adapter, only if SAND exposes a byte-stream transport.
- **Use:** frame messages over stream transports with bounded buffers.
- **Boundary:** do not use it as a substitute for OCTET's typed serialization or DATAGRAM's datagram envelope. It is not a kernel IPC implementation.

### CONTRACT — service/API framework
- **Placement:** isolated user-space service or service host, once process isolation, Veyra service handles and IPC are usable.
- **Use:** host typed local service endpoints and API boundaries for desktop components.
- **Boundary:** no ring-0 integration. HTTP listener exposure and dependency footprint require a separate threat and resource review.

### PHANTOM — asynchronous event broker
- **Placement:** optional user-space system service after IPC and service supervision.
- **Use:** route desktop/system events between independent services where asynchronous fan-out is needed.
- **Boundary:** not required for simple synchronous OS calls. At-least-once delivery requires idempotent consumers, bounded queues and authorization on subscriptions/publications.

### ORBIT — job orchestration
- **Placement:** optional user-space orchestration service.
- **Use:** coordinate longer-running multi-step jobs across services.
- **Boundary:** not a kernel scheduler and not needed for simple assistant commands. Preserve its documented single-controller and recovery limitations.

### DATAGRAM — UDP transport
- **Placement:** user-space networking transport or a clearly separated networking service after SAND has NIC drivers, Ethernet/IP/UDP and a socket/datagram Veyra contract.
- **Use:** optional UDP endpoints and datagram framing for connected services.
- **Boundary:** no use before the network stack exists; no hard-real-time or safety-critical actuation. Network is disabled by default for offline components.

### CALLGRAPH — typed remote operations
- **Placement:** user-space RPC service/client above OCTET and DATAGRAM (or another explicitly supported transport).
- **Use:** typed, allow-listed remote service operations with deadlines, authorization and audit.
- **Boundary:** never unrestricted shell execution, never ring-0, never a substitute for local capability checks. Remote requests are untrusted and require local authorization.

### LEXORA — declarative workflow language
- **Placement:** optional user-space workflow/compiler/runtime after Citadel's local capability API and sandbox exist.
- **Use:** a future declarative way to express multi-step assistant workflows.
- **Boundary:** not the current intent matcher, not a general scripting permission bypass, and not a production automation runtime until its VM, fuel, memory and capability limits are complete.

### Citadel — deterministic desktop assistant
- **Placement:** native user-space desktop service/app, integrated with SAND Horizon through Veyra service and desktop contracts.
- **Use:** offline rule-based command parsing, intent registry and policy-gated dispatch for operations such as opening installed apps, showing system status and changing allowed settings.
- **Boundary:** Citadel proposes a capability call; SAND's OS security manager decides whether it is allowed. Citadel must not receive raw kernel access or execute arbitrary shell text. AI provider ABI remains dormant until separately enabled.

## Citadel's intended native flow

```text
User input
   |
SAND Horizon assistant surface
   |
Citadel deterministic parser + intent registry
   |
typed capability request
   |
Veyra service / capability boundary
   |
SAND authorization and confirmation policy
   |
native service (app launcher, settings, files, system status)
```

A future AI provider may suggest an intent or request, but model output is untrusted and cannot grant permissions. The same Veyra authorization path applies to rule-based and AI-assisted requests.

## Signing and certification integration

The current SAND Apps feature branch `feature/real-app-modules` contains SAPP v2 host-side Ed25519 signing/verification tools and STCI publisher-certificate tooling. The default `main` branch still reports SAPP v1; do not assume the feature branch is merged or released.

SAND OS must implement the native enforcement contract in `sand-apps/docs/native-loader-stci.md`:
1. Read an immutable package image into kernel-owned memory.
2. Validate all SAPP v2 fields and ranges before cryptographic parsing.
3. Validate the embedded publisher certificate and code-signing usage.
4. Match issuer key ID against a root provisioned by trusted OS configuration.
5. Verify the issuer's Ed25519 certificate signature.
6. Verify the exact domain-separated SAPP v2 signed transcript.
7. Only then allocate/map executable segments, apply relocations and permit launch.
8. Record app identity, signer/root IDs, digest and result.

The main OS currently accepts SAPP v1 only and requires signature fields to be zero. Do not silently reinterpret v1. Production policy must reject unsigned v1 and invalid/unknown SAPP v2. Developer-mode exceptions must be explicit and auditable, never an implicit fallback.

**Critical dependency:** the current kernel has no cryptographic verification library, protected trust-store provisioning, trusted clock/revocation policy, immutable file object, general package manager, or complete process isolation. Do not add a superficial signature check to `process_create_sapp()`. First establish the package buffer and trust boundary, choose a reviewed Ed25519 verifier suitable for the 32-bit freestanding target, and create cross-language known-answer vectors from the SAND Apps host tools. Root private keys must never enter this repository or the OS image.

## Integration order — no parallel subsystem sprawl

1. **Finish current executable/process milestone:** resolve kernel image/loader constraints, make process creation and exit safe, and establish immutable image input semantics. No SAPP v2 launch claim yet.
2. **Complete Veyra kernel/service contracts:** validated buffers, handles, capability rights and a minimal IPC/service boundary.
3. **Native STCI enforcement:** trusted root provisioning, Ed25519 verification, SAPP v2 parsing and fail-closed launch gate. Add shared test vectors and fuzzing before accepting signed apps.
4. **DeviceCore and normalized I/O:** only the interfaces needed for services and desktop operations.
5. **User-space service foundation:** integrate OCTET for bounded message encoding; add CONTRACT only where service hosting is required.
6. **Citadel native adapter:** deterministic intents call only OS-authorized Veyra capabilities. Implement app launch only after the signed-package gate exists.
7. **Optional technologies by need:** FROSTBYTE/PHANTOM/ORBIT for service communication and workflows; DATAGRAM/CALLGRAPH after networking; LEXORA after a sandboxed workflow runtime.
8. **Verification throughout:** ARGUS policy models, AETHER build inspection, DUSRM ownership/conformance review, and SAND unit/integration/fuzz tests.

## Integration state vocabulary

- **Proposed:** mapped to a SAND layer; no adapter exists.
- **In development:** adapter or OS contract is being implemented.
- **Tested:** adapter passes repeatable tests against a stated SAND build.
- **Supported:** compatibility policy, failure behavior, security review and release tests exist.

No project is "integrated" merely because its name appears in a document or its repository is linked.
