# CAN Bootloader Security

## Overview

CAN (Controller Area Network) bootloader security is critical for protecting embedded systems from unauthorized firmware modifications, malicious code injection, and other security threats. This document covers secure boot mechanisms, signature verification, and rollback protection specifically for CAN-based bootloaders.

## Core Security Concepts

### 1. Secure Boot
Secure boot ensures that only authenticated and authorized firmware can execute on the device. The bootloader verifies firmware integrity before allowing execution.

### 2. Signature Verification
Digital signatures confirm that firmware comes from a trusted source and hasn't been tampered with during transmission or storage.

### 3. Rollback Protection
Prevents attackers from downgrading firmware to older versions with known vulnerabilities.

## Architecture Components

A secure CAN bootloader typically includes:

- **Root of Trust**: Hardware-based security foundation (often in ROM)
- **Cryptographic Library**: For signature verification (RSA, ECDSA, Ed25519)
- **Secure Storage**: For keys, version numbers, and firmware metadata
- **CAN Communication Layer**: Secure firmware download protocol
- **Firmware Verification Engine**: Validates signatures and versions

## Implementation Examples

### C/C++ Implementation

```c
// secure_bootloader.h
#ifndef SECURE_BOOTLOADER_H
#define SECURE_BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

// Firmware metadata structure
typedef struct {
    uint32_t version;           // Firmware version number
    uint32_t size;              // Firmware size in bytes
    uint32_t crc32;             // CRC32 checksum
    uint8_t signature[64];      // Digital signature (e.g., Ed25519)
    uint32_t timestamp;         // Build timestamp
    uint8_t reserved[32];       // Reserved for future use
} firmware_metadata_t;

// Bootloader state
typedef enum {
    BL_STATE_INIT,
    BL_STATE_WAITING,
    BL_STATE_DOWNLOADING,
    BL_STATE_VERIFYING,
    BL_STATE_INSTALLING,
    BL_STATE_ERROR
} bootloader_state_t;

// Security status codes
typedef enum {
    SEC_OK = 0,
    SEC_INVALID_SIGNATURE,
    SEC_ROLLBACK_DETECTED,
    SEC_CORRUPTED_FIRMWARE,
    SEC_UNAUTHORIZED_SOURCE
} security_status_t;

// Function prototypes
security_status_t verify_firmware_signature(
    const uint8_t* firmware, 
    uint32_t size,
    const uint8_t* signature,
    const uint8_t* public_key
);

security_status_t check_rollback_protection(uint32_t new_version);
bool verify_crc32(const uint8_t* data, uint32_t size, uint32_t expected_crc);
void store_firmware_version(uint32_t version);
uint32_t get_current_firmware_version(void);

#endif // SECURE_BOOTLOADER_H
```

