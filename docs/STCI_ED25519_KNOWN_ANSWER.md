# STCI Ed25519 known-answer vector

Status: interoperability fixture for the SAPP v2 verifier implementation. This vector is not itself a production trust root or a publisher credential.

## Vector: SAND-SAPP-V2-ED25519 / v1

The private seed below is the public RFC 8032 test seed and is included only to make this deterministic test vector reproducible. It MUST NOT be used as a real SAND root, publisher, release, or website signing key.

- Algorithm: Ed25519
- Public key (raw 32 bytes):
  `d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a`
- Domain separator: ASCII `SAND-SAPP-V2-ED25519` followed by NUL
- Test payload: ASCII `SAPP-STCI-KAT-v1` followed by NUL and bytes `00 01 02 ... 1f`
- Exact signed transcript (hex):
  `53414e442d534150502d56322d4544323535313900534150502d535443492d4b41542d763100000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f`
- Expected Ed25519 signature (64 bytes):
  `ad3c43fa3f87f5679468515bbc48f1ebe2b92233762082858a6e6cf431bd4424d98351425a70a8eb36ab0d7c4549a500ebb24910bb65fdc423b17933c0b6ec01`
- SHA-256 of transcript (diagnostic only; Ed25519 signs the transcript directly):
  `9f27195e3c49efb8c28b317311566a8091b2b3dd1fe43a02af5d6326c08d8fca`

## Required verifier tests

1. The public key, transcript and signature above verify successfully.
2. Flipping any one bit in the transcript fails verification.
3. Flipping any one bit in the signature fails verification.
4. A different public key fails verification.
5. A signature of the payload without the domain separator fails verification.
6. Empty, truncated and oversized key/signature inputs fail safely without out-of-bounds reads.

The native verifier must run this fixture in host tests and in the freestanding test harness before it is used by the SAPP loader. Additional fixtures must cover the full certificate chain and exact SAPP v2 header/payload transcript emitted by the current SAND Apps host tools.

## Reproduction

The vector was generated with OpenSSL 3.x Ed25519 using the RFC 8032 test seed and the transcript above. The seed is intentionally not stored in this repository.
