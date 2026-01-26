# MQTT Topic: Secure Boot and Attestation

## Detailed Description

**Secure Boot and Attestation** is a critical security mechanism in IoT and embedded systems that ensures device integrity from the moment of power-on through runtime operation. This topic covers two fundamental security concepts:

### Secure Boot
Secure boot is a cryptographic process that validates each component in the boot chain before execution. Starting from an immutable root of trust (typically stored in hardware), each boot stage verifies the digital signature of the next stage before transferring control. This creates a chain of trust that prevents unauthorized or tampered firmware from executing on the device.

**Key components:**
- **Root of Trust (RoT)**: Immutable code/keys in hardware (OTP memory, eFuses)
- **Boot stages**: Multiple verification layers (bootloader, kernel, application)
- **Digital signatures**: Cryptographic verification using public-key infrastructure
- **Rollback protection**: Prevents downgrade attacks to vulnerable firmware versions

### Remote Attestation
Attestation allows a device to prove its identity and security state to remote parties. The device generates cryptographic evidence of its configuration, firmware version, and runtime state, which can be verified by a server or service to ensure the device hasn't been compromised.

**Key components:**
- **Device identity**: Unique cryptographic credentials per device
- **Measurement/hashing**: Recording of boot components and configuration
- **Quote generation**: Signed attestation reports
- **Trusted Execution Environments (TEE)**: Isolated execution zones (ARM TrustZone, Intel SGX)

### MQTT Integration
In MQTT-based systems, secure boot and attestation ensure that:
- Only authentic devices can connect to the broker
- Device credentials aren't compromised by malware
- Remote verification of device trustworthiness before accepting data
- Tampered devices are detected and isolated

---

## C/C++ Code Examples

### Example 1: Secure Boot Verification (Simplified)

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

// Simulated hardware root of trust
typedef struct {
    uint8_t public_key[256];
    uint32_t key_length;
    uint8_t device_id[32];
} RootOfTrust;

// Boot stage structure
typedef struct {
    const char* name;
    uint8_t* code;
    size_t code_size;
    uint8_t signature[256];
    size_t sig_length;
} BootStage;

// Verify digital signature of boot stage
int verify_boot_stage(BootStage* stage, RootOfTrust* rot) {
    EVP_PKEY* pkey = NULL;
    EVP_MD_CTX* ctx = NULL;
    int result = 0;
    
    // Load public key from root of trust
    const unsigned char* key_data = rot->public_key;
    pkey = d2i_PUBKEY(NULL, &key_data, rot->key_length);
    if (!pkey) {
        fprintf(stderr, "Failed to load public key\n");
        return 0;
    }
    
    // Create verification context
    ctx = EVP_MD_CTX_new();
    if (!ctx) goto cleanup;
    
    // Verify signature
    if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) <= 0)
        goto cleanup;
    
    if (EVP_DigestVerifyUpdate(ctx, stage->code, stage->code_size) <= 0)
        goto cleanup;
    
    result = EVP_DigestVerifyFinal(ctx, stage->signature, stage->sig_length);
    
    if (result == 1) {
        printf("[SECURE BOOT] %s verified successfully\n", stage->name);
    } else {
        fprintf(stderr, "[SECURE BOOT] %s verification FAILED!\n", stage->name);
    }
    
cleanup:
    if (ctx) EVP_MD_CTX_free(ctx);
    if (pkey) EVP_PKEY_free(pkey);
    
    return (result == 1);
}

// Secure boot chain execution
int execute_secure_boot(BootStage* stages, int num_stages, RootOfTrust* rot) {
    printf("=== Starting Secure Boot Process ===\n");
    
    for (int i = 0; i < num_stages; i++) {
        printf("Verifying stage %d: %s\n", i, stages[i].name);
        
        if (!verify_boot_stage(&stages[i], rot)) {
            fprintf(stderr, "BOOT HALTED: Stage %d failed verification\n", i);
            return 0;
        }
        
        // In real implementation, would transfer control to verified code
        printf("Stage %d execution would begin here\n", i);
    }
    
    printf("=== Secure Boot Complete ===\n");
    return 1;
}
```

### Example 2: MQTT Attestation Client

```c
#include <mosquitto.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <time.h>

typedef struct {
    uint8_t device_id[32];
    uint8_t firmware_hash[32];
    uint32_t firmware_version;
    uint64_t boot_timestamp;
    uint8_t platform_state[64];
} AttestationReport;

// Generate attestation report
void generate_attestation(AttestationReport* report, const uint8_t* firmware, 
                         size_t fw_size, const uint8_t* device_key) {
    // Hash firmware
    SHA256(firmware, fw_size, report->firmware_hash);
    
    // Set device identity
    memcpy(report->device_id, device_key, 32);
    
    // Record boot time
    report->boot_timestamp = (uint64_t)time(NULL);
    
    // Simulate platform configuration register (PCR) values
    // In real TEE, this would come from hardware
    SHA256((uint8_t*)"platform_config_data", 20, report->platform_state);
    
    printf("Attestation report generated\n");
    printf("Firmware hash: ");
    for (int i = 0; i < 8; i++) printf("%02x", report->firmware_hash[i]);
    printf("...\n");
}

