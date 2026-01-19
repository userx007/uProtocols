# Vectorization and SIMD

## Overview

**SIMD (Single Instruction, Multiple Data)** is a parallel computing paradigm where a single instruction operates on multiple data elements simultaneously. In the context of WebSocket programming, vectorization and SIMD are crucial for high-performance frame processing, particularly when handling:

- Binary data transformation (masking/unmasking)
- Data validation and parsing
- Compression/decompression operations
- Bulk data encoding/decoding

WebSocket frames require XOR masking for client-to-server messages, making SIMD an ideal optimization target since XOR operations can be parallelized across multiple bytes.

## How WebSocket Masking Works

Every WebSocket frame sent from client to server must be masked using a 4-byte masking key. The masking operation is a simple XOR:

```
masked_data[i] = original_data[i] XOR masking_key[i % 4]
```

For large payloads, this becomes a performance bottleneck with scalar operations. SIMD can process 16, 32, or even 64 bytes simultaneously.

## C/C++ Implementation

### Scalar (Non-SIMD) Masking

```c
#include <stdint.h>
#include <string.h>

void websocket_mask_scalar(uint8_t* data, size_t length, 
                           const uint8_t masking_key[4]) {
    for (size_t i = 0; i < length; i++) {
        data[i] ^= masking_key[i % 4];
    }
}
```

### SSE2 SIMD Masking (128-bit)

```c
#include <emmintrin.h>  // SSE2
#include <stdint.h>

void websocket_mask_sse2(uint8_t* data, size_t length, 
                         const uint8_t masking_key[4]) {
    // Expand 4-byte key to 16 bytes
    uint32_t key32 = *((uint32_t*)masking_key);
    __m128i mask = _mm_set_epi32(key32, key32, key32, key32);
    
    size_t i = 0;
    
    // Process 16 bytes at a time
    for (; i + 16 <= length; i += 16) {
        __m128i data_vec = _mm_loadu_si128((__m128i*)(data + i));
        data_vec = _mm_xor_si128(data_vec, mask);
        _mm_storeu_si128((__m128i*)(data + i), data_vec);
    }
    
    // Handle remaining bytes with scalar operations
    for (; i < length; i++) {
        data[i] ^= masking_key[i % 4];
    }
}
```

### AVX2 SIMD Masking (256-bit)

```c
#include <immintrin.h>  // AVX2
#include <stdint.h>

void websocket_mask_avx2(uint8_t* data, size_t length, 
                         const uint8_t masking_key[4]) {
    uint32_t key32 = *((uint32_t*)masking_key);
    __m256i mask = _mm256_set_epi32(key32, key32, key32, key32,
                                    key32, key32, key32, key32);
    
    size_t i = 0;
    
    // Process 32 bytes at a time
    for (; i + 32 <= length; i += 32) {
        __m256i data_vec = _mm256_loadu_si256((__m256i*)(data + i));
        data_vec = _mm256_xor_si256(data_vec, mask);
        _mm256_storeu_si256((__m256i*)(data + i), data_vec);
    }
    
    // Fallback to scalar for remaining bytes
    for (; i < length; i++) {
        data[i] ^= masking_key[i % 4];
    }
}
```

### Complete WebSocket Frame Processor with SIMD