```c
// secure_bootloader.c
#include "secure_bootloader.h"
#include <string.h>

// Example using a simplified Ed25519 verification
// In production, use a proven crypto library like mbedTLS or wolfSSL
extern int ed25519_verify(
    const uint8_t* signature,
    const uint8_t* message,
    uint32_t message_len,
    const uint8_t* public_key
);

// Non-volatile storage for firmware version (e.g., EEPROM/Flash)
static uint32_t stored_firmware_version = 0;

// Public key stored in bootloader (typically in protected flash)
static const uint8_t BOOTLOADER_PUBLIC_KEY[32] = {
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    // ... 32 bytes total
};

/**
 * Verify firmware signature using Ed25519
 */
security_status_t verify_firmware_signature(
    const uint8_t* firmware, 
    uint32_t size,
    const uint8_t* signature,
    const uint8_t* public_key
) {
    if (!firmware || !signature || !public_key) {
        return SEC_INVALID_SIGNATURE;
    }

    // Verify the signature
    int result = ed25519_verify(signature, firmware, size, public_key);
    
    if (result != 0) {
        return SEC_INVALID_SIGNATURE;
    }

    return SEC_OK;
}

/**
 * Check for rollback attacks
 */
security_status_t check_rollback_protection(uint32_t new_version) {
    uint32_t current_version = get_current_firmware_version();
    
    // Prevent downgrade to older versions
    if (new_version < current_version) {
        return SEC_ROLLBACK_DETECTED;
    }
    
    return SEC_OK;
}

/**
 * CRC32 calculation for firmware integrity
 */
uint32_t calculate_crc32(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

bool verify_crc32(const uint8_t* data, uint32_t size, uint32_t expected_crc) {
    return calculate_crc32(data, size) == expected_crc;
}

/**
 * Complete firmware verification process
 */
security_status_t verify_firmware_complete(
    const uint8_t* firmware,
    const firmware_metadata_t* metadata
) {
    security_status_t status;
    
    // Step 1: Verify CRC32
    if (!verify_crc32(firmware, metadata->size, metadata->crc32)) {
        return SEC_CORRUPTED_FIRMWARE;
    }
    
    // Step 2: Check rollback protection
    status = check_rollback_protection(metadata->version);
    if (status != SEC_OK) {
        return status;
    }
    
    // Step 3: Verify digital signature
    status = verify_firmware_signature(
        firmware,
        metadata->size,
        metadata->signature,
        BOOTLOADER_PUBLIC_KEY
    );
    
    return status;
}

/**
 * CAN bootloader main processing loop
 */
void bootloader_process_can_message(
    uint32_t can_id,
    const uint8_t* data,
    uint8_t length
) {
    static bootloader_state_t state = BL_STATE_INIT;
    static uint8_t firmware_buffer[256 * 1024]; // 256KB firmware buffer
    static uint32_t bytes_received = 0;
    static firmware_metadata_t metadata;
    
    switch (state) {
        case BL_STATE_INIT:
            // Initialize bootloader
            state = BL_STATE_WAITING;
            break;
            
        case BL_STATE_WAITING:
            // Check for firmware download start command
            if (can_id == 0x7E0 && data[0] == 0x01) {
                // Extract metadata from first message
                memcpy(&metadata, &data[1], sizeof(firmware_metadata_t));
                bytes_received = 0;
                state = BL_STATE_DOWNLOADING;
            }
            break;
            
        case BL_STATE_DOWNLOADING:
            // Receive firmware data
            if (can_id == 0x7E1) {
                uint32_t offset = (data[0] << 8) | data[1];
                uint8_t chunk_size = data[2];
                memcpy(&firmware_buffer[offset], &data[3], chunk_size);
                bytes_received += chunk_size;
                
                if (bytes_received >= metadata.size) {
                    state = BL_STATE_VERIFYING;
                }
            }
            break;
            
        case BL_STATE_VERIFYING:
            // Verify firmware
            security_status_t status = verify_firmware_complete(
                firmware_buffer,
                &metadata
            );
            
            if (status == SEC_OK) {
                state = BL_STATE_INSTALLING;
            } else {
                state = BL_STATE_ERROR;
                // Send error response on CAN
            }
            break;
            
        case BL_STATE_INSTALLING:
            // Flash firmware to memory
            // Update stored version
            store_firmware_version(metadata.version);
            // Reset to application
            break;
            
        case BL_STATE_ERROR:
            // Handle error state
            break;
    }
}

// Storage functions (platform-specific)
void store_firmware_version(uint32_t version) {
    // Write to non-volatile memory (EEPROM/Flash)
    stored_firmware_version = version;
}

uint32_t get_current_firmware_version(void) {
    return stored_firmware_version;
}
```

### Rust Implementation