// Sign attestation report
int sign_attestation(AttestationReport* report, uint8_t* signature, 
                     size_t* sig_len, EVP_PKEY* private_key) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    
    if (EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, private_key) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }
    
    if (EVP_DigestSignUpdate(ctx, report, sizeof(AttestationReport)) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }
    
    if (EVP_DigestSignFinal(ctx, signature, sig_len) <= 0) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }
    
    EVP_MD_CTX_free(ctx);
    return 1;
}

// MQTT attestation publisher
void publish_attestation(struct mosquitto* mosq, const char* device_id) {
    AttestationReport report = {0};
    uint8_t signature[512];
    size_t sig_len = sizeof(signature);
    
    // Simulated firmware data
    uint8_t firmware[] = "DEVICE_FIRMWARE_v1.2.3";
    uint8_t device_key[32] = {0x01, 0x02, 0x03}; // Simplified
    
    // Generate attestation
    generate_attestation(&report, firmware, sizeof(firmware), device_key);
    report.firmware_version = 0x00010203; // v1.2.3
    
    // In production: sign with hardware-protected key
    // sign_attestation(&report, signature, &sig_len, device_private_key);
    
    // Create attestation payload (simplified JSON-like)
    char payload[1024];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\","
             "\"firmware_version\":%u,"
             "\"boot_time\":%lu,"
             "\"firmware_hash\":\"%02x%02x%02x%02x...\"}",
             device_id,
             report.firmware_version,
             report.boot_timestamp,
             report.firmware_hash[0], report.firmware_hash[1],
             report.firmware_hash[2], report.firmware_hash[3]);
    
    // Publish to attestation topic
    char topic[128];
    snprintf(topic, sizeof(topic), "device/%s/attestation", device_id);
    
    mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 1, false);
    printf("Published attestation to %s\n", topic);
}
```

---

## Rust Code Examples

### Example 1: Secure Boot Chain

```rust
use sha2::{Sha256, Digest};
use rsa::{RsaPublicKey, pkcs1v15::{Signature, VerifyingKey}};
use rsa::signature::Verifier;

#[derive(Debug)]
pub struct RootOfTrust {
    pub public_key: RsaPublicKey,
    pub device_id: [u8; 32],
}

#[derive(Debug)]
pub struct BootStage {
    pub name: String,
    pub code: Vec<u8>,
    pub signature: Vec<u8>,
}

impl BootStage {
    /// Verify the digital signature of this boot stage
    pub fn verify(&self, root_of_trust: &RootOfTrust) -> Result<(), String> {
        // Hash the boot stage code
        let mut hasher = Sha256::new();
        hasher.update(&self.code);
        let code_hash = hasher.finalize();
        
        // Create verifying key from RoT
        let verifying_key = VerifyingKey::<Sha256>::new(root_of_trust.public_key.clone());
        
        // Parse signature
        let signature = Signature::try_from(self.signature.as_slice())
            .map_err(|e| format!("Invalid signature format: {}", e))?;
        
        // Verify signature
        verifying_key.verify(&self.code, &signature)
            .map_err(|e| format!("Signature verification failed: {}", e))?;
        
        println!("[SECURE BOOT] {} verified successfully", self.name);
        Ok(())
    }
}

pub struct SecureBoot {
    stages: Vec<BootStage>,
    root_of_trust: RootOfTrust,
}

impl SecureBoot {
    pub fn new(root_of_trust: RootOfTrust) -> Self {
        Self {
            stages: Vec::new(),
            root_of_trust,
        }
    }
    
    pub fn add_stage(&mut self, stage: BootStage) {
        self.stages.push(stage);
    }
    
    /// Execute the complete secure boot chain
    pub fn execute(&self) -> Result<(), String> {
        println!("=== Starting Secure Boot Process ===");
        
        for (idx, stage) in self.stages.iter().enumerate() {
            println!("Verifying stage {}: {}", idx, stage.name);
            
            stage.verify(&self.root_of_trust)
                .map_err(|e| format!("BOOT HALTED at stage {}: {}", idx, e))?;
            
            // In real implementation, transfer control to verified code
            println!("Stage {} execution would begin here", idx);
        }
        
        println!("=== Secure Boot Complete ===");
        Ok(())
    }
    
    /// Calculate boot measurement (aggregate hash of all stages)
    pub fn calculate_boot_measurement(&self) -> [u8; 32] {
        let mut hasher = Sha256::new();
        
        for stage in &self.stages {
            hasher.update(&stage.code);
        }
        
        hasher.finalize().into()
    }
}
```

### Example 2: MQTT Attestation with Rumqttc

```rust
use rumqttc::{MqttOptions, Client, QoS};
use serde::{Serialize, Deserialize};
use sha2::{Sha256, Digest};
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, Serialize, Deserialize)]
pub struct AttestationReport {
    pub device_id: String,
    pub firmware_version: String,
    pub firmware_hash: String,
    pub boot_timestamp: u64,
    pub platform_state: String,
    pub tee_type: Option<String>, // e.g., "TrustZone", "SGX"
}