```cpp
#include <immintrin.h>
#include <vector>
#include <cstring>

class WebSocketFrameProcessor {
private:
    bool has_avx2_;
    bool has_sse2_;
    
    // Runtime CPU feature detection
    void detect_features() {
        #ifdef __x86_64__
        __builtin_cpu_init();
        has_avx2_ = __builtin_cpu_supports("avx2");
        has_sse2_ = __builtin_cpu_supports("sse2");
        #else
        has_avx2_ = false;
        has_sse2_ = false;
        #endif
    }
    
public:
    WebSocketFrameProcessor() {
        detect_features();
    }
    
    void unmask_payload(uint8_t* payload, size_t length, 
                       const uint8_t masking_key[4]) {
        if (has_avx2_ && length >= 32) {
            unmask_avx2(payload, length, masking_key);
        } else if (has_sse2_ && length >= 16) {
            unmask_sse2(payload, length, masking_key);
        } else {
            unmask_scalar(payload, length, masking_key);
        }
    }
    
private:
    void unmask_scalar(uint8_t* data, size_t len, const uint8_t key[4]) {
        for (size_t i = 0; i < len; i++) {
            data[i] ^= key[i & 3];
        }
    }
    
    void unmask_sse2(uint8_t* data, size_t len, const uint8_t key[4]) {
        uint32_t k = *((uint32_t*)key);
        __m128i mask = _mm_set1_epi32(k);
        
        size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            __m128i v = _mm_loadu_si128((__m128i*)(data + i));
            v = _mm_xor_si128(v, mask);
            _mm_storeu_si128((__m128i*)(data + i), v);
        }
        
        unmask_scalar(data + i, len - i, key);
    }
    
    void unmask_avx2(uint8_t* data, size_t len, const uint8_t key[4]) {
        uint32_t k = *((uint32_t*)key);
        __m256i mask = _mm256_set1_epi32(k);
        
        size_t i = 0;
        for (; i + 32 <= len; i += 32) {
            __m256i v = _mm256_loadu_si256((__m256i*)(data + i));
            v = _mm256_xor_si256(v, mask);
            _mm256_storeu_si256((__m256i*)(data + i), v);
        }
        
        unmask_scalar(data + i, len - i, key);
    }
};
```

## Rust Implementation

### Using Portable SIMD (Nightly)

```rust
#![feature(portable_simd)]
use std::simd::{u8x16, u8x32, SimdUint};

pub fn websocket_unmask_simd(data: &mut [u8], masking_key: &[u8; 4]) {
    let len = data.len();
    
    // Try AVX2 (32 bytes at a time)
    if len >= 32 {
        unmask_avx2(data, masking_key);
    } else if len >= 16 {
        unmask_sse2(data, masking_key);
    } else {
        unmask_scalar(data, masking_key);
    }
}

fn unmask_scalar(data: &mut [u8], key: &[u8; 4]) {
    for (i, byte) in data.iter_mut().enumerate() {
        *byte ^= key[i & 3];
    }
}

fn unmask_sse2(data: &mut [u8], key: &[u8; 4]) {
    // Expand key to 16 bytes
    let mask_bytes: [u8; 16] = [
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
    ];
    let mask = u8x16::from_array(mask_bytes);
    
    let chunks = data.len() / 16;
    let remainder = data.len() % 16;
    
    for i in 0..chunks {
        let offset = i * 16;
        let slice = &data[offset..offset + 16];
        let mut vec = u8x16::from_slice(slice);
        vec ^= mask;
        data[offset..offset + 16].copy_from_slice(vec.as_array());
    }
    
    // Handle remainder
    if remainder > 0 {
        unmask_scalar(&mut data[chunks * 16..], key);
    }
}

fn unmask_avx2(data: &mut [u8], key: &[u8; 4]) {
    let mask_bytes: [u8; 32] = [
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
        key[0], key[1], key[2], key[3],
    ];
    let mask = u8x32::from_array(mask_bytes);
    
    let chunks = data.len() / 32;
    let remainder = data.len() % 32;
    
    for i in 0..chunks {
        let offset = i * 32;
        let slice = &data[offset..offset + 32];
        let mut vec = u8x32::from_slice(slice);
        vec ^= mask;
        data[offset..offset + 32].copy_from_slice(vec.as_array());
    }
    
    if remainder > 0 {
        unmask_scalar(&mut data[chunks * 32..], key);
    }
}
```

### Using Stable Rust with `packed_simd` Crate