```rust
// secure_bootloader.rs
use core::convert::TryInto;

// Firmware metadata structure
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct FirmwareMetadata {
    pub version: u32,
    pub size: u32,
    pub crc32: u32,
    pub signature: [u8; 64],
    pub timestamp: u32,
    pub reserved: [u8; 32],
}

// Security status enum
#[derive(Debug, PartialEq)]
pub enum SecurityStatus {
    Ok,
    InvalidSignature,
    RollbackDetected,
    CorruptedFirmware,
    UnauthorizedSource,
}

// Bootloader state
#[derive(Debug, PartialEq)]
pub enum BootloaderState {
    Init,
    Waiting,
    Downloading,
    Verifying,
    Installing,
    Error,
}

// Public key constant (stored in bootloader)
const BOOTLOADER_PUBLIC_KEY: [u8; 32] = [
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
];

/// CRC32 calculation
pub fn calculate_crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFFFFFF;
    
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            let mask = (crc & 1).wrapping_neg();
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    
    !crc
}

/// Verify CRC32 checksum
pub fn verify_crc32(data: &[u8], expected_crc: u32) -> bool {
    calculate_crc32(data) == expected_crc
}

/// Check for rollback attacks
pub fn check_rollback_protection(
    new_version: u32,
    current_version: u32,
) -> SecurityStatus {
    if new_version < current_version {
        SecurityStatus::RollbackDetected
    } else {
        SecurityStatus::Ok
    }
}

/// Verify firmware signature using Ed25519
/// (requires ed25519-dalek or similar crate)
pub fn verify_firmware_signature(
    firmware: &[u8],
    signature: &[u8; 64],
    public_key: &[u8; 32],
) -> SecurityStatus {
    // In production, use ed25519-dalek:
    // use ed25519_dalek::{PublicKey, Signature, Verifier};
    //
    // let public_key = PublicKey::from_bytes(public_key).unwrap();
    // let signature = Signature::from_bytes(signature).unwrap();
    //
    // match public_key.verify(firmware, &signature) {
    //     Ok(_) => SecurityStatus::Ok,
    //     Err(_) => SecurityStatus::InvalidSignature,
    // }
    
    // Placeholder for demonstration
    SecurityStatus::Ok
}

/// Complete firmware verification
pub fn verify_firmware_complete(
    firmware: &[u8],
    metadata: &FirmwareMetadata,
    current_version: u32,
) -> SecurityStatus {
    // Step 1: Verify CRC32
    if !verify_crc32(firmware, metadata.crc32) {
        return SecurityStatus::CorruptedFirmware;
    }
    
    // Step 2: Check rollback protection
    let rollback_status = check_rollback_protection(
        metadata.version,
        current_version,
    );
    if rollback_status != SecurityStatus::Ok {
        return rollback_status;
    }
    
    // Step 3: Verify digital signature
    verify_firmware_signature(
        firmware,
        &metadata.signature,
        &BOOTLOADER_PUBLIC_KEY,
    )
}

/// Secure bootloader implementation
pub struct SecureBootloader {
    state: BootloaderState,
    firmware_buffer: [u8; 256 * 1024], // 256KB
    bytes_received: usize,
    metadata: Option<FirmwareMetadata>,
    current_version: u32,
}

impl SecureBootloader {
    pub fn new(current_version: u32) -> Self {
        Self {
            state: BootloaderState::Init,
            firmware_buffer: [0; 256 * 1024],
            bytes_received: 0,
            metadata: None,
            current_version,
        }
    }
    
    /// Process incoming CAN message
    pub fn process_can_message(
        &mut self,
        can_id: u32,
        data: &[u8],
    ) -> Result<(), &'static str> {
        match self.state {
            BootloaderState::Init => {
                self.state = BootloaderState::Waiting;
                Ok(())
            }
            
            BootloaderState::Waiting => {
                if can_id == 0x7E0 && data.len() > 1 && data[0] == 0x01 {
                    // Parse metadata
                    self.metadata = self.parse_metadata(&data[1..])?;
                    self.bytes_received = 0;
                    self.state = BootloaderState::Downloading;
                }
                Ok(())
            }
            
            BootloaderState::Downloading => {
                if can_id == 0x7E1 && data.len() >= 3 {
                    let offset = ((data[0] as usize) << 8) | (data[1] as usize);
                    let chunk_size = data[2] as usize;
                    
                    if offset + chunk_size <= self.firmware_buffer.len() {
                        self.firmware_buffer[offset..offset + chunk_size]
                            .copy_from_slice(&data[3..3 + chunk_size]);
                        self.bytes_received += chunk_size;
                        
                        if let Some(ref meta) = self.metadata {
                            if self.bytes_received >= meta.size as usize {
                                self.state = BootloaderState::Verifying;
                            }
                        }
                    }
                }
                Ok(())
            }
            
            BootloaderState::Verifying => {
                if let Some(ref metadata) = self.metadata {
                    let firmware = &self.firmware_buffer[..metadata.size as usize];
                    let status = verify_firmware_complete(
                        firmware,
                        metadata,
                        self.current_version,
                    );
                    
                    if status == SecurityStatus::Ok {
                        self.state = BootloaderState::Installing;
                        Ok(())
                    } else {
                        self.state = BootloaderState::Error;
                        Err("Firmware verification failed")
                    }
                } else {
                    Err("No metadata available")
                }
            }
            
            BootloaderState::Installing => {
                // Flash firmware and update version
                if let Some(ref metadata) = self.metadata {
                    self.current_version = metadata.version;
                    // Trigger reset to application
                }
                Ok(())
            }
            
            BootloaderState::Error => {
                Err("Bootloader in error state")
            }
        }
    }
    
    fn parse_metadata(&self, data: &[u8]) -> Result<Option<FirmwareMetadata>, &'static str> {
        if data.len() < core::mem::size_of::<FirmwareMetadata>() {
            return Err("Insufficient data for metadata");
        }
        
        // Safely parse metadata
        let version = u32::from_le_bytes(data[0..4].try_into().unwrap());
        let size = u32::from_le_bytes(data[4..8].try_into().unwrap());
        let crc32 = u32::from_le_bytes(data[8..12].try_into().unwrap());
        
        let mut signature = [0u8; 64];
        signature.copy_from_slice(&data[12..76]);
        
        let timestamp = u32::from_le_bytes(data[76..80].try_into().unwrap());
        
        let mut reserved = [0u8; 32];
        reserved.copy_from_slice(&data[80..112]);
        
        Ok(Some(FirmwareMetadata {
            version,
            size,
            crc32,
            signature,
            timestamp,
            reserved,
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_crc32() {
        let data = b"Hello, World!";
        let crc = calculate_crc32(data);
        assert!(verify_crc32(data, crc));
    }
    
    #[test]
    fn test_rollback_protection() {
        assert_eq!(
            check_rollback_protection(2, 1),
            SecurityStatus::Ok
        );
        assert_eq!(
            check_rollback_protection(1, 2),
            SecurityStatus::RollbackDetected
        );
    }
}
```

