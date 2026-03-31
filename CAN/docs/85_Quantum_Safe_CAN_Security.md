# 85. Quantum-Safe CAN Security

- **Sections 1–3** establish the threat: why quantum computers break current CAN security (Shor's algorithm vs ECDH/ECDSA/RSA), the *harvest-now, decrypt-later* model relevant to 25-year vehicle lifespans, and PQC fundamentals (lattice, hash-based, code-based families).

- **Sections 4–6** define the architecture: a three-tier design that confines expensive PQC operations (ML-KEM, ML-DSA) to offline and session-setup phases, leaving real-time frames to CMAC-AES-256 — solving the fundamental bandwidth mismatch between PQC artifact sizes and CAN frame limits.

- **Section 7 (C/C++)** provides four working code examples using **liboqs**:
  - ML-KEM-768 key encapsulation + HKDF session key derivation
  - ML-DSA-65 firmware signing and ECU-side verification
  - CMAC-AES-256 SecOC-style per-frame MAC
  - Crypto-agile policy manager with runtime algorithm switching

- **Section 8 (Rust)** provides three examples using **pqcrypto-kyber** and **pqcrypto-dilithium**:
  - Full ECU ↔ Gateway ML-KEM session establishment with `ZeroizeOnDrop` key protection
  - ML-DSA-65 firmware signer + verifier with tamper detection test
  - Crypto-agile manager enforcing monotonic policy upgrades

- **Sections 9–12** cover hybrid classical+PQC schemes, migration state machines, ECU capability tiering, performance benchmarks on ARM Cortex-M4, and the relevant standards (NIST FIPS 203/204/205, ISO 21434, AUTOSAR SecOC, UN R155/R156).

## Preparing CAN Security Architectures for Post-Quantum Cryptographic Algorithms and Key Management

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [The Quantum Threat to Classical CAN Security](#2-the-quantum-threat-to-classical-can-security)
3. [Post-Quantum Cryptography (PQC) Fundamentals](#3-post-quantum-cryptography-pqc-fundamentals)
4. [Quantum-Safe CAN Architecture Design](#4-quantum-safe-can-architecture-design)
5. [NIST PQC Algorithm Overview for CAN](#5-nist-pqc-algorithm-overview-for-can)
6. [Key Management in a Post-Quantum CAN Environment](#6-key-management-in-a-post-quantum-can-environment)
7. [C/C++ Implementation Examples](#7-cc-implementation-examples)
8. [Rust Implementation Examples](#8-rust-implementation-examples)
9. [Hybrid Classical-Quantum Security Schemes](#9-hybrid-classical-quantum-security-schemes)
10. [Migration Strategy and Crypto-Agility](#10-migration-strategy-and-crypto-agility)
11. [Performance Considerations on Embedded ECUs](#11-performance-considerations-on-embedded-ecus)
12. [Standards and Compliance Roadmap](#12-standards-and-compliance-roadmap)
13. [Summary](#13-summary)

---

## 1. Introduction

The Controller Area Network (CAN) bus remains the backbone of automotive and industrial embedded communication, found in virtually every modern vehicle and industrial controller. For decades, the security of CAN-based systems has relied on classical cryptographic primitives — symmetric ciphers like AES, hash functions like SHA-2/SHA-3, and asymmetric algorithms like RSA and ECC (ECDSA, ECDH).

The emergence of large-scale **quantum computers** threatens to break the asymmetric foundations of current CAN security:

- **RSA** and **ECC** are vulnerable to Shor's algorithm, which runs in polynomial time on a quantum computer.
- **Symmetric keys** and **hashes** require significantly larger sizes (roughly doubling) to remain secure against Grover's algorithm, but are not rendered completely obsolete.

For automotive and industrial systems, vehicles and infrastructure can have operational lifetimes of **10–25 years**. Security decisions made today must account for cryptographic threats that will materialize mid-lifecycle. This is the *harvest-now, decrypt-later* (HNDL) attack model: adversaries capture encrypted CAN traffic today and decrypt it once sufficiently powerful quantum hardware exists.

**Quantum-Safe CAN Security** is the discipline of redesigning CAN authentication, key distribution, and secure boot schemes to withstand both classical and quantum adversaries, while meeting the real-time, low-bandwidth, and resource-constrained realities of CAN.

---

## 2. The Quantum Threat to Classical CAN Security

### 2.1 Current CAN Security Mechanisms

Typical automotive CAN security (per ISO 21434, AUTOSAR SecOC, and CANsec) uses:

| Mechanism | Algorithm | Quantum Vulnerability |
|---|---|---|
| Message Authentication | CMAC-AES-128 (SecOC) | Moderate (key size doubling needed) |
| ECU Authentication / Key Exchange | ECDH (SECP256r1) | **Critical** (Shor's algorithm) |
| Digital Signatures / Certificates | ECDSA / RSA-2048 | **Critical** (Shor's algorithm) |
| Secure Boot | RSA-PSS / ECDSA | **Critical** |
| Session Key Derivation | HKDF-SHA256 | Moderate (needs larger keys) |
| Random Number Generation | DRBG-based | Moderate |

### 2.2 Shor's Algorithm Impact

Shor's algorithm (1994) runs in O((log N)³) quantum gate operations to factor large integers and solve the discrete logarithm problem. A quantum computer with **~4,000 stable logical qubits** would break RSA-2048 and ECC-256 in hours.

```
Classical Security Level:
  RSA-2048    → Classical: 112-bit security     → Quantum: 0 bits
  ECC-256     → Classical: 128-bit security     → Quantum: 0 bits
  AES-128     → Classical: 128-bit security     → Quantum: ~64 bits (Grover)
  AES-256     → Classical: 256-bit security     → Quantum: ~128 bits (Grover)
  SHA-256     → Classical: 256-bit security     → Quantum: ~128 bits (Grover)
```

### 2.3 Harvest-Now, Decrypt-Later for CAN

Long-lived vehicles are particularly exposed:

```
Timeline Example (Automotive):
  2024 → Vehicle ECU firmware signed with ECDSA-256, keys exchanged via ECDH
  2030 → Cryptographically-relevant quantum computer (CRQC) becomes available
  2034 → Vehicle still on the road; firmware signed in 2024 can be forged retroactively
         CAN session keys negotiated in 2024 can be reconstructed
```

For CAN specifically, the risks include:

- **Firmware spoofing**: Old firmware signatures can be forged, enabling malicious ECU updates.
- **Key recovery**: Historic key exchange traffic can be decrypted, exposing long-term session keys.
- **Authentication bypass**: Forged MACs if the key derivation chain used RSA/ECDH is broken.
- **Secure boot compromise**: Boot-time signature verification bypassed.

---

## 3. Post-Quantum Cryptography (PQC) Fundamentals

Post-Quantum Cryptography (PQC) refers to classical (non-quantum) algorithms believed to be resistant to attacks by both classical and quantum computers. These run on standard CPUs without quantum hardware.

### 3.1 Mathematical Hard Problems Underpinning PQC

| Problem Family | Example Algorithm | Quantum Resistance Basis |
|---|---|---|
| Lattice-based | CRYSTALS-Kyber, CRYSTALS-Dilithium, NTRU | Shortest Vector Problem (SVP), Learning with Errors (LWE) |
| Hash-based | SPHINCS+, XMSS, LMS | Security of cryptographic hash functions |
| Code-based | Classic McEliece, BIKE, HQC | Syndrome Decoding Problem |
| Isogeny-based | SIKE (deprecated), SQISign | Supersingular Isogeny problems |
| Multivariate | Rainbow (deprecated), GeMSS | Multivariate Quadratic systems |

### 3.2 NIST PQC Standardization (2024)

NIST finalized its first PQC standards in 2024:

- **FIPS 203**: ML-KEM (Module-Lattice Key Encapsulation Mechanism, from Kyber) — Key exchange
- **FIPS 204**: ML-DSA (Module-Lattice Digital Signature Algorithm, from Dilithium) — Digital signatures
- **FIPS 205**: SLH-DSA (Stateless Hash-Based Digital Signature Algorithm, from SPHINCS+) — Digital signatures (hash-based alternative)

XMSS and LMS (hash-based, stateful signatures) are standardized in NIST SP 800-208.

### 3.3 Key Size Comparison

| Algorithm | Public Key | Private Key | Signature / CT | Notes |
|---|---|---|---|---|
| ECDSA P-256 | 64 bytes | 32 bytes | 64 bytes | Vulnerable |
| RSA-2048 | 256 bytes | ~1.2 KB | 256 bytes | Vulnerable |
| ML-DSA-44 (Dilithium2) | 1312 bytes | 2528 bytes | 2420 bytes | FIPS 204, 128-bit PQ |
| ML-DSA-65 (Dilithium3) | 1952 bytes | 4000 bytes | 3293 bytes | FIPS 204, 192-bit PQ |
| SLH-DSA-SHA2-128s | 32 bytes | 64 bytes | 7856 bytes | FIPS 205, small keys |
| ML-KEM-512 (Kyber512) | 800 bytes | 1632 bytes | CT: 768 bytes | FIPS 203, 128-bit PQ |
| ML-KEM-768 (Kyber768) | 1184 bytes | 2400 bytes | CT: 1088 bytes | FIPS 203, 192-bit PQ |
| XMSS-SHA2_10_256 | 64 bytes | 1.3 KB | 2500 bytes | SP 800-208, stateful |

The **large artifact sizes** are the central challenge for CAN, whose frames are limited to **8 bytes** (classic CAN) or **64 bytes** (CAN FD).

---

## 4. Quantum-Safe CAN Architecture Design

### 4.1 The Bandwidth Problem

CAN bandwidth is extremely limited:

- **Classic CAN**: 1 Mbit/s max, 8-byte payload per frame
- **CAN FD**: Up to 8 Mbit/s data phase, 64-byte payload per frame
- **CAN XL**: Up to 10 Mbit/s, up to 2048-byte payload (emerging)

A single ML-DSA-44 signature (2420 bytes) would require **~303 CAN classic frames** or **~38 CAN FD frames**. This is only feasible for **offline or semi-online operations** (e.g., OTA firmware update, key provisioning), not real-time message authentication.

### 4.2 Tiered Security Architecture

The solution is a **tiered approach** separating long-term PQC operations from real-time symmetric operations:

```
┌─────────────────────────────────────────────────────────────┐
│                    TIER 1: Offline / OTA                    │
│  PQC Digital Signatures (ML-DSA / SLH-DSA)                 │
│  - ECU firmware signing & verification                      │
│  - Certificate chain validation                             │
│  - Long-term key provisioning                               │
└────────────────────┬────────────────────────────────────────┘
                     │ Authenticated Key Derivation
┌────────────────────▼────────────────────────────────────────┐
│                    TIER 2: Session Setup                    │
│  PQC Key Encapsulation (ML-KEM / Kyber)                     │
│  - Session key establishment between ECUs                   │
│  - Gateway-mediated key distribution                        │
│  - Executed at startup or reconnect, not per-message        │
└────────────────────┬────────────────────────────────────────┘
                     │ Symmetric Session Keys
┌────────────────────▼────────────────────────────────────────┐
│                TIER 3: Real-Time CAN Traffic                │
│  Symmetric Cryptography (AES-256-GCM / CMAC-AES-256)       │
│  - Per-message authentication (AUTOSAR SecOC extended)      │
│  - Truncated MAC tags (4–8 bytes) in CAN payload           │
│  - Replay protection via freshness counters                 │
└─────────────────────────────────────────────────────────────┘
```

### 4.3 ECU Role Classification

```
┌──────────────────────────────────────────────────────┐
│                  CENTRAL GATEWAY ECU                 │
│  Full PQC Stack (ML-KEM + ML-DSA + AES-256)         │
│  - Manages ECU certificate provisioning              │
│  - Performs PQC key encapsulation on behalf of ECUs  │
│  - Distributes symmetric session keys over secure ch │
│  - Crypto-agile: can swap algorithms via policy      │
└───────────────────────┬──────────────────────────────┘
                        │ Authenticated CAN channel
         ┌──────────────┼──────────────┐
         ▼              ▼              ▼
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  HIGH-END   │  │  MID-RANGE  │  │  LOW-END    │
│    ECU      │  │    ECU      │  │    ECU      │
│ (ADAS/IVI)  │  │ (Body/PT)   │  │ (Sensor)    │
│ Full PQC    │  │ PQC-KEM +   │  │ Symmetric   │
│ stack on-   │  │ Symm. only  │  │ only (keys  │
│ chip HSM    │  │             │  │ provisioned)│
└─────────────┘  └─────────────┘  └─────────────┘
```

---

## 5. NIST PQC Algorithm Overview for CAN

### 5.1 ML-KEM (Kyber) — Key Encapsulation

ML-KEM is used to establish a shared secret between two parties. One party encapsulates a random secret using the other's public key, producing a ciphertext. The receiver decapsulates using their private key.

```
ECU_A has:   pk_B (B's ML-KEM public key, 800–1184 bytes)
ECU_A runs:  (ciphertext, shared_secret) = ML-KEM.Encap(pk_B)
ECU_A sends: ciphertext to ECU_B (768–1088 bytes, fragmented over CAN FD)
ECU_B runs:  shared_secret = ML-KEM.Decap(sk_B, ciphertext)
Both derive: session_key = HKDF-SHA256(shared_secret, context)
```

### 5.2 ML-DSA (Dilithium) — Digital Signatures

ML-DSA is used for firmware signing, certificate signing, and any scenario requiring non-repudiation.

```
Signer:   sig = ML-DSA.Sign(sk, message)    // 2420–4595 bytes
Verifier: valid = ML-DSA.Verify(pk, message, sig)
```

### 5.3 SLH-DSA (SPHINCS+) — Hash-Based Signatures

SLH-DSA provides an alternative with minimal security assumptions (only hash function security). Its stateless property is crucial for automotive use where state management is unreliable after power loss.

```
Trade-offs vs ML-DSA:
  + Smaller public/private keys (32/64 bytes)
  + Simpler security reduction (hash functions only)
  - Much larger signatures (7856–49856 bytes)
  - Slower signing (~10ms–1s depending on variant)
```

### 5.4 XMSS/LMS — Stateful Hash-Based Signatures (for ECU Firmware)

XMSS (eXtended Merkle Signature Scheme) is ideal for secure boot:

- The private key maintains a **one-time-use leaf counter**
- Signatures are smaller than SPHINCS+ (~2500 bytes for XMSS-SHA2_10_256)
- Automotive boot ROM can hold the Merkle root (only 32 bytes for the public key)
- State must be persisted in NVM; re-use of a leaf index is catastrophic

---

## 6. Key Management in a Post-Quantum CAN Environment

### 6.1 Lifecycle Phases

```
Phase 1: MANUFACTURING
  → ECU HSM generates ML-KEM keypair
  → Root CA (OEM HSM) signs ECU certificate using ML-DSA
  → XMSS root (firmware verification key) burned into OTP/eFuse
  → Initial symmetric keys provisioned via secure manufacturing channel

Phase 2: VEHICLE COMMISSIONING
  → Gateway validates all ECU certificates (ML-DSA chain verification)
  → Session keys established via ML-KEM between Gateway and each ECU
  → Group session key derived and distributed (encrypted under individual session keys)

Phase 3: OPERATION
  → Real-time: CMAC-AES-256 with freshness values on all CAN frames
  → Periodic: Session key rotation (daily/weekly) via ML-KEM re-encapsulation
  → Key epoch counters distributed via authenticated broadcast

Phase 4: OTA UPDATE
  → New firmware package signed with ML-DSA (or XMSS for boot-critical code)
  → Signature fragments delivered over CAN FD (multi-frame ISO 15765-2)
  → ECU verifies signature, updates XMSS leaf counter in NVM before flashing

Phase 5: END OF LIFE / KEY REVOCATION
  → Certificate Revocation List (CRL) distributed via backend to Gateway
  → Gateway broadcasts revocation notice (AES-256-GCM encrypted)
  → ECUs invalidate session keys associated with revoked ECU
```

### 6.2 Key Hierarchy

```
OEM Root CA Key (ML-DSA-87, offline HSM)
  └── Intermediate CA Key (ML-DSA-65, online backend HSM)
        ├── ECU Certificate (ML-DSA-44, per ECU)
        │     └── ECU ML-KEM Public Key (ML-KEM-768, per ECU)
        └── Firmware Signing Key (XMSS-SHA2_20_256, per platform)
              └── Per-build Firmware Signature (XMSS leaf)

Session Key Layer (ephemeral, per-session):
  ML-KEM Shared Secret → HKDF → { CAN_MAC_Key, CAN_ENC_Key, KDF_Key }
```

### 6.3 Freshness Counter and Anti-Replay

Quantum-safe MACs still require replay protection. A **64-bit trip counter + 16-bit message counter** scheme is recommended:

```
Freshness Value = [Trip_Counter(32b) | Msg_Counter(16b) | PDU_ID(16b)]
MAC_Tag = CMAC-AES-256(Session_Key, PDU_ID || Freshness_Value || Payload)[0:3]
```

---

## 7. C/C++ Implementation Examples

### 7.1 Prerequisites and Libraries

The following examples use:
- **liboqs** (Open Quantum Safe): C library for ML-KEM, ML-DSA, SPHINCS+
- **OpenSSL 3.x** with `oqs-provider`: For hybrid certificates and HKDF
- Standard POSIX sockets / CAN socket API (SocketCAN)

```bash
# Install liboqs and oqs-provider
git clone https://github.com/open-quantum-safe/liboqs.git
cmake -S liboqs -B liboqs/build -DOQS_DIST_BUILD=ON
cmake --build liboqs/build --parallel 4
sudo cmake --install liboqs/build
```

### 7.2 ML-KEM Key Encapsulation for CAN Session Key Establishment (C)

```c
/**
 * @file can_pq_kem.c
 * @brief Quantum-safe session key establishment for CAN using ML-KEM-768 (Kyber768)
 *
 * Implements the key encapsulation flow for a CAN gateway ECU acting as
 * the encapsulating party, and a remote ECU as the decapsulating party.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <oqs/oqs.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>

/* CAN-specific constants */
#define CAN_PQ_KEM_ALG          "ML-KEM-768"
#define CAN_SESSION_KEY_LEN     32      /* AES-256 */
#define CAN_MAC_KEY_LEN         32      /* CMAC-AES-256 */
#define CAN_FRAG_PAYLOAD_SIZE   48      /* CAN FD usable bytes per fragment */

/* Derived key context strings */
static const char *KDF_CONTEXT_MAC = "CAN-SESSION-MAC-KEY-v1";
static const char *KDF_CONTEXT_ENC = "CAN-SESSION-ENC-KEY-v1";

typedef struct {
    uint8_t *public_key;
    size_t   public_key_len;
    uint8_t *secret_key;
    size_t   secret_key_len;
} can_pq_keypair_t;

typedef struct {
    uint8_t  mac_key[CAN_MAC_KEY_LEN];
    uint8_t  enc_key[CAN_SESSION_KEY_LEN];
    uint64_t session_id;
    uint32_t epoch;
} can_session_keys_t;

/**
 * @brief Generate an ML-KEM-768 keypair for a CAN ECU.
 *        In production, private key lives in the ECU's HSM.
 */
int can_pq_keygen(can_pq_keypair_t *kp)
{
    OQS_KEM *kem = OQS_KEM_new(CAN_PQ_KEM_ALG);
    if (!kem) {
        fprintf(stderr, "[ERROR] ML-KEM-768 not supported by liboqs build\n");
        return -1;
    }

    kp->public_key_len = kem->length_public_key;
    kp->secret_key_len = kem->length_secret_key;

    kp->public_key = OQS_MEM_malloc(kp->public_key_len);
    kp->secret_key = OQS_MEM_malloc(kp->secret_key_len);

    if (!kp->public_key || !kp->secret_key) {
        OQS_KEM_free(kem);
        return -1;
    }

    OQS_STATUS rc = OQS_KEM_keypair(kem, kp->public_key, kp->secret_key);
    OQS_KEM_free(kem);

    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "[ERROR] ML-KEM key generation failed\n");
        return -1;
    }

    printf("[INFO] ML-KEM-768 keypair generated: pk=%zu bytes, sk=%zu bytes\n",
           kp->public_key_len, kp->secret_key_len);
    return 0;
}

/**
 * @brief Gateway (sender) encapsulates a shared secret using the remote ECU's public key.
 *        The ciphertext must be fragmented and sent over CAN FD.
 *
 * @param remote_pk      Remote ECU's ML-KEM-768 public key (1184 bytes)
 * @param remote_pk_len  Length of remote public key
 * @param ciphertext     Output: ciphertext (1088 bytes for ML-KEM-768) — to be sent over CAN
 * @param ct_len         Output: ciphertext length
 * @param session_keys   Output: derived session keys (MAC + ENC)
 * @return 0 on success, -1 on failure
 */
int can_pq_gateway_encapsulate(
    const uint8_t *remote_pk,
    size_t remote_pk_len,
    uint8_t **ciphertext,
    size_t *ct_len,
    can_session_keys_t *session_keys)
{
    OQS_KEM *kem = OQS_KEM_new(CAN_PQ_KEM_ALG);
    if (!kem) return -1;

    if (remote_pk_len != kem->length_public_key) {
        fprintf(stderr, "[ERROR] Invalid remote public key length: %zu (expected %zu)\n",
                remote_pk_len, kem->length_public_key);
        OQS_KEM_free(kem);
        return -1;
    }

    *ct_len = kem->length_ciphertext;
    *ciphertext = OQS_MEM_malloc(*ct_len);
    uint8_t *shared_secret = OQS_MEM_malloc(kem->length_shared_secret);

    if (!*ciphertext || !shared_secret) {
        OQS_KEM_free(kem);
        return -1;
    }

    OQS_STATUS rc = OQS_KEM_encaps(kem, *ciphertext, shared_secret, remote_pk);
    OQS_KEM_free(kem);

    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "[ERROR] ML-KEM encapsulation failed\n");
        OQS_MEM_secure_free(shared_secret, kem->length_shared_secret);
        return -1;
    }

    printf("[INFO] Encapsulated: shared_secret=%zu bytes, ciphertext=%zu bytes\n",
           (size_t)32, *ct_len);

    /* Derive CAN session keys from shared secret using HKDF-SHA256 */
    int ret = can_derive_session_keys(shared_secret, 32, session_keys);
    OQS_MEM_secure_free(shared_secret, 32);

    return ret;
}

/**
 * @brief Remote ECU decapsulates the ciphertext received over CAN FD to recover session keys.
 */
int can_pq_ecu_decapsulate(
    const uint8_t *ciphertext,
    size_t ct_len,
    const uint8_t *secret_key,
    size_t sk_len,
    can_session_keys_t *session_keys)
{
    OQS_KEM *kem = OQS_KEM_new(CAN_PQ_KEM_ALG);
    if (!kem) return -1;

    if (ct_len != kem->length_ciphertext || sk_len != kem->length_secret_key) {
        fprintf(stderr, "[ERROR] Ciphertext or secret key length mismatch\n");
        OQS_KEM_free(kem);
        return -1;
    }

    uint8_t *shared_secret = OQS_MEM_malloc(kem->length_shared_secret);
    if (!shared_secret) { OQS_KEM_free(kem); return -1; }

    OQS_STATUS rc = OQS_KEM_decaps(kem, shared_secret, ciphertext, secret_key);
    OQS_KEM_free(kem);

    if (rc != OQS_SUCCESS) {
        fprintf(stderr, "[ERROR] ML-KEM decapsulation failed (possible tampering)\n");
        OQS_MEM_secure_free(shared_secret, 32);
        return -1;
    }

    int ret = can_derive_session_keys(shared_secret, 32, session_keys);
    OQS_MEM_secure_free(shared_secret, 32);
    return ret;
}

/**
 * @brief Derive CAN MAC and encryption keys from ML-KEM shared secret using HKDF-SHA256.
 */
int can_derive_session_keys(
    const uint8_t *shared_secret,
    size_t ss_len,
    can_session_keys_t *keys)
{
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!kdf) return -1;

    /* Derive MAC key */
    EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
    OSSL_PARAM params[5];
    params[0] = OSSL_PARAM_construct_utf8_string("digest", "SHA256", 0);
    params[1] = OSSL_PARAM_construct_octet_string("key", (void*)shared_secret, ss_len);
    params[2] = OSSL_PARAM_construct_octet_string("info",
                    (void*)KDF_CONTEXT_MAC, strlen(KDF_CONTEXT_MAC));
    params[3] = OSSL_PARAM_construct_end();

    if (EVP_KDF_derive(ctx, keys->mac_key, CAN_MAC_KEY_LEN, params) <= 0) {
        EVP_KDF_CTX_free(ctx); EVP_KDF_free(kdf); return -1;
    }
    EVP_KDF_CTX_free(ctx);

    /* Derive ENC key */
    ctx = EVP_KDF_CTX_new(kdf);
    params[2] = OSSL_PARAM_construct_octet_string("info",
                    (void*)KDF_CONTEXT_ENC, strlen(KDF_CONTEXT_ENC));
    if (EVP_KDF_derive(ctx, keys->enc_key, CAN_SESSION_KEY_LEN, params) <= 0) {
        EVP_KDF_CTX_free(ctx); EVP_KDF_free(kdf); return -1;
    }
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);

    printf("[INFO] Session keys derived: MAC_KEY and ENC_KEY (AES-256 each)\n");
    return 0;
}

/**
 * @brief Fragment and simulate CAN FD transmission of ML-KEM ciphertext.
 *        In practice this maps to ISO 15765-2 (UDS multi-frame) or custom framing.
 */
void can_fd_fragment_and_send(const uint8_t *data, size_t len, uint32_t can_id)
{
    size_t offset = 0;
    uint8_t seq = 0;
    printf("[CAN FD] Fragmenting %zu bytes over CAN FD (CAN ID: 0x%03X)\n", len, can_id);

    while (offset < len) {
        size_t chunk = (len - offset > CAN_FRAG_PAYLOAD_SIZE)
                       ? CAN_FRAG_PAYLOAD_SIZE : (len - offset);
        printf("[CAN FD] Frame %3u: bytes [%4zu..%4zu] (%zu bytes)\n",
               seq, offset, offset + chunk - 1, chunk);
        offset += chunk;
        seq++;
    }
    printf("[CAN FD] Total frames: %u for %zu bytes\n", seq, len);
}
```

### 7.3 ML-DSA Digital Signature for CAN Firmware Signing (C++)

```cpp
/**
 * @file can_pq_sign.cpp
 * @brief Post-quantum firmware signing and verification for CAN ECUs using ML-DSA-65.
 *
 * Demonstrates the offline signing path (production toolchain) and the
 * online ECU verification path (runs on ECU at boot or OTA update).
 */

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <oqs/oqs.h>
#include <openssl/sha.h>

constexpr const char *ML_DSA_ALG = "ML-DSA-65";  /* FIPS 204 Level 3 */

class QuantumSafeSigner {
public:
    struct KeyPair {
        std::vector<uint8_t> public_key;
        std::vector<uint8_t> secret_key;
    };

    /**
     * @brief Generate an ML-DSA-65 keypair for firmware signing.
     *        In production, the secret key never leaves the OEM's HSM.
     */
    static KeyPair generateKeyPair() {
        OQS_SIG *sig = OQS_SIG_new(ML_DSA_ALG);
        if (!sig) throw std::runtime_error("ML-DSA-65 not available");

        KeyPair kp;
        kp.public_key.resize(sig->length_public_key);
        kp.secret_key.resize(sig->length_secret_key);

        OQS_STATUS rc = OQS_SIG_keypair(sig,
            kp.public_key.data(),
            kp.secret_key.data());
        OQS_SIG_free(sig);

        if (rc != OQS_SUCCESS)
            throw std::runtime_error("ML-DSA key generation failed");

        std::cout << "[INFO] ML-DSA-65 keypair: pk=" << kp.public_key.size()
                  << " bytes, sk=" << kp.secret_key.size() << " bytes\n";
        return kp;
    }

    /**
     * @brief Sign a firmware blob using ML-DSA-65.
     *        The message is first SHA3-256 hashed to reduce signature input size.
     *
     * @param firmware   Raw firmware binary
     * @param secret_key Signing key (HSM-protected in production)
     * @return Signature blob (3293 bytes for ML-DSA-65)
     */
    static std::vector<uint8_t> signFirmware(
        const std::vector<uint8_t> &firmware,
        const std::vector<uint8_t> &secret_key)
    {
        /* Hash firmware first — standard practice for large messages */
        std::vector<uint8_t> digest(32);
        SHA256(firmware.data(), firmware.size(), digest.data());

        OQS_SIG *sig = OQS_SIG_new(ML_DSA_ALG);
        if (!sig) throw std::runtime_error("ML-DSA-65 not available");

        std::vector<uint8_t> signature(sig->length_signature);
        size_t sig_len = 0;

        OQS_STATUS rc = OQS_SIG_sign(sig,
            signature.data(), &sig_len,
            digest.data(), digest.size(),
            secret_key.data());
        OQS_SIG_free(sig);

        if (rc != OQS_SUCCESS)
            throw std::runtime_error("ML-DSA signing failed");

        signature.resize(sig_len);
        std::cout << "[INFO] Firmware signed: " << sig_len << " bytes (ML-DSA-65)\n";
        return signature;
    }

    /**
     * @brief Verify firmware signature on ECU — runs at boot or OTA acceptance.
     *
     * @param firmware    Firmware to verify
     * @param signature   ML-DSA-65 signature
     * @param public_key  Embedded manufacturer public key (1952 bytes, stored in secure flash)
     * @return true if signature is valid
     */
    static bool verifyFirmware(
        const std::vector<uint8_t> &firmware,
        const std::vector<uint8_t> &signature,
        const std::vector<uint8_t> &public_key)
    {
        /* Recompute digest */
        std::vector<uint8_t> digest(32);
        SHA256(firmware.data(), firmware.size(), digest.data());

        OQS_SIG *sig = OQS_SIG_new(ML_DSA_ALG);
        if (!sig) return false;

        OQS_STATUS rc = OQS_SIG_verify(sig,
            digest.data(), digest.size(),
            signature.data(), signature.size(),
            public_key.data());
        OQS_SIG_free(sig);

        bool valid = (rc == OQS_SUCCESS);
        std::cout << "[INFO] Firmware signature verification: "
                  << (valid ? "VALID ✓" : "INVALID ✗") << "\n";
        return valid;
    }
};

/* ---- AUTOSAR-style SecOC MAC using AES-256-CMAC ---- */
/**
 * @brief Compute a truncated CMAC-AES-256 tag for a CAN PDU.
 *        The mac_key was established via ML-KEM session key derivation.
 *
 * @param mac_key      32-byte AES-256 key (from ML-KEM session)
 * @param pdu_id       CAN PDU identifier (2 bytes)
 * @param freshness    64-bit freshness counter value
 * @param payload      CAN payload data
 * @param tag_out      Output: 4-byte truncated MAC (fits in CAN frame alongside data)
 */
#include <openssl/cmac.h>

bool can_secoc_compute_mac(
    const uint8_t mac_key[32],
    uint16_t pdu_id,
    uint64_t freshness,
    const std::vector<uint8_t> &payload,
    uint8_t tag_out[4])
{
    /* Construct authenticated data: PDU_ID || Freshness || Payload */
    std::vector<uint8_t> auth_data;
    auth_data.push_back((pdu_id >> 8) & 0xFF);
    auth_data.push_back(pdu_id & 0xFF);
    for (int i = 7; i >= 0; i--)
        auth_data.push_back((freshness >> (i * 8)) & 0xFF);
    auth_data.insert(auth_data.end(), payload.begin(), payload.end());

    /* Compute CMAC-AES-256 */
    CMAC_CTX *ctx = CMAC_CTX_new();
    const EVP_CIPHER *cipher = EVP_aes_256_cbc();
    uint8_t full_mac[16];
    size_t mac_len = 0;

    if (!CMAC_Init(ctx, mac_key, 32, cipher, NULL) ||
        !CMAC_Update(ctx, auth_data.data(), auth_data.size()) ||
        !CMAC_Final(ctx, full_mac, &mac_len)) {
        CMAC_CTX_free(ctx);
        return false;
    }
    CMAC_CTX_free(ctx);

    /* Truncate to 4 bytes (per AUTOSAR SecOC recommendation for CAN) */
    memcpy(tag_out, full_mac, 4);
    return true;
}

/* ---- Demo main ---- */
int main()
{
    std::cout << "=== Quantum-Safe CAN Security Demo ===\n\n";

    /* 1. Key generation (OEM factory / HSM) */
    auto kp = QuantumSafeSigner::generateKeyPair();

    /* 2. Simulate firmware blob */
    std::vector<uint8_t> firmware(16384, 0xAB); /* 16 KB dummy firmware */

    /* 3. Sign firmware (OEM backend) */
    auto signature = QuantumSafeSigner::signFirmware(firmware, kp.secret_key);

    /* 4. ECU boot: verify firmware signature */
    bool ok = QuantumSafeSigner::verifyFirmware(firmware, signature, kp.public_key);
    if (!ok) {
        std::cerr << "[FATAL] Firmware verification failed — halting boot\n";
        return 1;
    }

    /* 5. Simulate CAN FD fragmentation of ML-KEM ciphertext for key setup */
    std::cout << "\n[INFO] Simulating ML-KEM-768 ciphertext CAN FD transmission:\n";
    /* ML-KEM-768 ciphertext = 1088 bytes, CAN FD = 64 bytes/frame */
    size_t ct_size = 1088;
    size_t frames_needed = (ct_size + 63) / 64;
    std::cout << "[CAN FD] ML-KEM-768 ciphertext requires " << frames_needed
              << " CAN FD frames (64 bytes each)\n";

    return 0;
}
```

### 7.4 Crypto-Agile CAN Security Manager (C++)

```cpp
/**
 * @file can_crypto_agile.cpp
 * @brief Crypto-agile CAN security manager supporting runtime algorithm selection.
 *
 * Crypto-agility is essential for quantum migration: the system must be able
 * to swap algorithms (e.g., ECDH → ML-KEM, ECDSA → ML-DSA) via configuration
 * or over-the-air policy update without code changes.
 */

#include <iostream>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <oqs/oqs.h>

enum class CANSecurityLevel {
    CLASSICAL_ONLY,       /* ECDH + ECDSA — legacy, vulnerable */
    HYBRID_TRANSITION,    /* ECDH+ML-KEM combined, ECDSA+ML-DSA combined */
    PQC_ONLY,             /* ML-KEM + ML-DSA only — fully quantum-safe */
};

struct CANSecurityPolicy {
    CANSecurityLevel    level;
    std::string         kem_algorithm;    /* Key encapsulation */
    std::string         sig_algorithm;    /* Digital signature */
    std::string         mac_algorithm;    /* Message authentication (symmetric) */
    uint32_t            mac_key_bits;     /* 128 or 256 */
    uint32_t            mac_tag_bytes;    /* Truncated tag length in CAN frame */
    bool                use_hybrid_sig;   /* Combine classical + PQC signature */
};

class CANCryptoManager {
public:
    static CANSecurityPolicy defaultPolicy(CANSecurityLevel level) {
        switch (level) {
        case CANSecurityLevel::CLASSICAL_ONLY:
            return { level, "ECDH", "ECDSA_P256", "CMAC-AES-128", 128, 4, false };

        case CANSecurityLevel::HYBRID_TRANSITION:
            return { level, "ML-KEM-768", "ML-DSA-65", "CMAC-AES-256", 256, 4, true };

        case CANSecurityLevel::PQC_ONLY:
            return { level, "ML-KEM-768", "ML-DSA-65", "CMAC-AES-256", 256, 6, false };

        default:
            throw std::invalid_argument("Unknown security level");
        }
    }

    explicit CANCryptoManager(CANSecurityPolicy policy)
        : policy_(policy) {
        validatePolicy();
        initializeKEM();
        std::cout << "[CANCryptoManager] Initialized with level="
                  << levelToString(policy_.level)
                  << ", KEM=" << policy_.kem_algorithm
                  << ", SIG=" << policy_.sig_algorithm << "\n";
    }

    /* Upgrade security policy at runtime (e.g., triggered by OTA config update) */
    bool upgradePolicy(const CANSecurityPolicy &new_policy) {
        if (static_cast<int>(new_policy.level) < static_cast<int>(policy_.level)) {
            std::cerr << "[WARN] Policy downgrade rejected (security monotonicity enforced)\n";
            return false;
        }
        policy_ = new_policy;
        validatePolicy();
        initializeKEM();
        std::cout << "[CANCryptoManager] Policy upgraded to "
                  << levelToString(policy_.level) << "\n";
        return true;
    }

    /* Perform key encapsulation — returns ciphertext for CAN transmission */
    std::vector<uint8_t> encapsulateKey(
        const std::vector<uint8_t> &remote_pk,
        std::vector<uint8_t> &shared_secret_out)
    {
        if (policy_.level == CANSecurityLevel::CLASSICAL_ONLY) {
            /* Placeholder: classical ECDH (not shown, use existing implementation) */
            throw std::runtime_error("Classical ECDH not implemented in this demo");
        }

        /* PQC or hybrid path */
        OQS_KEM *kem = OQS_KEM_new(policy_.kem_algorithm.c_str());
        if (!kem) throw std::runtime_error("KEM algorithm unavailable: " + policy_.kem_algorithm);

        std::vector<uint8_t> ct(kem->length_ciphertext);
        shared_secret_out.resize(kem->length_shared_secret);

        OQS_STATUS rc = OQS_KEM_encaps(kem, ct.data(), shared_secret_out.data(), remote_pk.data());
        OQS_KEM_free(kem);

        if (rc != OQS_SUCCESS)
            throw std::runtime_error("KEM encapsulation failed");

        return ct;
    }

    /* Query bandwidth overhead for current policy (useful for bus load planning) */
    void printBandwidthImpact() const {
        std::cout << "\n[BW Impact] Security Level: " << levelToString(policy_.level) << "\n";

        if (policy_.kem_algorithm == "ML-KEM-768") {
            std::cout << "  Key exchange ciphertext: 1088 bytes ("
                      << (1088 + 63) / 64 << " CAN FD frames)\n";
            std::cout << "  Public key size:         1184 bytes\n";
        }
        if (policy_.sig_algorithm == "ML-DSA-65") {
            std::cout << "  Firmware signature:      3293 bytes ("
                      << (3293 + 63) / 64 << " CAN FD frames)\n";
            std::cout << "  Signing public key:      1952 bytes\n";
        }
        std::cout << "  Per-frame MAC overhead:  " << policy_.mac_tag_bytes
                  << " bytes (+ freshness counter)\n";
        std::cout << "  Effective payload loss:  "
                  << (policy_.mac_tag_bytes * 100 / 8) << "% of 8-byte classic CAN frame\n";
    }

private:
    CANSecurityPolicy policy_;

    void validatePolicy() {
        if (policy_.mac_key_bits != 128 && policy_.mac_key_bits != 256)
            throw std::invalid_argument("MAC key must be 128 or 256 bits");
        if (policy_.mac_tag_bytes < 4 || policy_.mac_tag_bytes > 8)
            throw std::invalid_argument("MAC tag must be 4–8 bytes for CAN");
    }

    void initializeKEM() {
        if (policy_.kem_algorithm == "ML-KEM-768" || policy_.kem_algorithm == "ML-KEM-512") {
            OQS_KEM *kem = OQS_KEM_new(policy_.kem_algorithm.c_str());
            if (!kem) {
                std::cerr << "[WARN] Requested KEM " << policy_.kem_algorithm
                          << " unavailable, check liboqs build\n";
                return;
            }
            std::cout << "[INFO] KEM ready: " << policy_.kem_algorithm
                      << " (pk=" << kem->length_public_key
                      << ", ct=" << kem->length_ciphertext << " bytes)\n";
            OQS_KEM_free(kem);
        }
    }

    static std::string levelToString(CANSecurityLevel level) {
        switch (level) {
        case CANSecurityLevel::CLASSICAL_ONLY:    return "CLASSICAL_ONLY";
        case CANSecurityLevel::HYBRID_TRANSITION: return "HYBRID_TRANSITION";
        case CANSecurityLevel::PQC_ONLY:          return "PQC_ONLY";
        default:                                   return "UNKNOWN";
        }
    }
};

int main()
{
    /* Start with hybrid policy during migration period */
    auto policy = CANCryptoManager::defaultPolicy(CANSecurityLevel::HYBRID_TRANSITION);
    CANCryptoManager mgr(policy);
    mgr.printBandwidthImpact();

    /* Later, upgrade to full PQC via OTA policy push */
    std::cout << "\n[INFO] Simulating OTA policy upgrade to PQC_ONLY...\n";
    auto pqc_policy = CANCryptoManager::defaultPolicy(CANSecurityLevel::PQC_ONLY);
    mgr.upgradePolicy(pqc_policy);
    mgr.printBandwidthImpact();

    return 0;
}
```

---

## 8. Rust Implementation Examples

### 8.1 ML-KEM Key Exchange in Rust

```rust
//! can_pq_kem.rs
//! Quantum-safe CAN session key establishment using ML-KEM-768 in Rust.
//!
//! Dependencies (Cargo.toml):
//! [dependencies]
//! pqcrypto-kyber = "0.8"       # Kyber / ML-KEM implementation
//! pqcrypto-traits = "0.3"
//! hkdf = "0.12"
//! sha2 = "0.10"
//! zeroize = { version = "1.7", features = ["zeroize_derive"] }
//! thiserror = "1.0"
//! hex = "0.4"

use pqcrypto_kyber::kyber768;
use pqcrypto_traits::kem::{PublicKey, SecretKey, Ciphertext, SharedSecret};
use hkdf::Hkdf;
use sha2::Sha256;
use zeroize::{Zeroize, ZeroizeOnDrop};
use thiserror::Error;

// ── Error types ────────────────────────────────────────────────────────────

#[derive(Debug, Error)]
pub enum CanPqError {
    #[error("Key encapsulation failed")]
    EncapsulationFailed,
    #[error("Key decapsulation failed")]
    DecapsulationFailed,
    #[error("Key derivation failed: {0}")]
    KdfFailed(String),
    #[error("Invalid key material")]
    InvalidKey,
    #[error("CAN frame assembly error: {0}")]
    FramingError(String),
}

// ── Secure session key container (zeroed on drop) ──────────────────────────

#[derive(Zeroize, ZeroizeOnDrop)]
pub struct CanSessionKeys {
    pub mac_key: [u8; 32],   // AES-256 CMAC key for per-frame authentication
    pub enc_key: [u8; 32],   // AES-256 encryption key (optional, for encrypted frames)
    pub epoch:   u32,        // Key epoch for rotation tracking
}

impl std::fmt::Debug for CanSessionKeys {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "CanSessionKeys {{ epoch: {}, mac_key: [REDACTED], enc_key: [REDACTED] }}", self.epoch)
    }
}

// ── CAN FD fragmentation info ─────────────────────────────────────────────

pub struct CanFdFragmentInfo {
    pub total_bytes: usize,
    pub frame_count: usize,
    pub bytes_per_frame: usize,
}

impl CanFdFragmentInfo {
    /// Calculate CAN FD framing overhead for a given payload size.
    pub fn calculate(payload_len: usize) -> Self {
        const CAN_FD_MAX_PAYLOAD: usize = 64;
        // Reserve 2 bytes for sequence/length header in each frame
        let usable = CAN_FD_MAX_PAYLOAD - 2;
        let frames = payload_len.div_ceil(usable);
        Self {
            total_bytes: payload_len,
            frame_count: frames,
            bytes_per_frame: CAN_FD_MAX_PAYLOAD,
        }
    }

    pub fn print_summary(&self, label: &str) {
        println!(
            "[CAN FD] {}: {} bytes → {} frames ({} bytes each, 2-byte header)",
            label, self.total_bytes, self.frame_count, self.bytes_per_frame
        );
    }
}

// ── ML-KEM-768 keypair ─────────────────────────────────────────────────────

pub struct CanPqKeyPair {
    pub public_key: kyber768::PublicKey,
    secret_key: kyber768::SecretKey,  // Private — never expose directly
}

impl CanPqKeyPair {
    /// Generate a new ML-KEM-768 keypair for a CAN ECU.
    /// In production, the secret key is generated inside the HSM and never exported.
    pub fn generate() -> Self {
        let (pk, sk) = kyber768::keypair();
        println!(
            "[INFO] ML-KEM-768 keypair generated: pk={} bytes, sk={} bytes",
            pk.as_bytes().len(),
            sk.as_bytes().len()
        );
        CanPqKeyPair { public_key: pk, secret_key: sk }
    }

    /// Serialize the public key for distribution (e.g., stored in ECU certificate).
    pub fn public_key_bytes(&self) -> &[u8] {
        self.public_key.as_bytes()
    }
}

// ── Gateway (Encapsulating) side ──────────────────────────────────────────

pub struct CanGateway;

impl CanGateway {
    /// Encapsulate a session key for a remote ECU using its ML-KEM-768 public key.
    ///
    /// Returns:
    ///   - `ciphertext`: to be fragmented and sent over CAN FD to the remote ECU
    ///   - `session_keys`: locally derived session keys (MAC + ENC)
    pub fn establish_session(
        remote_pk_bytes: &[u8],
    ) -> Result<(Vec<u8>, CanSessionKeys), CanPqError> {
        // Reconstruct public key from bytes
        let remote_pk = kyber768::PublicKey::from_bytes(remote_pk_bytes)
            .map_err(|_| CanPqError::InvalidKey)?;

        // Encapsulate: generates random shared secret + ciphertext
        let (shared_secret, ciphertext) = kyber768::encapsulate(&remote_pk);

        let ct_bytes = ciphertext.as_bytes().to_vec();
        let frag_info = CanFdFragmentInfo::calculate(ct_bytes.len());
        frag_info.print_summary("ML-KEM-768 ciphertext");

        // Derive session keys from shared secret
        let session_keys = Self::derive_keys(shared_secret.as_bytes())?;
        println!("[INFO] Gateway: session keys established (epoch={})", session_keys.epoch);

        Ok((ct_bytes, session_keys))
    }

    fn derive_keys(shared_secret: &[u8]) -> Result<CanSessionKeys, CanPqError> {
        derive_session_keys(shared_secret, 0)
    }
}

// ── ECU (Decapsulating) side ──────────────────────────────────────────────

pub struct CanEcu {
    keypair: CanPqKeyPair,
    pub ecu_id: u8,
}

impl CanEcu {
    /// Create a new ECU with a freshly generated ML-KEM keypair.
    pub fn new(ecu_id: u8) -> Self {
        println!("[INFO] ECU 0x{:02X}: generating ML-KEM-768 keypair...", ecu_id);
        CanEcu { keypair: CanPqKeyPair::generate(), ecu_id }
    }

    /// Return the public key bytes for distribution to the gateway.
    pub fn public_key_bytes(&self) -> &[u8] {
        self.keypair.public_key_bytes()
    }

    /// Decapsulate a ciphertext received from the gateway to recover session keys.
    pub fn decapsulate_session(
        &self,
        ciphertext_bytes: &[u8],
    ) -> Result<CanSessionKeys, CanPqError> {
        let ciphertext = kyber768::Ciphertext::from_bytes(ciphertext_bytes)
            .map_err(|_| CanPqError::InvalidKey)?;

        let shared_secret = kyber768::decapsulate(&ciphertext, &self.keypair.secret_key);
        println!("[INFO] ECU 0x{:02X}: ML-KEM decapsulation successful", self.ecu_id);

        derive_session_keys(shared_secret.as_bytes(), 0)
    }
}

// ── Key derivation ─────────────────────────────────────────────────────────

fn derive_session_keys(
    shared_secret: &[u8],
    epoch: u32,
) -> Result<CanSessionKeys, CanPqError> {
    let hkdf = Hkdf::<Sha256>::new(None, shared_secret);
    let mut keys = CanSessionKeys {
        mac_key: [0u8; 32],
        enc_key: [0u8; 32],
        epoch,
    };

    hkdf.expand(b"CAN-SESSION-MAC-KEY-v1", &mut keys.mac_key)
        .map_err(|e| CanPqError::KdfFailed(e.to_string()))?;

    hkdf.expand(b"CAN-SESSION-ENC-KEY-v1", &mut keys.enc_key)
        .map_err(|e| CanPqError::KdfFailed(e.to_string()))?;

    Ok(keys)
}

// ── SecOC-style freshness + MAC for real-time CAN frames ──────────────────

/// Represents a quantum-safe authenticated CAN frame.
/// The 4-byte truncated MAC fits alongside real payload in an 8-byte CAN frame.
#[derive(Debug)]
pub struct SecOcCanFrame {
    pub can_id:       u32,
    pub payload:      [u8; 4],   // Effective payload (4 bytes after MAC reservation)
    pub mac_tag:      [u8; 4],   // Truncated CMAC-AES-256 (4 bytes)
    pub freshness_lsb: u16,      // Least significant 16 bits of freshness value
}

impl SecOcCanFrame {
    /// Verify the frame's MAC tag using the session key and full freshness value.
    pub fn verify(&self, mac_key: &[u8; 32], full_freshness: u64) -> bool {
        // In production: recompute CMAC-AES-256 over (can_id || freshness || payload)
        // and compare truncated result with self.mac_tag
        // This is a structural placeholder — use aes/cmac crate for full implementation
        let _ = (mac_key, full_freshness); // suppress unused warnings in demo
        println!(
            "[SecOC] Verifying frame 0x{:08X}: freshness_lsb=0x{:04X}",
            self.can_id, self.freshness_lsb
        );
        true // Placeholder
    }
}

// ── Demo ──────────────────────────────────────────────────────────────────

fn main() -> Result<(), CanPqError> {
    println!("=== Quantum-Safe CAN Security — Rust Demo ===\n");

    // Step 1: ECU generates ML-KEM keypair (done at manufacture / first boot)
    let ecu = CanEcu::new(0x1A);
    println!();

    // Step 2: Gateway fetches ECU's public key (via certificate or direct enrollment)
    let remote_pk = ecu.public_key_bytes().to_vec();
    println!("[INFO] Gateway: received ECU 0x{:02X} public key ({} bytes)", ecu.ecu_id, remote_pk.len());

    // Step 3: Gateway encapsulates session key, sends ciphertext over CAN FD
    let (ciphertext, gw_keys) = CanGateway::establish_session(&remote_pk)?;
    println!("[INFO] Gateway: ciphertext to send = {} bytes", ciphertext.len());
    println!("[INFO] Gateway: {:?}", gw_keys);
    println!();

    // Step 4: ECU receives fragmented ciphertext over CAN FD, reassembles, decapsulates
    let ecu_keys = ecu.decapsulate_session(&ciphertext)?;
    println!("[INFO] ECU 0x{:02X}: {:?}", ecu.ecu_id, ecu_keys);

    // Step 5: Verify both sides derived the same MAC key
    let keys_match = gw_keys.mac_key == ecu_keys.mac_key && gw_keys.enc_key == ecu_keys.enc_key;
    println!(
        "\n[RESULT] Session key agreement: {}",
        if keys_match { "✓ SUCCESS — keys match" } else { "✗ FAILURE — key mismatch!" }
    );

    // Step 6: Show bandwidth summary
    println!("\n[BW] ML-KEM-768 overhead summary:");
    CanFdFragmentInfo::calculate(1184).print_summary("Public key (ECU → Gateway)");
    CanFdFragmentInfo::calculate(1088).print_summary("Ciphertext (Gateway → ECU)");
    CanFdFragmentInfo::calculate(3293).print_summary("ML-DSA-65 firmware signature");

    Ok(())
}
```

### 8.2 Quantum-Safe Firmware Signer in Rust (ML-DSA / Dilithium)

```rust
//! can_pq_firmware_signer.rs
//! Post-quantum firmware signing with ML-DSA-65 (Dilithium3) for automotive ECUs.
//!
//! Cargo.toml:
//! [dependencies]
//! pqcrypto-dilithium = "0.5"
//! pqcrypto-traits = "0.3"
//! sha3 = "0.10"
//! zeroize = { version = "1.7", features = ["zeroize_derive"] }
//! thiserror = "1.0"

use pqcrypto_dilithium::dilithium3;
use pqcrypto_traits::sign::{PublicKey, SecretKey, SignedMessage, DetachedSignature};
use sha3::{Sha3_256, Digest};
use zeroize::{Zeroize, ZeroizeOnDrop};
use thiserror::Error;
use std::time::Instant;

#[derive(Debug, Error)]
pub enum FirmwareSignError {
    #[error("Signing failed")]
    SigningFailed,
    #[error("Verification failed — firmware tampered or wrong key")]
    VerificationFailed,
    #[error("Firmware hash computation failed")]
    HashFailed,
}

/// Firmware metadata embedded alongside the binary.
#[derive(Debug, Clone)]
pub struct FirmwareManifest {
    pub ecu_part_number:  String,
    pub version_major:    u16,
    pub version_minor:    u16,
    pub version_patch:    u16,
    pub build_timestamp:  u64,
    pub target_hardware:  String,
}

impl FirmwareManifest {
    /// Serialize manifest to bytes for inclusion in signed message.
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(self.ecu_part_number.as_bytes());
        v.extend_from_slice(&self.version_major.to_be_bytes());
        v.extend_from_slice(&self.version_minor.to_be_bytes());
        v.extend_from_slice(&self.version_patch.to_be_bytes());
        v.extend_from_slice(&self.build_timestamp.to_be_bytes());
        v.extend_from_slice(self.target_hardware.as_bytes());
        v
    }
}

/// ML-DSA-65 signing key — held exclusively in OEM HSM.
#[derive(ZeroizeOnDrop)]
pub struct FirmwareSigningKey {
    inner: dilithium3::SecretKey,
    pub public_key: dilithium3::PublicKey,
}

impl FirmwareSigningKey {
    /// Generate a new ML-DSA-65 signing keypair.
    pub fn generate() -> Self {
        let (pk, sk) = dilithium3::keypair();
        println!(
            "[INFO] ML-DSA-65 keypair: pk={} bytes, sk={} bytes, sig_size={} bytes",
            pk.as_bytes().len(),
            sk.as_bytes().len(),
            // Dilithium3 signature size is 3293 bytes
            3293usize
        );
        FirmwareSigningKey { inner: sk, public_key: pk }
    }

    /// Sign a firmware blob + manifest using ML-DSA-65.
    ///
    /// The signed message includes:
    ///   SHA3-256(firmware) || manifest_bytes
    /// This binds the signature to both content and metadata.
    pub fn sign_firmware(
        &self,
        firmware: &[u8],
        manifest: &FirmwareManifest,
    ) -> Result<Vec<u8>, FirmwareSignError> {
        let t0 = Instant::now();

        // Hash firmware to produce fixed-size digest
        let mut hasher = Sha3_256::new();
        hasher.update(firmware);
        let fw_hash: Vec<u8> = hasher.finalize().to_vec();

        // Assemble signed content: hash + manifest
        let mut signed_content = Vec::with_capacity(32 + 256);
        signed_content.extend_from_slice(&fw_hash);
        signed_content.extend_from_slice(&manifest.to_bytes());

        // ML-DSA-65 sign
        let sig = dilithium3::detached_sign(&signed_content, &self.inner);
        let sig_bytes = sig.as_bytes().to_vec();

        let elapsed = t0.elapsed();
        println!(
            "[INFO] ML-DSA-65 signing complete: {} bytes, took {:.2}ms",
            sig_bytes.len(),
            elapsed.as_secs_f64() * 1000.0
        );
        Ok(sig_bytes)
    }
}

/// Firmware verifier — runs on ECU at secure boot or OTA acceptance.
pub struct FirmwareVerifier {
    trusted_pk_bytes: Vec<u8>,
}

impl FirmwareVerifier {
    /// Create a verifier from the embedded manufacturer public key.
    /// In automotive production, this is stored in signed, read-only flash.
    pub fn new(public_key_bytes: &[u8]) -> Self {
        FirmwareVerifier {
            trusted_pk_bytes: public_key_bytes.to_vec(),
        }
    }

    /// Verify firmware signature on ECU.
    ///
    /// This is the critical path executed:
    ///   1. At secure boot (before executing any firmware code)
    ///   2. After OTA download (before writing to flash)
    pub fn verify_firmware(
        &self,
        firmware: &[u8],
        manifest: &FirmwareManifest,
        signature_bytes: &[u8],
    ) -> Result<(), FirmwareSignError> {
        let t0 = Instant::now();

        // Reconstruct public key
        let pk = dilithium3::PublicKey::from_bytes(&self.trusted_pk_bytes)
            .map_err(|_| FirmwareSignError::VerificationFailed)?;

        // Reconstruct signature
        let sig = dilithium3::DetachedSignature::from_bytes(signature_bytes)
            .map_err(|_| FirmwareSignError::VerificationFailed)?;

        // Recompute signed content (same as signing)
        let mut hasher = Sha3_256::new();
        hasher.update(firmware);
        let fw_hash: Vec<u8> = hasher.finalize().to_vec();

        let mut signed_content = Vec::new();
        signed_content.extend_from_slice(&fw_hash);
        signed_content.extend_from_slice(&manifest.to_bytes());

        // Verify ML-DSA-65 signature
        dilithium3::verify_detached_signature(&sig, &signed_content, &pk)
            .map_err(|_| FirmwareSignError::VerificationFailed)?;

        let elapsed = t0.elapsed();
        println!(
            "[INFO] ML-DSA-65 verification: VALID ✓ (took {:.2}ms)",
            elapsed.as_secs_f64() * 1000.0
        );
        Ok(())
    }
}

/// Compute required CAN FD frames to transmit a firmware signature.
fn can_fd_frames_for_signature(sig_len: usize) -> usize {
    const CAN_FD_USABLE: usize = 62; // 64 - 2 bytes overhead
    sig_len.div_ceil(CAN_FD_USABLE)
}

fn main() -> Result<(), FirmwareSignError> {
    println!("=== Quantum-Safe CAN Firmware Signer (Rust / ML-DSA-65) ===\n");

    // 1. Generate signing key (OEM factory HSM — done once per platform)
    let signing_key = FirmwareSigningKey::generate();
    println!();

    // 2. Prepare firmware + manifest (build system output)
    let firmware = vec![0xDE_u8; 128 * 1024]; // 128 KB dummy ECU firmware
    let manifest = FirmwareManifest {
        ecu_part_number:  "ECU-BRAKES-MCU-A2".to_string(),
        version_major:    2,
        version_minor:    4,
        version_patch:    11,
        build_timestamp:  1_711_900_000,
        target_hardware:  "ARM-Cortex-M33".to_string(),
    };
    println!("[INFO] Firmware: {} bytes, version {}.{}.{}",
        firmware.len(), manifest.version_major, manifest.version_minor, manifest.version_patch);

    // 3. Sign firmware (done in CI/CD pipeline or OEM signing server)
    let signature = signing_key.sign_firmware(&firmware, &manifest)?;
    let frames = can_fd_frames_for_signature(signature.len());
    println!("[INFO] Signature requires {} CAN FD frames to transmit", frames);
    println!();

    // 4. ECU: verify signature at secure boot or OTA install
    let verifier = FirmwareVerifier::new(signing_key.public_key.as_bytes());
    verifier.verify_firmware(&firmware, &manifest, &signature)?;

    // 5. Simulate tampered firmware detection
    println!("\n[TEST] Simulating tampered firmware...");
    let mut tampered = firmware.clone();
    tampered[1000] ^= 0xFF;  // Flip a byte

    match verifier.verify_firmware(&tampered, &manifest, &signature) {
        Ok(_)  => println!("[FAIL] Tampered firmware was not detected!"),
        Err(e) => println!("[PASS] Tamper detected: {} ✓", e),
    }

    Ok(())
}
```

### 8.3 Crypto-Agile Session Manager in Rust

```rust
//! can_crypto_agile.rs
//! Crypto-agile CAN security manager with runtime algorithm negotiation.
//!
//! Demonstrates policy-driven algorithm selection and graceful migration
//! from classical to hybrid to full PQC security.
//!
//! Cargo.toml:
//! [dependencies]
//! serde = { version = "1.0", features = ["derive"] }
//! serde_json = "1.0"
//! thiserror = "1.0"

use serde::{Deserialize, Serialize};
use std::fmt;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum PolicyError {
    #[error("Policy downgrade rejected: current={current}, requested={requested}")]
    DowngradeRejected { current: String, requested: String },
    #[error("Algorithm {0} is not supported by this ECU's hardware")]
    AlgorithmNotSupported(String),
    #[error("Invalid configuration: {0}")]
    InvalidConfig(String),
}

/// Security level enum with ordering — downgrade is always rejected.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum SecurityLevel {
    ClassicalOnly       = 0,
    HybridTransition    = 1,
    PqcOnly             = 2,
}

impl fmt::Display for SecurityLevel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SecurityLevel::ClassicalOnly    => write!(f, "CLASSICAL_ONLY"),
            SecurityLevel::HybridTransition => write!(f, "HYBRID_TRANSITION"),
            SecurityLevel::PqcOnly          => write!(f, "PQC_ONLY"),
        }
    }
}

/// Describes all cryptographic parameters for a CAN security configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CanSecurityPolicy {
    pub level:          SecurityLevel,
    pub kem_algorithm:  String,         // "ML-KEM-768", "ML-KEM-512", "ECDH-P256"
    pub sig_algorithm:  String,         // "ML-DSA-65", "ML-DSA-44", "ECDSA-P256"
    pub mac_algorithm:  String,         // "CMAC-AES-256", "CMAC-AES-128"
    pub mac_tag_bytes:  u8,             // 4–8 bytes truncated tag in CAN frame
    pub key_epoch_s:    u32,            // Session key rotation interval (seconds)
    pub hybrid_mode:    bool,           // Combine classical + PQC for transition period
}

impl CanSecurityPolicy {
    /// Create the recommended policy for each security level.
    pub fn recommended(level: SecurityLevel) -> Result<Self, PolicyError> {
        match level {
            SecurityLevel::ClassicalOnly => Ok(Self {
                level,
                kem_algorithm: "ECDH-P256".into(),
                sig_algorithm: "ECDSA-P256".into(),
                mac_algorithm: "CMAC-AES-128".into(),
                mac_tag_bytes: 4,
                key_epoch_s:   86400,  // 24 hours
                hybrid_mode:   false,
            }),

            SecurityLevel::HybridTransition => Ok(Self {
                level,
                kem_algorithm: "ML-KEM-768".into(),    // PQC KEM
                sig_algorithm: "ML-DSA-65".into(),      // PQC signature
                mac_algorithm: "CMAC-AES-256".into(),   // Quantum-hardened MAC
                mac_tag_bytes: 4,
                key_epoch_s:   3600,   // 1 hour — more frequent rotation during migration
                hybrid_mode:   true,   // Also perform ECDH for classical interoperability
            }),

            SecurityLevel::PqcOnly => Ok(Self {
                level,
                kem_algorithm: "ML-KEM-768".into(),
                sig_algorithm: "ML-DSA-65".into(),
                mac_algorithm: "CMAC-AES-256".into(),
                mac_tag_bytes: 6,      // Slightly larger tag for stronger truncation
                key_epoch_s:   7200,   // 2 hours
                hybrid_mode:   false,
            }),
        }
    }

    /// Estimate CAN FD frames required for key exchange under this policy.
    pub fn key_exchange_overhead(&self) -> KeyExchangeOverhead {
        let (pk_bytes, ct_bytes) = match self.kem_algorithm.as_str() {
            "ML-KEM-512" => (800,  768),
            "ML-KEM-768" => (1184, 1088),
            "ML-KEM-1024"=> (1568, 1568),
            "ECDH-P256"  => (65,   65),
            _            => (0,    0),
        };

        let sig_bytes = match self.sig_algorithm.as_str() {
            "ML-DSA-44"   => 2420,
            "ML-DSA-65"   => 3293,
            "ML-DSA-87"   => 4595,
            "ECDSA-P256"  => 64,
            _             => 0,
        };

        const CAN_FD_USABLE: usize = 62;
        KeyExchangeOverhead {
            public_key_bytes:    pk_bytes,
            ciphertext_bytes:    ct_bytes,
            signature_bytes:     sig_bytes,
            pk_frames:           pk_bytes.div_ceil(CAN_FD_USABLE),
            ct_frames:           ct_bytes.div_ceil(CAN_FD_USABLE),
            sig_frames:          sig_bytes.div_ceil(CAN_FD_USABLE),
            mac_overhead_bytes:  self.mac_tag_bytes as usize,
        }
    }
}

#[derive(Debug)]
pub struct KeyExchangeOverhead {
    pub public_key_bytes:   usize,
    pub ciphertext_bytes:   usize,
    pub signature_bytes:    usize,
    pub pk_frames:          usize,
    pub ct_frames:          usize,
    pub sig_frames:         usize,
    pub mac_overhead_bytes: usize,
}

impl fmt::Display for KeyExchangeOverhead {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "  Public key:           {:5} bytes  → {:3} CAN FD frames", self.public_key_bytes, self.pk_frames)?;
        writeln!(f, "  KEM ciphertext:       {:5} bytes  → {:3} CAN FD frames", self.ciphertext_bytes, self.ct_frames)?;
        writeln!(f, "  Firmware signature:   {:5} bytes  → {:3} CAN FD frames", self.signature_bytes, self.sig_frames)?;
        write!(f,   "  Per-frame MAC tag:       {} bytes  (in 8-byte CAN payload)",  self.mac_overhead_bytes)
    }
}

/// The crypto-agile manager for a CAN node.
pub struct CanCryptoManager {
    ecu_id:  u8,
    policy:  CanSecurityPolicy,
}

impl CanCryptoManager {
    pub fn new(ecu_id: u8, level: SecurityLevel) -> Result<Self, PolicyError> {
        let policy = CanSecurityPolicy::recommended(level)?;
        println!("[CANCryptoManager] ECU 0x{:02X} initialized: level={}", ecu_id, policy.level);
        Ok(Self { ecu_id, policy })
    }

    /// Apply a new security policy from an OTA configuration update.
    /// Enforces monotonicity: level can only increase, never decrease.
    pub fn apply_policy(&mut self, new_policy: CanSecurityPolicy) -> Result<(), PolicyError> {
        if new_policy.level < self.policy.level {
            return Err(PolicyError::DowngradeRejected {
                current:   self.policy.level.to_string(),
                requested: new_policy.level.to_string(),
            });
        }

        // Validate MAC tag size for CAN feasibility
        if new_policy.mac_tag_bytes < 4 || new_policy.mac_tag_bytes > 8 {
            return Err(PolicyError::InvalidConfig(
                "MAC tag must be 4–8 bytes for CAN frame compatibility".into()
            ));
        }

        println!(
            "[CANCryptoManager] ECU 0x{:02X}: policy upgraded {} → {}",
            self.ecu_id, self.policy.level, new_policy.level
        );
        self.policy = new_policy;
        Ok(())
    }

    pub fn print_policy_summary(&self) {
        println!("\n╔═══ CAN Security Policy (ECU 0x{:02X}) ═══╗", self.ecu_id);
        println!("║  Level:         {}", self.policy.level);
        println!("║  KEM:           {}", self.policy.kem_algorithm);
        println!("║  Signature:     {}", self.policy.sig_algorithm);
        println!("║  MAC:           {} ({} bytes truncated)", self.policy.mac_algorithm, self.policy.mac_tag_bytes);
        println!("║  Key rotation:  {}s", self.policy.key_epoch_s);
        println!("║  Hybrid mode:   {}", self.policy.hybrid_mode);
        println!("╠═══ Bandwidth Overhead ══════════════════╣");
        println!("{}", self.policy.key_exchange_overhead());
        println!("╚═════════════════════════════════════════╝");
    }
}

fn main() -> Result<(), PolicyError> {
    println!("=== Crypto-Agile CAN Security Manager (Rust) ===\n");

    // ECU starts with hybrid policy (migration phase)
    let mut mgr = CanCryptoManager::new(0x2B, SecurityLevel::HybridTransition)?;
    mgr.print_policy_summary();

    // Simulate OTA policy upgrade to full PQC
    println!("\n[OTA] Received policy upgrade command from backend...");
    let pqc_policy = CanSecurityPolicy::recommended(SecurityLevel::PqcOnly)?;
    mgr.apply_policy(pqc_policy)?;
    mgr.print_policy_summary();

    // Attempt illegal downgrade — must be rejected
    println!("\n[SECURITY] Attempting illegal policy downgrade...");
    let classical = CanSecurityPolicy::recommended(SecurityLevel::ClassicalOnly)?;
    match mgr.apply_policy(classical) {
        Err(PolicyError::DowngradeRejected { current, requested }) =>
            println!("[PASS] Downgrade correctly rejected: {} → {} ✓", current, requested),
        Ok(_) => println!("[FAIL] Downgrade was incorrectly accepted!"),
        Err(e) => println!("[ERROR] Unexpected error: {}", e),
    }

    Ok(())
}
```

---

## 9. Hybrid Classical-Quantum Security Schemes

During the **transition period** (estimated 2025–2035), neither classical nor PQC algorithms should be exclusively trusted. Hybrid schemes combine both:

### 9.1 Hybrid Key Exchange

```
Hybrid KEM = ECDH-P256 + ML-KEM-768

shared_secret_classical = ECDH(ECU_sk, GW_pk)
shared_secret_pqc       = ML-KEM-768-Decaps(CT, ECU_sk_kem)
combined_secret         = SHA3-256(shared_secret_classical || shared_secret_pqc || context)
session_keys            = HKDF-SHA256(combined_secret, "CAN-HYBRID-SESSION-v1")

Security guarantee: attacker must break BOTH ECDH AND ML-KEM-768 to recover session keys.
```

### 9.2 Hybrid Signatures

```
hybrid_sig = (ECDSA_P256_sig || ML-DSA-65_sig)

Verifier accepts only if BOTH signatures are valid.
This prevents a scenario where:
  - Classical verifier rejects a valid PQC-only signature (legacy ECU)
  - Quantum attacker forges an ECDSA-only signature (classic attack)
```

### 9.3 Migration State Machine

```
                  ┌─────────────────┐
                  │  CLASSICAL_ONLY  │  ← Existing deployed ECUs
                  │  ECDH + ECDSA   │
                  │  CMAC-AES-128   │
                  └────────┬────────┘
                           │ OTA firmware + key provisioning
                           ▼
                  ┌─────────────────┐
                  │    HYBRID        │  ← Transition (2024–2028 est.)
                  │  ECDH+ML-KEM    │
                  │  ECDSA+ML-DSA   │
                  │  CMAC-AES-256   │
                  └────────┬────────┘
                           │ All ECUs in network confirmed PQC-capable
                           ▼
                  ┌─────────────────┐
                  │   PQC_ONLY       │  ← Target state
                  │  ML-KEM-768     │
                  │  ML-DSA-65      │
                  │  CMAC-AES-256   │
                  └─────────────────┘
```

---

## 10. Migration Strategy and Crypto-Agility

### 10.1 Crypto-Agility Requirements for AUTOSAR

A quantum-ready AUTOSAR Crypto Stack must expose algorithm-independent APIs:

```
[Classic AUTOSAR Crypto Stack]                    [PQC Extension]
  Csm_Encrypt(jobId, dataIn, dataOut)         →   Csm_KemEncaps(jobId, pk, ct, ss)
  Csm_Decrypt(jobId, dataIn, dataOut)         →   Csm_KemDecaps(jobId, sk, ct, ss)
  Csm_SignatureGenerate(jobId, data, sig)     →   Csm_PqcSign(jobId, data, sig)
  Csm_SignatureVerify(jobId, data, sig, ver)  →   Csm_PqcVerify(jobId, data, sig, ver)
```

Key design principle: **algorithm identifiers** are policy parameters, not compile-time constants.

### 10.2 ECU Capability Tiers

| Tier | Example ECU | PQC Capability | Strategy |
|---|---|---|---|
| Tier A | Gateway, IVI, ADAS SoC | Full PQC + hybrid | On-chip HSM with ML-KEM/ML-DSA hardware acceleration |
| Tier B | Body, Powertrain MCU | ML-KEM decaps + CMAC-AES-256 | Gateway performs encaps; ECU decaps + symmetric only |
| Tier C | Simple sensor ECU | Symmetric only | Keys provisioned via gateway; no asymmetric on-node |

### 10.3 Key Migration Protocol

```
Phase A (NOW): Provision PQC public keys alongside ECDH keys during manufacturing
Phase B: OTA update delivers PQC-capable firmware to Tier A ECUs
Phase C: Gateway begins hybrid key exchange (ECDH+ML-KEM) with Tier A ECUs
Phase D: OTA rolls hybrid firmware to Tier B ECUs
Phase E: All intra-vehicle session keys now established via ML-KEM
Phase F: ECDH disabled, classical asymmetric code removed
```

---

## 11. Performance Considerations on Embedded ECUs

### 11.1 Benchmark Overview (ARM Cortex-M4 @ 120 MHz, no hardware acceleration)

| Operation | Classical | ML-KEM-768 | ML-DSA-65 |
|---|---|---|---|
| Key generation | ECDH: ~5 ms | ~17 ms | ~38 ms |
| Sign / Encaps | ECDSA: ~8 ms | Encaps: ~18 ms | Sign: ~80 ms |
| Verify / Decaps | ECDSA: ~12 ms | Decaps: ~19 ms | Verify: ~30 ms |
| Memory (stack) | ~2 KB | ~5 KB | ~8 KB |
| Memory (keys) | ~128 bytes | ~5 KB total | ~10 KB total |

ML-KEM and ML-DSA operations are feasible on **Tier A/B ECUs** but should not be performed per-message. The 3-tier architecture (Section 4.2) ensures PQC operations are confined to startup/session establishment.

### 11.2 Hardware Acceleration

Modern automotive-grade SoCs (Renesas RH850/U2A, NXP S32G, Infineon AURIX TC4xx) are adding:

- **NTT (Number Theoretic Transform)** hardware units — accelerates Kyber/Dilithium inner products by ~10×
- **Side-channel-resistant multipliers** — protects lattice operations against power analysis
- **HSM with PQC co-processor** — isolates long-term private key operations

### 11.3 CAN Bus Load Impact

With the 3-tier architecture, PQC impacts only session setup, not steady-state:

```
Steady-state bus load (per frame):
  Classical:  0-byte MAC overhead (no SecOC) OR 4-byte MAC with CMAC-AES-128
  PQC-safe:   6-byte MAC with CMAC-AES-256 (+ freshness 2 bytes) → same as classical SecOC

Session setup overhead (one-time, at startup):
  ML-KEM-768 key exchange for 20 ECUs:
    20 × (1184 + 1088) bytes = ~44 KB
    At 8 Mbit/s CAN FD: ~44 ms of bus time (negligible)
```

---

## 12. Standards and Compliance Roadmap

| Standard | Relevance | PQC Status |
|---|---|---|
| ISO/SAE 21434 | Automotive cybersecurity risk management | Implicitly requires crypto-agility; PQC guidance expected in revision |
| AUTOSAR SecOC | CAN message authentication | AES-256 baseline; PQC KEM API extension under discussion |
| UN R155 / R156 | Cybersecurity management system | Requires lifecycle key management; aligns with PQC migration |
| NIST FIPS 203/204/205 | ML-KEM, ML-DSA, SLH-DSA | Finalized 2024; reference for automotive profiles |
| NIST SP 800-208 | XMSS, LMS stateful signatures | Recommended for automotive secure boot |
| ETSI TS 103 744 | PQC migration guidance | Directly applicable to V2X and CAN gateway PKI |
| ISO 15765-2 | UDS transport (multi-frame over CAN) | Used for fragmenting PQC key exchange data |
| BSI TR-02102-1 | German technical guideline for cryptography | Recommends hybrid PQC from 2024 |

### 12.1 OEM Action Items (2024–2028)

1. **Inventory**: Audit all ECU firmware and gateway code for classical asymmetric algorithm usage
2. **Classify**: Assign Tier A/B/C to all ECUs; identify CAN FD-capable networks
3. **HSM provisioning**: Require new ECU designs to include PQC-capable HSMs
4. **Hybrid keys**: Begin issuing dual ECDSA+ML-DSA firmware signing certificates
5. **CAN FD mandate**: Require CAN FD (64-byte frames) for all buses involved in key management
6. **AUTOSAR upgrade**: Update Crypto Service Manager to expose KEM and PQC signature APIs
7. **Backend PKI**: Migrate backend CA infrastructure to ML-DSA certificate chains

---

## 13. Summary

Quantum-Safe CAN Security addresses a critical long-term threat to automotive and industrial embedded systems. The convergence of quantum computing advances and the 10–25 year lifetimes of CAN-equipped platforms creates a real and pressing cryptographic transition challenge.

**Core threat**: Shor's algorithm will render all current CAN asymmetric cryptography (RSA, ECDH, ECDSA) obsolete once cryptographically-relevant quantum computers (CRQCs) emerge. The *harvest-now, decrypt-later* model makes this a present-day risk for long-lived vehicles.

**Solution framework**: A three-tier architecture separates concerns effectively:
- **Tier 1 (Offline)**: ML-DSA / SPHINCS+ / XMSS digital signatures for firmware and certificates — large artifacts are acceptable offline
- **Tier 2 (Session setup)**: ML-KEM (Kyber) key encapsulation replaces ECDH — large ciphertexts fragmented over CAN FD, performed once at startup
- **Tier 3 (Real-time)**: CMAC-AES-256 with freshness counters for per-message authentication — quantum-hardened symmetric operations fit within CAN frame constraints

**Key design principles**:
- **Crypto-agility**: Algorithms are policy parameters, not compile-time constants, enabling OTA algorithm migration
- **Hybrid security during transition**: Combining classical + PQC (ECDH + ML-KEM, ECDSA + ML-DSA) ensures security against both quantum and classical adversaries during the migration period
- **Monotonic policy enforcement**: Security levels can only increase, never be downgraded — prevents adversarial policy rollback attacks
- **HSM-anchored key storage**: All long-term PQC private keys reside exclusively in hardware security modules; software never handles raw private key material

**NIST standardized algorithms** (FIPS 203/204/205) — ML-KEM, ML-DSA, and SLH-DSA — provide the cryptographic foundation, supported by liboqs (C/C++) and the pqcrypto ecosystem (Rust) on embedded platforms today.

**Implementation readiness**: ML-KEM-768 (1184-byte public key, 1088-byte ciphertext) is feasible over CAN FD with multi-frame fragmentation. ML-DSA-65 (3293-byte signatures) is suitable for firmware signing and OTA flows. Both algorithms run within acceptable time budgets on mid-range automotive MCUs, particularly those with emerging NTT hardware acceleration.

The migration path from classical-only to hybrid to PQC-only is achievable through OTA firmware updates, standardized API extensions (AUTOSAR CSM), and coordinated OEM key provisioning — making quantum-safe CAN an engineering challenge that is well-defined, standards-backed, and increasingly urgent to act on.

---

*Document: 85_Quantum_Safe_CAN_Security.md | Topic: Post-Quantum Cryptography for CAN Bus Security*
*References: NIST FIPS 203/204/205, ISO/SAE 21434, AUTOSAR SecOC, NIST SP 800-208, liboqs, pqcrypto-rs*