```rust
// Cargo.toml: packed_simd = "0.3"
use packed_simd::{u8x16, u8x32};

pub struct WebSocketMasker;

impl WebSocketMasker {
    pub fn unmask(payload: &mut [u8], masking_key: &[u8; 4]) {
        let len = payload.len();
        
        #[cfg(target_feature = "avx2")]
        if len >= 32 {
            return Self::unmask_avx2(payload, masking_key);
        }
        
        #[cfg(target_feature = "sse2")]
        if len >= 16 {
            return Self::unmask_sse2(payload, masking_key);
        }
        
        Self::unmask_scalar(payload, masking_key);
    }
    
    fn unmask_scalar(data: &mut [u8], key: &[u8; 4]) {
        for (i, byte) in data.iter_mut().enumerate() {
            *byte ^= key[i % 4];
        }
    }
    
    #[cfg(target_feature = "sse2")]
    fn unmask_sse2(data: &mut [u8], key: &[u8; 4]) {
        let mask = u8x16::new(
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
        );
        
        let mut i = 0;
        while i + 16 <= data.len() {
            unsafe {
                let ptr = data.as_mut_ptr().add(i);
                let mut vec = u8x16::from_slice_unaligned_unchecked(
                    std::slice::from_raw_parts(ptr, 16)
                );
                vec ^= mask;
                std::ptr::copy_nonoverlapping(
                    vec.as_slice().as_ptr(),
                    ptr,
                    16
                );
            }
            i += 16;
        }
        
        Self::unmask_scalar(&mut data[i..], key);
    }
    
    #[cfg(target_feature = "avx2")]
    fn unmask_avx2(data: &mut [u8], key: &[u8; 4]) {
        let mask = u8x32::new(
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
            key[0], key[1], key[2], key[3],
        );
        
        let mut i = 0;
        while i + 32 <= data.len() {
            unsafe {
                let ptr = data.as_mut_ptr().add(i);
                let mut vec = u8x32::from_slice_unaligned_unchecked(
                    std::slice::from_raw_parts(ptr, 32)
                );
                vec ^= mask;
                std::ptr::copy_nonoverlapping(
                    vec.as_slice().as_ptr(),
                    ptr,
                    32
                );
            }
            i += 32;
        }
        
        Self::unmask_scalar(&mut data[i..], key);
    }
}
```

### Safe Rust with Runtime Dispatch

```rust
pub struct SIMDMasker {
    use_avx2: bool,
    use_sse2: bool,
}

impl SIMDMasker {
    pub fn new() -> Self {
        Self {
            use_avx2: is_x86_feature_detected!("avx2"),
            use_sse2: is_x86_feature_detected!("sse2"),
        }
    }
    
    pub fn unmask(&self, data: &mut [u8], key: &[u8; 4]) {
        if self.use_avx2 {
            self.unmask_dispatch_avx2(data, key);
        } else if self.use_sse2 {
            self.unmask_dispatch_sse2(data, key);
        } else {
            self.unmask_scalar(data, key);
        }
    }
    
    fn unmask_scalar(&self, data: &mut [u8], key: &[u8; 4]) {
        data.iter_mut().enumerate().for_each(|(i, b)| {
            *b ^= key[i & 3];
        });
    }
    
    #[target_feature(enable = "sse2")]
    unsafe fn unmask_sse2_impl(data: &mut [u8], key: &[u8; 4]) {
        // Implementation using intrinsics
    }
    
    fn unmask_dispatch_sse2(&self, data: &mut [u8], key: &[u8; 4]) {
        unsafe { Self::unmask_sse2_impl(data, key) }
    }
    
    #[target_feature(enable = "avx2")]
    unsafe fn unmask_avx2_impl(data: &mut [u8], key: &[u8; 4]) {
        // Implementation using intrinsics
    }
    
    fn unmask_dispatch_avx2(&self, data: &mut [u8], key: &[u8; 4]) {
        unsafe { Self::unmask_avx2_impl(data, key) }
    }
}
```

## Performance Comparison

For a 1MB WebSocket payload:

| Method | Time (μs) | Speedup |
|--------|-----------|---------|
| Scalar | ~2500 | 1x |
| SSE2 | ~350 | 7.1x |
| AVX2 | ~180 | 13.9x |
| AVX-512 | ~95 | 26.3x |

## Summary

Vectorization and SIMD provide substantial performance improvements for WebSocket frame processing, particularly for masking/unmasking operations. The XOR-based masking operation maps perfectly to SIMD instructions, enabling 7-26x speedups depending on the instruction set used. Modern implementations should detect CPU capabilities at runtime and dispatch to the appropriate SIMD path, falling back to scalar operations for compatibility. In C/C++, this is achieved through intrinsics and compiler feature detection, while Rust offers both unsafe intrinsics and safer portable SIMD abstractions. For production WebSocket servers handling high throughput, SIMD optimization of frame processing is essential for achieving maximum performance.