### Advanced Security Features (C++)

```cpp
// advanced_security.hpp
#ifndef ADVANCED_SECURITY_HPP
#define ADVANCED_SECURITY_HPP

#include <cstdint>
#include <array>
#include <optional>

namespace can_bootloader {

// Secure monotonic counter for rollback protection
class MonotonicCounter {
public:
    // Increment counter (irreversible)
    bool increment();
    
    // Get current value
    uint32_t get_value() const;
    
    // Verify that new_value is greater than stored value
    bool verify_increment(uint32_t new_value) const;
    
private:
    uint32_t counter_{0};
    // In production, this would be backed by OTP memory or secure element
};

// Anti-rollback protection with version pinning
class AntiRollback {
public:
    struct VersionPolicy {
        uint32_t minimum_version;  // Absolute minimum
        uint32_t pinned_version;   // Current installed version
        bool allow_same_version;   // Allow reinstall of same version
    };
    
    explicit AntiRollback(const VersionPolicy& policy);
    
    bool is_version_allowed(uint32_t new_version) const;
    void update_pinned_version(uint32_t version);
    
private:
    VersionPolicy policy_;
};

// Secure key storage abstraction
class SecureKeyStore {
public:
    // Get public key for verification
    std::array<uint8_t, 32> get_public_key() const;
    
    // Verify key hasn't been tampered with
    bool verify_key_integrity() const;
    
private:
    static constexpr std::array<uint8_t, 32> PUBLIC_KEY = {
        0x12, 0x34, 0x56, 0x78, // ... rest of key
    };
};

// Secure boot chain verification
class SecureBootChain {
public:
    enum class Stage {
        ROM_BOOTLOADER,
        BOOTLOADER,
        APPLICATION
    };
    
    // Verify each stage of the boot process
    bool verify_stage(Stage stage, const uint8_t* code, size_t size);
    
    // Ensure chain of trust from ROM to application
    bool verify_chain();
    
private:
    bool rom_verified_{false};
    bool bootloader_verified_{false};
};

} // namespace can_bootloader

#endif // ADVANCED_SECURITY_HPP
```

## Security Best Practices

1. **Key Management**
   - Store public keys in read-only memory
   - Never expose private keys in bootloader
   - Use hardware security modules when available

2. **Cryptographic Strength**
   - Use modern algorithms (Ed25519, ECDSA P-256+)
   - Avoid deprecated algorithms (MD5, SHA-1)
   - Implement constant-time comparisons

3. **Rollback Protection**
   - Use hardware-backed monotonic counters
   - Implement version pinning
   - Store version in write-once memory

4. **Communication Security**
   - Implement CAN authentication (MAC)
   - Use encrypted firmware transfer when possible
   - Validate message sources

5. **Fault Injection Protection**
   - Verify critical operations multiple times
   - Use redundant checks
   - Implement glitch detection

## Summary

CAN bootloader security requires a comprehensive approach combining cryptographic verification, rollback protection, and secure communication protocols. Key elements include signature verification using strong algorithms like Ed25519, monotonic counters preventing version downgrades, CRC32 integrity checks, and secure storage of cryptographic keys. The bootloader operates as a state machine progressing through initialization, firmware download, verification, and installation phases. Both C/C++ and Rust implementations benefit from zero-cost abstractions while maintaining safety guarantees. Production systems should integrate hardware security features like secure elements, OTP memory, and trusted execution environments. Proper implementation protects against unauthorized firmware modifications, downgrade attacks, and code injection while maintaining automotive-grade reliability and real-time performance requirements.