impl AttestationReport {
    /// Generate attestation report for current device state
    pub fn generate(device_id: &str, firmware: &[u8]) -> Self {
        // Hash firmware
        let mut hasher = Sha256::new();
        hasher.update(firmware);
        let firmware_hash = hex::encode(hasher.finalize());
        
        // Get boot timestamp
        let boot_timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        // Simulate platform configuration state
        let mut platform_hasher = Sha256::new();
        platform_hasher.update(b"platform_config_data");
        let platform_state = hex::encode(platform_hasher.finalize());
        
        Self {
            device_id: device_id.to_string(),
            firmware_version: "1.2.3".to_string(),
            firmware_hash,
            boot_timestamp,
            platform_state,
            tee_type: Some("TrustZone".to_string()),
        }
    }
    
    /// Sign the attestation report (simplified)
    pub fn sign(&self) -> Vec<u8> {
        // In production: use hardware-protected key in TEE
        let serialized = serde_json::to_string(self).unwrap();
        let mut hasher = Sha256::new();
        hasher.update(serialized.as_bytes());
        hasher.finalize().to_vec()
    }
}

pub struct AttestationClient {
    mqtt_client: Client,
    device_id: String,
}

impl AttestationClient {
    pub fn new(broker: &str, port: u16, device_id: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let mut mqtt_options = MqttOptions::new(device_id, broker, port);
        mqtt_options.set_keep_alive(std::time::Duration::from_secs(30));
        
        let (mqtt_client, _connection) = Client::new(mqtt_options, 10);
        
        Ok(Self {
            mqtt_client,
            device_id: device_id.to_string(),
        })
    }
    
    /// Publish attestation report to MQTT broker
    pub fn publish_attestation(&self, firmware: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        // Generate attestation report
        let report = AttestationReport::generate(&self.device_id, firmware);
        
        println!("Generated attestation report:");
        println!("  Firmware hash: {}", &report.firmware_hash[..16]);
        println!("  Boot time: {}", report.boot_timestamp);
        println!("  TEE: {:?}", report.tee_type);
        
        // Sign report
        let signature = report.sign();
        
        // Create payload with report and signature
        #[derive(Serialize)]
        struct AttestationPayload {
            report: AttestationReport,
            signature: String,
        }
        
        let payload = AttestationPayload {
            report,
            signature: hex::encode(signature),
        };
        
        let json_payload = serde_json::to_string(&payload)?;
        
        // Publish to device-specific attestation topic
        let topic = format!("device/{}/attestation", self.device_id);
        self.mqtt_client.publish(&topic, QoS::AtLeastOnce, false, json_payload)?;
        
        println!("Published attestation to {}", topic);
        Ok(())
    }
    
    /// Subscribe to attestation challenge requests
    pub fn subscribe_to_challenges(&self) -> Result<(), Box<dyn std::error::Error>> {
        let topic = format!("device/{}/attestation/challenge", self.device_id);
        self.mqtt_client.subscribe(&topic, QoS::AtLeastOnce)?;
        println!("Subscribed to attestation challenges on {}", topic);
        Ok(())
    }
}

// Example usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = AttestationClient::new("localhost", 1883, "secure-device-001")?;
    
    // Simulated firmware binary
    let firmware = b"DEVICE_FIRMWARE_BINARY_v1.2.3";
    
    // Publish attestation
    client.publish_attestation(firmware)?;
    
    // Subscribe to remote attestation challenges
    client.subscribe_to_challenges()?;
    
    Ok(())
}
```

---

## Summary

**Secure Boot and Attestation** forms the foundation of trusted IoT device operation by establishing cryptographic proof that devices are running authentic, unmodified firmware and maintaining their security posture throughout operation.

**Key Takeaways:**

- **Secure Boot** creates a hardware-rooted chain of trust that validates each boot component cryptographically before execution, preventing malware and unauthorized firmware from running
- **Remote Attestation** enables devices to prove their identity and security state to remote parties, essential for zero-trust IoT architectures
- **MQTT Integration** allows attestation reports to be published to brokers for centralized verification, enabling fleet-wide security monitoring
- **Trusted Execution Environments (TEEs)** like ARM TrustZone and Intel SGX provide isolated execution zones that protect cryptographic keys and sensitive attestation processes
- Implementation requires cryptographic primitives (RSA/ECC signatures, SHA-256 hashing), hardware security features (OTP memory, secure storage), and careful key management

This security mechanism is critical for high-value IoT deployments in industrial control, medical devices, automotive systems, and any scenario where device compromise could have serious consequences. The combination of secure boot and continuous attestation provides defense-in-depth against firmware tampering, malware injection, and device impersonation attacks.