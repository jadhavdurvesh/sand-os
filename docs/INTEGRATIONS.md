# SAND OS and the DMJ technology ecosystem

SAND OS is an independent operating system platform. The projects below are independent technologies with their own repositories, responsibilities and release lifecycles. Integration means providing the platform interfaces they need; it does not mean merging their source into the kernel.

This document separates confirmed project purpose from proposed SAND integration. A proposed integration is not a claim that a project already runs on SAND.

## Independent technologies and their correct roles

| Project | Correct purpose | Potential relationship to SAND OS |
|---|---|---|
| [NOCTIS](https://github.com/jadhavdurvesh/noctis) | Independently developed web browser and browser engine, not a Chromium/Gecko/WebKit fork | Native browser application once process execution, graphics, networking, TLS and storage are available |
| [ANANTA](https://github.com/jadhavdurvesh/ANANTA) | Independent database/data engine | Database service/library for native applications, system state, structured data and application storage; it is not the SAND filesystem |
| [MAINTAIN AI](https://github.com/jadhavdurvesh/Maintain.ai) | Device-management and industrial/IoT platform: telemetry, hardware integration, monitoring, maintenance and device operations | Native device-management application/service after device APIs, networking and userland exist |
| [DUSRM](https://github.com/jadhavdurvesh/DUSRM) | Technology-independent architectural framework for describing and validating system responsibilities; not an executable runtime, recovery tool or firewall | Could guide SAND subsystem contracts, responsibility boundaries, architecture validation and conformance documentation; it is not a kernel module |
| [EREBUS](https://github.com/jadhavdurvesh/erebus) | Independent network defence/firewall policy engine | Future network security service through a SAND-native packet/flow enforcement adapter |
| [CONTRACT](https://github.com/jadhavdurvesh/CONTRACT) | Rust API/application framework | Application/service API framework when SAND has a usable userland runtime and network APIs |
| [OCTET](https://github.com/jadhavdurvesh/OCTET) | Zig binary data/serialization protocol | Structured binary encoding for IPC, storage formats, service messages and device communication |
| [DATAGRAM](https://github.com/jadhavdurvesh/DATAGRAM) | Rust UDP networking/transport engine | Network transport library/service once SAND's network stack exposes socket or datagram interfaces |
| [CALLGRAPH](https://github.com/jadhavdurvesh/callgraph) | Go remote procedure execution / RPC runtime | Inter-process and service-to-service RPC once SAND has IPC, process isolation and a compatible Go runtime or portable protocol implementation |
| [Relay](https://github.com/jadhavdurvesh/Relay) | AI model routing/orchestration across providers | Optional userland service/application; requires networking, TLS, process runtime and provider connectivity |
| [CONTINUUM](https://github.com/jadhavdurvesh/CONTINUUM) | Collaborative multi-agent AI engineering platform with shared workspace and cloud-agent architecture | Optional application/service; not a kernel subsystem |
| [DMJ LLM](https://github.com/jadhavdurvesh/DMJ-LLM) | Custom language-model project and inference/training ecosystem | Potential native inference application if SAND eventually supports the required memory, compute, drivers and model runtime |
| [MI-Recover](https://github.com/jadhavdurvesh/MI-Recover) | MIUI/HyperOS private-file recovery toolkit | Potential recovery/forensics utility only where SAND has compatible storage access and safe device interfaces; it is not a kernel recovery architecture |
| [MAINTAIN AI DeviceOS](https://github.com/jadhavdurvesh/Maintain.ai-DeviceOS) and [IoT Gateway](https://github.com/jadhavdurvesh/MAINTAIN-AI-IoT-Gateway) | Device-side operating environment and IoT telemetry gateway for MAINTAIN AI | Hardware/device ecosystem integration; evaluate separately from the SAND desktop OS and do not assume direct kernel reuse |
| [FireBot](https://github.com/jadhavdurvesh/firebot-core) ecosystem | Autonomous firefighting and human-rescue robotics platform, with separate firmware, hardware, vision, mapping, power-management, simulation, dashboard and Hive Mind repositories | SAND could eventually serve as a development/control platform or host selected services, but real-time robot firmware and safety-critical control must remain appropriately isolated |
| [DMJ language/toolchain projects](https://github.com/jadhavdurvesh) | Independent language, compiler, runtime and developer-tool work | Potential native compiler targets, SDKs, libraries and application runtimes once SAND defines its executable format and syscall ABI |

Other repositories in the user's GitHub account may also become candidates. Their exact roles should be checked from their own READMEs and source before adding them to this integration catalogue; repository names alone are not enough evidence.

## EREBUS: firewall integration boundary

EREBUS currently describes a portable Rust policy engine, validated versioned TOML policy, transactional in-memory policy reload, bounded connection tracking, event journal, token-bucket rate limiting, DNS parsing/policy/cache and a UDP/TCP DNS proxy. Its current status explicitly says native packet interception and OS resolver integration are not implemented.

Its adapter API accepts a normalized traffic descriptor and policy decision, then requires a platform adapter to apply the verdict and report failures. That is a useful future boundary for SAND.

1. Build SAND Ethernet, ARP, IPv4/IPv6, UDP and TCP foundations and a network-interface driver API.
2. Define a SAND-owned packet/flow hook with explicit ownership, lifetime, direction and error semantics.
3. Define a stable boundary between the SAND kernel and an isolated privileged EREBUS service.
4. Port or compile the portable EREBUS core for SAND userland only after a Rust/runtime strategy exists. The current 32-bit freestanding C kernel cannot simply link a normal Rust std crate.
5. Begin in observe-only mode with deterministic test packets and mock enforcement.
6. Enable enforcement only after deny/allow, failure-path, boot-recovery and isolation tests pass.

The kernel retains final authority over packet disposition. A missing or crashed policy service must follow a documented fail-safe mode; never silently turn an explicit deny into allow. Do not expose a fake firewall toggle before the packet hook is live.

## Shared foundations SAND should provide

Rather than embedding every project in the kernel, SAND should expose stable platform capabilities:

- Executable format, process lifecycle, virtual memory and permissions.
- Versioned syscall ABI and inter-process communication.
- Device model, driver API, interrupt and event delivery.
- Filesystem and block-storage API, separate from ANANTA's database semantics.
- Network interface, packet stack, socket/datagram API and packet policy hooks.
- Graphics compositor, window manager, input and audio APIs.
- Service manager, configuration, logging, audit and package/application model.
- Developer SDK, compiler target, linker format, debugging and profiling interfaces.

## Integration rules

- Keep projects in their own repositories; do not copy source into SAND without explicit architecture and license review.
- Prefer a small C ABI or versioned message protocol at the kernel/user boundary over linking application code into the kernel.
- Kernel code remains freestanding and avoids allocation, panic, blocking I/O or unbounded work in interrupt context.
- Treat configuration, packets and IPC messages as untrusted input.
- Version interfaces and add compatibility tests before integrating a project.
- DUSRM can inform architecture and responsibility validation, but it must not be represented as an executable subsystem.
- NOCTIS is the browser; ANANTA is the database/data engine; MAINTAIN AI is the device/industrial platform. Preserve these distinctions in all future plans.

## Proposed sequence

1. Complete SAND's executable loading and process lifecycle.
2. Complete device model, keyboard, display and storage expansion.
3. Build networking and IPC foundations.
4. Integrate EREBUS through a real native enforcement adapter; expose DATAGRAM's required transport boundary.
5. Establish service/API and serialization contracts for CONTRACT, OCTET and CALLGRAPH.
6. Build the graphical desktop and application runtime; then bring NOCTIS and other userland applications onto it.
7. Evaluate ANANTA, MAINTAIN AI, Relay, CONTINUUM, DMJ LLM and other services against the stable SAND APIs.
8. Use DUSRM as an architecture-validation reference across milestones, not as a runtime dependency.

This is a proposed integration map. Each project still requires a source, build-target, dependency, licensing and compatibility review before implementation is claimed.
