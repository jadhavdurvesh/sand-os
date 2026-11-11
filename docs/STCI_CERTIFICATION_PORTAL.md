# SAND App Certification Portal — Architecture

Status: proposed service architecture. This document defines how a future SAND application website can accept submissions and certify applications. It does not claim that a public portal, automated malware scanner, online signing service or native OS trust enforcement currently exists.

## Two different decisions

The portal must keep these decisions separate:

1. **Publisher identity / code-signing eligibility:** who controls the signing key? A publisher certificate binds an identified publisher to an Ed25519 public key with `codeSigning` usage. SAPP v2 uses this certificate to verify who signed the executable.
2. **Application certification:** has SAND reviewed this exact application release and approved it for the SAND catalogue? This is a per-release decision bound to the exact package digest, version and review record.

A publisher certificate alone does NOT mean every application from that publisher is certified. SAPP v2 currently carries a publisher certificate and publisher signature; it does not define a separate SAND app-review endorsement. Do not silently treat the publisher signature as an app certification. A future signed catalogue attestation or a versioned SAPP extension must carry per-release approval.

## Recommended website workflow

1. **Publisher account:** applicant registers, verifies contact/organization identity and accepts the publisher terms.
2. **Key enrollment:** publisher creates an Ed25519 key pair locally or in their protected signing environment. They submit only the public key and proof-of-possession signature. Never ask a publisher to upload a private key.
3. **Application submission:** publisher uploads a SAPP v1 development image or SAPP v2 candidate plus manifest, source repository/revision, build instructions, dependencies, requested capabilities, screenshots and release notes. Reject malformed/oversized archives before deeper processing.
4. **Automated intake:** isolate the upload; inspect the SAPP structure; verify publisher signature if present; calculate a cryptographic digest; scan archive paths for traversal, symlinks and decompression bombs; run static checks and reproducible-build comparison where available. No submitted binary runs on the portal host.
5. **Human review:** review requested capabilities, network/filesystem behavior, provenance, privacy, update behavior, known vulnerabilities, accessibility and app functionality. Record reviewer identity, evidence, exceptions and decision.
6. **Decision:** reject, request changes, or approve this exact release. Approval records app ID, version, architecture, digest, publisher key ID, review-policy version, timestamp and expiry/review status.
7. **Publisher signing:** publisher signs the native SAPP v2 executable with their own private key. The portal verifies the signature and certificate chain against SAND's trust policy.
8. **SAND certification attestation:** after approval, the portal issues a separate SAND-signed catalogue attestation for that exact digest. This attestation must use a dedicated certification key, not the offline root key. The attestation is not currently part of SAPP v2 and must not be represented as kernel-enforced until an OS verifier for it is implemented.
9. **Distribution:** publish the exact reviewed artifact, its digest, publisher identity, SAND review status, requested capabilities, compatibility range, changelog and revocation status. Any byte change requires a new digest and review/signing decision.
10. **Updates and revocation:** every release is reviewed independently or under an explicit update policy. Provide a visible revocation/withdrawal mechanism for compromised keys, malicious releases and abandoned apps.

## Trust hierarchy and key custody

- **Offline SAND root:** held offline; used only for root/intermediate lifecycle and recovery. Never deployed to the website application server.
- **Online certification intermediate:** constrained key used by the isolated certification/signing service to issue publisher certificates, if that issuance model is selected. Prefer HSM-backed or equivalent non-exportable key custody, dual control and audited operations.
- **SAND app-review attestation key:** separate key and key usage for per-release certification. It must not be interchangeable with publisher code-signing keys.
- **Publisher private keys:** controlled by publishers; never collected by SAND.
- **Device trust store:** public keys only, provisioned by a trusted OS installation/update path. A downloaded app or website response must never be allowed to add its own trusted root.

The current `sandcert.py publisher-issue` command uses the root private key directly to issue publisher certificates. That is suitable only for an offline administrative workflow, not a public web request handler. The website must not expose that command with the root key mounted. Before online issuance, define and implement a constrained intermediate certificate chain and audited signing service, or keep certificate issuance as a manual offline approval operation.

## Security controls for the portal

- Separate upload, analysis, review, signing and distribution services/accounts.
- No execution of submitted binaries on the web/API host; use disposable, network-restricted sandboxes for dynamic analysis.
- Strict upload limits, content-type-independent parsing, archive traversal protection, resource/time limits and malware scanning.
- Require proof of possession for enrolled publisher keys.
- MFA and role separation for reviewers and signing approvers; dual approval for root/intermediate changes and high-impact certification.
- Append-only audit records for submissions, downloads, review actions, certificate issuance, signing, revocation and key rotation.
- Reproducible build evidence where source is available; disclose when review is binary-only.
- Clear privacy/data-retention policy for source code, logs, crash reports and publisher identity documents.
- No promise that automated scanning proves software safe. Certification is a bounded review, not a guarantee.

## Native OS enforcement contract

The OS must independently enforce the policy; it cannot trust the website UI or an online API response at launch time.

- SAPP v2 publisher verification authenticates the publisher and executable integrity.
- A separate SAND certification attestation authenticates SAND's approval of a particular digest/release.
- If product policy requires only SAND-certified applications, the OS must require both valid publisher signature and valid app attestation (or an explicitly equivalent future signed catalogue design).
- If offline operation is required, the attestation and required public trust anchors must be cached in an integrity-protected local store with explicit expiry/revocation semantics.
- Unknown, expired, revoked, malformed or mismatched signatures fail closed in production mode.
- Developer mode, if supported, must be explicit, visibly marked and auditable; it must not silently downgrade verification.
- Signing is not sandboxing. Capability checks, memory isolation, syscall validation and resource limits remain mandatory.

## Suggested implementation phases

1. Native SAPP v2 parser and Ed25519 verifier with shared known-answer vectors.
2. Protected root provisioning, certificate validation and trust-store policy.
3. Native per-release certification-attestation verifier and local cache format.
4. Host-side submission CLI and review metadata schema.
5. Website publisher dashboard, submission workflow and reviewer console.
6. Isolated signing service, intermediate-key custody, audit trail and revocation.
7. Public catalogue/download service with digest verification and OS compatibility metadata.

The current SAPP v2 feature branch in `sand-apps` supplies host-side publisher signing/verification. It does not yet supply a per-app SAND endorsement format or native SAND OS enforcement. Keep these gaps visible until implemented and tested.
