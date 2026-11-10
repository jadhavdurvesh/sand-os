# Native technology integration plan

SAND OS is the platform foundation. Independent projects should integrate through stable, documented interfaces rather than being copied wholesale into the kernel. Each project keeps its own repository and release lifecycle; SAND owns the platform adapter and ABI.

## Integration candidates

| Project | Intended SAND role | Readiness | Dependency |
|---|---|---|---|
| [EREBUS](https://github.com/jadhavdurvesh/erebus) | Firewall policy engine, connection policy, rate limits, DNS security and audit events | Portable Rust core exists; native enforcement is not implemented | SAND network drivers, IP stack and packet/flow hooks |
| [NOCTIS](https://github.com/jadhavdurvesh/noctis) | Potential native browser application | Separate browser project; no OS integration contract yet | Process execution, graphics, windowing, networking, TLS and storage |
| [DUSRM](https://github.com/jadhavdurvesh/DUSRM) | Potential system recovery / diagnostics utility | Separate systems project; integration contract needed | Storage, permissions, process and device APIs |
| [CALLGRAPH](https://github.com/jadhavdurvesh/callgraph) | Developer tooling, profiling and diagnostics | Host-side tooling first; not a kernel dependency | SAND SDK, executable metadata and debug-symbol format |
| [Relay](https://github.com/jadhavdurvesh/Relay) | Optional model/provider routing service | Application/service layer, not a kernel component | Networking, user processes, TLS and application runtime |
| [MAINTAIN AI](https://github.com/jadhavdurvesh/Maintain.ai) | Optional device-management application | Application/server product, not a kernel component | Network stack, device API and userland |
| [ANANTA](https://github.com/jadhavdurvesh/ANANTA) | Potential data/storage service | Separate database-engine project; no direct kernel integration | Filesystem, memory management, process isolation and storage APIs |
| DMJ language/toolchain work | Native compiler, runtime and SDK targets | Independent language/toolchain work | Executable ABI, linker format, syscall ABI and SDK |

These are architecture directions, not claims that the projects currently run on SAND OS. Recheck each repository's current build system, license and dependencies before selecting code for integration.

## EREBUS firewall integration

EREBUS currently describes a portable Rust policy engine, validated versioned TOML policy, transactional in-memory policy reload, bounded connection tracking, event journal, token-bucket rate limiting, DNS parsing/policy/cache and a UDP/TCP DNS proxy. Its own status says native packet interception and OS resolver integration are not implemented.

Its adapter API accepts a normalized traffic descriptor and policy decision, then requires a platform adapter to apply the verdict and report failures. That is the right future boundary for SAND.

1. Build SAND Ethernet, ARP, IPv4/IPv6, UDP and TCP foundations and a network-interface driver API.
2. Define a SAND-owned packet/flow hook with explicit ownership, lifetime, direction and error semantics.
3. Define a stable boundary between the SAND kernel and an isolated privileged EREBUS service.
4. Port or compile the portable EREBUS core for SAND userland only after a Rust/runtime strategy exists. The current 32-bit freestanding C kernel cannot simply link a normal Rust std crate.
5. Begin in observe-only mode with deterministic test packets and mock enforcement.
6. Enable enforcement only after deny/allow, failure-path, boot-recovery and isolation tests pass.

The kernel retains final authority over packet disposition. A missing or crashed policy service must follow a documented fail-safe mode; never silently turn an explicit deny into allow. Do not expose a fake firewall toggle before the packet hook is live.

## Integration rules

- Keep external projects in their own repositories; do not copy source into SAND without architecture and license review.
- Prefer a small C ABI or versioned message protocol at the kernel/user boundary over linking application code into the kernel.
- Kernel code remains freestanding and avoids allocation, panic, blocking I/O or unbounded work in interrupt context.
- Treat configuration, packets and IPC messages as untrusted input.
- Version interfaces and add compatibility tests before integration.
- SAND's own implementations remain authoritative for boot, memory, scheduling, drivers, filesystem, networking and graphics.

## Sequencing

M7: executable loading and process lifecycle. M8: device model, keyboard, display and storage expansion. M9: network interface, Ethernet and IP stack; this unlocks the real EREBUS adapter. M10: graphics/windowing and native desktop. Later userland milestones include EREBUS service, NOCTIS browser, developer SDK and other DMJ applications.
