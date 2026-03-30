# 81. Machine Learning on CAN Data


**Conceptual foundations** — CAN data properties that matter for ML (IAT periodicity, signal dimensionality, class imbalance, the importance of DBC decoding before feature extraction).

**Three application domains** with dedicated method comparisons:
- *Anomaly detection* — Mahalanobis distance, Isolation Forest, LSTM Autoencoder, One-Class SVM, with a decision table by resource constraint
- *Predictive maintenance* — RUL formulation, per-component signal selection (battery, brakes, drivetrain), label generation strategies including physics-informed proxies
- *Pattern recognition* — driving behaviour classification, operating mode detection, and raw-frame signal fingerprinting for intrusion detection

**C/C++ code examples:**
1. Templated circular buffer with sliding-window statistics (IAT, variance, derivatives)
2. Quantised Isolation Forest inference — flat array trees suitable for flash storage
3. Minimal LSTM Autoencoder inference with Padé tanh approximation for embedded use
4. Full SocketCAN receiver that wires feature extraction → anomaly scoring → alerting

**Rust code examples:**
1. `const`-compatible circular buffer with iterator-based feature computation
2. Isolation Forest with `Option<usize>` child pointers and expected path length formula
3. Per-ID pipeline struct with byte entropy and IAT-based scoring
4. Gradient Boosted Tree inference engine for RUL regression with severity classification
5. Integration harness simulating normal traffic then injecting an anomalous frame

> Applying anomaly detection, predictive maintenance, and pattern recognition to CAN traffic analysis.

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN Data Characteristics for ML](#can-data-characteristics-for-ml)
3. [Feature Engineering from CAN Frames](#feature-engineering-from-can-frames)
4. [Anomaly Detection](#anomaly-detection)
5. [Predictive Maintenance](#predictive-maintenance)
6. [Pattern Recognition and Classification](#pattern-recognition-and-classification)
7. [Online / Real-Time Inference on Embedded Systems](#online--real-time-inference-on-embedded-systems)
8. [Implementation in C/C++](#implementation-in-cc)
9. [Implementation in Rust](#implementation-in-rust)
10. [Pipeline Overview](#pipeline-overview)
11. [Summary](#summary)

---

## Introduction

Controller Area Network (CAN) bus is the backbone communication protocol used in virtually every modern vehicle and many industrial systems. Each ECU (Electronic Control Unit) broadcasts structured frames at deterministic intervals, producing a rich, high-frequency stream of sensor readings, control commands, and system states.

Applying Machine Learning (ML) to this data stream unlocks three major capabilities:

| Capability | Description | Example Use Case |
|---|---|---|
| **Anomaly Detection** | Identify frames or signals that deviate from learned normal behaviour | Intrusion detection, fault identification |
| **Predictive Maintenance** | Forecast component degradation before failure occurs | Battery SoH prediction, brake wear |
| **Pattern Recognition** | Classify driving behaviour, operating modes, or signal signatures | Driver profiling, mode detection |

Unlike traditional threshold-based monitoring, ML models can capture complex, non-linear relationships between hundreds of simultaneous signals — making them far more robust in practice.

---

## CAN Data Characteristics for ML

Before designing any model, it is essential to understand the properties of CAN data that distinguish it from typical tabular or image datasets.

### Structural Properties

- **Message ID (Arbitration ID):** 11-bit (standard) or 29-bit (extended). Acts as a topic identifier.
- **DLC (Data Length Code):** 0–8 bytes of payload per frame.
- **Payload:** Raw bytes that must be decoded via a DBC (Database CAN) file into physical signal values (e.g., engine RPM, throttle position).
- **Timestamps:** Microsecond-resolution hardware timestamps; timing regularity is itself a feature.
- **Bus Load:** Typically 30–70% utilisation; the arrival rate of specific IDs is stable under normal conditions.

### Statistical Properties

- **High dimensionality:** A modern vehicle may have 50–200 unique message IDs, each carrying 1–16 decoded signals — yielding hundreds of simultaneous time series.
- **Mixed update rates:** Some signals update at 1 ms (e.g., ABS wheel speed), others at 100 ms (e.g., HVAC status).
- **Temporal dependencies:** Signals are correlated in time; RPM follows throttle with a characteristic lag.
- **Class imbalance:** Anomalous frames are extremely rare relative to normal traffic.

### Key Insight

> Raw CAN bytes are meaningless without a DBC. ML pipelines should operate on **decoded physical values** wherever possible, or treat raw byte patterns as features only for intrusion-detection tasks where the attacker may inject syntactically valid but semantically abnormal frames.

---

## Feature Engineering from CAN Frames

Good feature engineering is often more impactful than model selection for CAN ML tasks.

### Time-Domain Features per Signal Window

For each signal `s` over a sliding window of `N` samples:

- **Statistical:** mean, variance, min, max, range, skewness, kurtosis
- **Temporal:** rate of change (first derivative), zero-crossing rate, autocorrelation at lag-1
- **Frequency:** dominant FFT frequency, spectral energy in defined bands

### Inter-Signal Features

- Pearson / Spearman correlation between pairs of signals
- Ratio features (e.g., fuel consumption / engine load)
- Lead-lag correlation: does signal A precede signal B?

### CAN-Specific Features (for intrusion detection on raw frames)

- **Inter-arrival time (IAT):** time between consecutive frames of the same ID
- **IAT variance:** an injected frame breaks the periodic timing
- **Payload byte entropy:** random-looking bytes indicate spoofing
- **Hamming distance** from the previous frame of the same ID

---

## Anomaly Detection

### Approach 1 — Statistical Baseline (Z-Score / Mahalanobis Distance)

The simplest approach: model normal behaviour with a multivariate Gaussian and flag frames whose Mahalanobis distance exceeds a threshold `τ`.

```
D²(x) = (x - μ)ᵀ Σ⁻¹ (x - μ)
```

Works well for slow-drifting sensor anomalies. Fails for correlated, non-Gaussian signals.

### Approach 2 — Isolation Forest

Isolation Forest partitions feature space with random splits. Anomalies are isolated in fewer splits (shorter path length). No distance metric required; robust to high dimensionality.

Typical workflow:
1. Train on normal driving data (unsupervised).
2. Score each window; alert when score < −0.5.
3. Retrain periodically as vehicle ages.

### Approach 3 — LSTM Autoencoder

An LSTM encoder compresses a multi-variate time series window to a latent vector; the decoder reconstructs it. High reconstruction error signals an anomaly.

```
Input window → [LSTM Encoder] → latent z → [LSTM Decoder] → reconstructed window
Anomaly score = MSE(input, reconstruction)
```

This captures temporal structure that flat-feature models miss. Requires GPU or optimised inference for real-time use.

### Approach 4 — One-Class SVM

Trains a hypersphere around normal data in feature space. Any point outside the sphere (with some margin ν) is anomalous. Effective for small datasets; scales poorly.

### Choosing an Approach

| Constraint | Recommended Approach |
|---|---|
| ECU with < 64 KB RAM | Statistical (Z-score), pre-computed thresholds |
| Gateway ECU with RTOS | Isolation Forest (quantised, small tree count) |
| Offline log analysis | LSTM Autoencoder |
| Few labelled anomalies | One-Class SVM or Isolation Forest |

---

## Predictive Maintenance

### Problem Formulation

Predictive maintenance (PdM) predicts **remaining useful life (RUL)** or the **probability of failure** of a component within a future horizon `H`.

```
P(failure within H hours | signal history) = f(features)
```

### Signal Selection for Common Components

**Battery (HV/12V):**
- Voltage sag under load, internal resistance (estimated from V/I), temperature
- SoC vs charge acceptance, charge cycle count

**Brakes:**
- Brake pressure vs deceleration ratio
- ABS activation frequency
- Estimated disc temperature from friction model

**Engine/Transmission:**
- Oil pressure under load, coolant temperature rise rate
- Gear shift quality (jerk signature), clutch slip

### Model Choices

- **Gradient Boosted Trees (XGBoost/LightGBM):** Excellent for tabular features with good calibration. Deployable as lookup tables on resource-constrained ECUs.
- **Random Forest Regressor:** Robust baseline; inherent feature importance for explainability.
- **LSTM / GRU:** Best for sequence-to-scalar RUL regression when raw signal history matters.

### Label Generation

Since ground-truth failure labels are rare, common strategies include:

- **Degradation proxy labels:** Use declining performance metrics (e.g., starter motor crank time increasing) as soft labels.
- **End-of-life labelling:** Work backwards from known failure events; label windows `t` steps before failure with decreasing RUL values.
- **Physics-informed features:** Use domain models (Peukert's equation for batteries, Archard's wear model for friction components) to generate synthetic degradation curves for training.

---

## Pattern Recognition and Classification

### Driving Behaviour Classification

Multi-class classification from a window of CAN signals:

| Class | Key Signals |
|---|---|
| Aggressive | High throttle gradient, hard braking events, high lateral g |
| Eco | Gentle throttle, early up-shifts, low RPM cruise |
| Highway Cruise | Steady high speed, ACC engaged, minimal steering |
| Urban Stop-Go | Frequent braking, low speeds, high idle time |

A lightweight Random Forest or SVM on window-level features is typically sufficient. Deep learning (1D-CNN) gives marginal gains but requires significantly more compute.

### Operating Mode Detection

Detecting whether a vehicle is in: normal operation, regenerative braking, limp-home mode, cold start, trailer tow mode, etc.

This is a multi-label problem since modes overlap. Useful for: data segmentation before anomaly detection, maintenance scheduling, and charging strategy.

### Signal Fingerprinting (Intrusion Detection)

Even without DBC decoding, the raw payload byte patterns of each CAN ID carry a statistical fingerprint. A naïve Bayes or neural network classifier trained per-ID learns the normal byte distribution. Frames that deviate (injected by an attacker) are flagged.

---

## Online / Real-Time Inference on Embedded Systems

Deploying ML models inside a vehicle requires meeting hard latency and memory budgets.

### Quantisation and Compression

- **Fixed-point arithmetic:** Convert float32 weights to int8. Typically < 1% accuracy loss.
- **Tree ensemble serialisation:** Flatten decision trees to arrays of thresholds and leaf values for cache-friendly traversal.
- **Lookup tables:** Pre-compute model outputs over the discretised input space for the simplest models.

### Sliding Window Management

Maintain a circular buffer per signal. On each new CAN frame, update the buffer, recompute only the changed features, and run inference.

```
[Frame arrives] → update circular buffer → delta-update features → inference → alert?
```

### Latency Targets

| Task | Acceptable Latency |
|---|---|
| Intrusion detection | < 1 ms per frame |
| Fault detection | < 10 ms per window |
| Predictive maintenance | < 100 ms per trip segment |
| Driving behaviour | < 500 ms per window |

---

## Implementation in C/C++

The following C/C++ examples demonstrate the core building blocks for CAN-based ML inference on embedded or gateway ECUs.

### 1. Feature Extraction — Sliding Window Statistics

```cpp
// feature_extractor.hpp
#pragma once
#include <cstdint>
#include <cmath>
#include <array>
#include <numeric>
#include <algorithm>

constexpr size_t WINDOW_SIZE = 64;

struct SignalFeatures {
    float mean;
    float variance;
    float min_val;
    float max_val;
    float range;
    float mean_derivative;   // mean |x[i] - x[i-1]|
    float iat_mean;          // inter-arrival time mean (ms)
    float iat_variance;
};

/// Circular buffer for a single CAN signal
template<size_t N>
class CircularBuffer {
public:
    void push(float value, uint64_t timestamp_us) {
        samples_[head_] = value;
        timestamps_[head_] = timestamp_us;
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }

    bool full() const { return count_ == N; }

    // Fill out array with oldest-first ordering
    void ordered_samples(std::array<float, N>& out) const {
        size_t start = full() ? head_ : 0;
        for (size_t i = 0; i < count_; ++i)
            out[i] = samples_[(start + i) % N];
    }

    void ordered_timestamps(std::array<uint64_t, N>& out) const {
        size_t start = full() ? head_ : 0;
        for (size_t i = 0; i < count_; ++i)
            out[i] = timestamps_[(start + i) % N];
    }

    size_t count() const { return count_; }

private:
    std::array<float, N>      samples_{};
    std::array<uint64_t, N>   timestamps_{};
    size_t head_ = 0;
    size_t count_ = 0;
};

/// Compute statistical features over a window of N samples
template<size_t N>
SignalFeatures extract_features(const CircularBuffer<N>& buf) {
    std::array<float, N> s{};
    std::array<uint64_t, N> t{};
    buf.ordered_samples(s);
    buf.ordered_timestamps(t);

    const size_t n = buf.count();

    // Mean
    float sum = 0.f;
    for (size_t i = 0; i < n; ++i) sum += s[i];
    float mean = sum / static_cast<float>(n);

    // Variance, min, max
    float var_acc = 0.f, mn = s[0], mx = s[0];
    for (size_t i = 0; i < n; ++i) {
        float d = s[i] - mean;
        var_acc += d * d;
        if (s[i] < mn) mn = s[i];
        if (s[i] > mx) mx = s[i];
    }

    // Mean absolute derivative
    float deriv_sum = 0.f;
    for (size_t i = 1; i < n; ++i)
        deriv_sum += std::fabs(s[i] - s[i-1]);

    // Inter-arrival time statistics (in ms)
    float iat_sum = 0.f, iat_sq_sum = 0.f;
    for (size_t i = 1; i < n; ++i) {
        float iat = static_cast<float>(t[i] - t[i-1]) / 1000.f; // us -> ms
        iat_sum += iat;
        iat_sq_sum += iat * iat;
    }
    float iat_n = static_cast<float>(n - 1);
    float iat_mean = (n > 1) ? iat_sum / iat_n : 0.f;
    float iat_var  = (n > 1) ? (iat_sq_sum / iat_n) - (iat_mean * iat_mean) : 0.f;

    return SignalFeatures{
        .mean           = mean,
        .variance       = var_acc / static_cast<float>(n),
        .min_val        = mn,
        .max_val        = mx,
        .range          = mx - mn,
        .mean_derivative = (n > 1) ? deriv_sum / static_cast<float>(n-1) : 0.f,
        .iat_mean       = iat_mean,
        .iat_variance   = iat_var
    };
}
```

---

### 2. Isolation Forest Inference (Quantised, Embedded-Friendly)

```cpp
// isolation_forest.hpp
#pragma once
#include <cstdint>
#include <array>
#include <cmath>

/// A single split node in an isolation tree.
/// Trained offline; serialised to flash as a constant array.
struct IsolationNode {
    uint8_t  feature_idx;    // which feature dimension to split on
    float    threshold;      // split value
    int16_t  left_child;     // index into node array; -1 = leaf
    int16_t  right_child;
    uint8_t  leaf_depth;     // depth at leaf (0 for non-leaf)
};

/// Compute path length for a single sample through one tree.
template<size_t MAX_NODES>
float path_length(const std::array<IsolationNode, MAX_NODES>& tree,
                  const float* features, size_t n_nodes)
{
    int16_t node_idx = 0;
    float depth = 0.f;

    while (node_idx >= 0) {
        const auto& node = tree[static_cast<size_t>(node_idx)];
        if (node.left_child == -1) {
            // Leaf: add expected path length correction c(n_leaf)
            // c(n) ~ 2 * (ln(n-1) + 0.5772) - 2*(n-1)/n  for n > 1
            depth += static_cast<float>(node.leaf_depth);
            break;
        }
        ++depth;
        if (features[node.feature_idx] <= node.threshold)
            node_idx = node.left_child;
        else
            node_idx = node.right_child;
    }
    return depth;
}

/// Anomaly score from an ensemble of trees.
/// Returns value in (0, 1); score > 0.6 is anomalous.
template<size_t N_TREES, size_t MAX_NODES, size_t N_FEATURES>
float anomaly_score(
    const std::array<std::array<IsolationNode, MAX_NODES>, N_TREES>& forest,
    const std::array<size_t, N_TREES>& tree_sizes,
    const std::array<float, N_FEATURES>& features,
    float normalisation_factor)   // c(n_training_samples)
{
    float mean_depth = 0.f;
    for (size_t t = 0; t < N_TREES; ++t)
        mean_depth += path_length(forest[t], features.data(), tree_sizes[t]);
    mean_depth /= static_cast<float>(N_TREES);

    // Anomaly score: 2^(-mean_depth / c)
    return std::pow(2.f, -mean_depth / normalisation_factor);
}
```

---

### 3. LSTM Autoencoder — Reconstruction Error (Inference Only)

```cpp
// lstm_autoencoder_inference.cpp
// Minimal single-layer LSTM inference for CAN anomaly detection.
// Weights are pre-trained offline and loaded from flash/EEPROM.

#include <cmath>
#include <array>
#include <cstring>

constexpr int INPUT_DIM  = 8;   // number of decoded CAN signals
constexpr int HIDDEN_DIM = 16;  // LSTM hidden units (keep small for ECU)
constexpr int SEQ_LEN    = 32;  // window length

struct LSTMWeights {
    // Fused weight matrices [W_i | W_f | W_g | W_o] shaped (4*H, I+H)
    float Wfused[(4*HIDDEN_DIM) * (INPUT_DIM + HIDDEN_DIM)];
    float bfused[4*HIDDEN_DIM];
};

// Sigmoid and tanh approximations (fast for embedded)
inline float sigmoid(float x) {
    return 1.f / (1.f + std::exp(-x));
}

inline float fast_tanh(float x) {
    // Pade approximation, accurate for |x| < 4
    if (x >  4.f) return  1.f;
    if (x < -4.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

/// Run one LSTM cell step.
void lstm_step(
    const float* x,          // input[INPUT_DIM]
    float* h,                 // hidden state[HIDDEN_DIM], updated in place
    float* c,                 // cell state[HIDDEN_DIM], updated in place
    const LSTMWeights& W)
{
    float gates[4 * HIDDEN_DIM] = {};

    // Concatenate [x, h] and compute W * [x,h] + b
    float xh[INPUT_DIM + HIDDEN_DIM];
    std::memcpy(xh, x, INPUT_DIM * sizeof(float));
    std::memcpy(xh + INPUT_DIM, h, HIDDEN_DIM * sizeof(float));

    int cols = INPUT_DIM + HIDDEN_DIM;
    for (int row = 0; row < 4 * HIDDEN_DIM; ++row) {
        float acc = W.bfused[row];
        for (int col = 0; col < cols; ++col)
            acc += W.Wfused[row * cols + col] * xh[col];
        gates[row] = acc;
    }

    // Apply gate activations
    for (int i = 0; i < HIDDEN_DIM; ++i) {
        float ig = sigmoid(gates[i]);                        // input gate
        float fg = sigmoid(gates[HIDDEN_DIM + i]);           // forget gate
        float gg = fast_tanh(gates[2*HIDDEN_DIM + i]);       // cell gate
        float og = sigmoid(gates[3*HIDDEN_DIM + i]);         // output gate
        c[i] = fg * c[i] + ig * gg;
        h[i] = og * fast_tanh(c[i]);
    }
}

/// Compute reconstruction MSE for anomaly scoring.
float reconstruction_error(
    const float sequence[SEQ_LEN][INPUT_DIM],
    const LSTMWeights& encoder_W,
    const LSTMWeights& decoder_W,
    const float decoder_output_W[INPUT_DIM * HIDDEN_DIM],
    const float decoder_output_b[INPUT_DIM])
{
    float h[HIDDEN_DIM] = {}, c[HIDDEN_DIM] = {};

    // --- Encode ---
    for (int t = 0; t < SEQ_LEN; ++t)
        lstm_step(sequence[t], h, c, encoder_W);

    // Latent vector is h after encoding. Decode by repeating h as input.
    float reconstructed[SEQ_LEN][INPUT_DIM] = {};
    float dh[HIDDEN_DIM], dc[HIDDEN_DIM];
    std::memcpy(dh, h, sizeof(h));
    std::memcpy(dc, c, sizeof(c));

    float prev_output[INPUT_DIM] = {};
    // --- Decode ---
    for (int t = 0; t < SEQ_LEN; ++t) {
        lstm_step(prev_output, dh, dc, decoder_W);
        // Linear projection: output = decoder_output_W * dh + b
        for (int j = 0; j < INPUT_DIM; ++j) {
            float val = decoder_output_b[j];
            for (int k = 0; k < HIDDEN_DIM; ++k)
                val += decoder_output_W[j * HIDDEN_DIM + k] * dh[k];
            reconstructed[t][j] = val;
            prev_output[j] = val;
        }
    }

    // --- MSE ---
    float mse = 0.f;
    for (int t = 0; t < SEQ_LEN; ++t)
        for (int j = 0; j < INPUT_DIM; ++j) {
            float diff = sequence[t][j] - reconstructed[t][j];
            mse += diff * diff;
        }
    return mse / static_cast<float>(SEQ_LEN * INPUT_DIM);
}
```

---

### 4. CAN Frame Receiver with ML Integration (SocketCAN)

```cpp
// can_ml_monitor.cpp — Linux gateway example using SocketCAN
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

// Include project headers
#include "feature_extractor.hpp"
#include "isolation_forest.hpp"

// Declare extern forest (loaded from file / flash in production)
extern const std::array<std::array<IsolationNode, 64>, 50> g_forest;
extern const std::array<size_t, 50>                        g_forest_sizes;
constexpr float NORM_FACTOR = 11.32f;   // c(256 training samples)
constexpr float ALERT_THRESHOLD = 0.62f;

// Per-ID signal buffer — track IAT and byte-entropy
static CircularBuffer<WINDOW_SIZE> g_iat_buf[2048];   // one per CAN ID
static uint64_t                    g_last_ts[2048] = {};

static uint64_t now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1'000ULL;
}

// Simple byte entropy H = -sum(p * log2(p))
static float byte_entropy(const uint8_t* data, uint8_t dlc) {
    uint32_t freq[256] = {};
    for (int i = 0; i < dlc; ++i) ++freq[data[i]];
    float h = 0.f;
    for (int i = 0; i < 256; ++i) {
        if (!freq[i]) continue;
        float p = static_cast<float>(freq[i]) / static_cast<float>(dlc);
        h -= p * std::log2(p);
    }
    return h;
}

int main() {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return 1; }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, "vcan0", IFNAMSIZ);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr{};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    printf("[CAN-ML] Monitoring vcan0 for anomalies...\n");

    struct can_frame frame;
    while (true) {
        ssize_t nbytes = read(sock, &frame, sizeof(frame));
        if (nbytes < 0) { perror("read"); break; }

        uint32_t id  = frame.can_id & CAN_SFF_MASK;
        uint64_t now = now_us();

        // Compute inter-arrival time for this ID
        float iat_ms = (g_last_ts[id] > 0)
                     ? static_cast<float>(now - g_last_ts[id]) / 1000.f
                     : 0.f;
        g_last_ts[id] = now;

        float entropy = byte_entropy(frame.data, frame.can_dlc);

        // Push features into sliding window
        g_iat_buf[id].push(iat_ms, now);

        if (g_iat_buf[id].full()) {
            SignalFeatures feats = extract_features(g_iat_buf[id]);

            std::array<float, 8> fvec = {
                feats.mean,
                feats.variance,
                feats.iat_mean,
                feats.iat_variance,
                feats.mean_derivative,
                entropy,
                static_cast<float>(frame.can_dlc),
                static_cast<float>(id)
            };

            float score = anomaly_score(g_forest, g_forest_sizes, fvec, NORM_FACTOR);

            if (score > ALERT_THRESHOLD) {
                printf("[ALERT] ID=0x%03X  score=%.3f  iat_var=%.2f  entropy=%.2f\n",
                       id, score, feats.iat_variance, entropy);
            }
        }
    }

    close(sock);
    return 0;
}
```

---

## Implementation in Rust

Rust is increasingly used in automotive software for its memory safety guarantees and zero-cost abstractions. The following examples show equivalent ML inference infrastructure in Rust.

### 1. Feature Extractor — Circular Buffer and Statistics

```rust
// src/feature_extractor.rs

pub const WINDOW_SIZE: usize = 64;

/// Features extracted from a fixed-length signal window.
#[derive(Debug, Default, Clone, Copy)]
pub struct SignalFeatures {
    pub mean: f32,
    pub variance: f32,
    pub min_val: f32,
    pub max_val: f32,
    pub range: f32,
    pub mean_derivative: f32,
    pub iat_mean: f32,
    pub iat_variance: f32,
}

/// Fixed-capacity circular buffer for a single decoded CAN signal.
pub struct CircularBuffer {
    samples: [f32; WINDOW_SIZE],
    timestamps: [u64; WINDOW_SIZE], // microseconds
    head: usize,
    count: usize,
}

impl CircularBuffer {
    pub const fn new() -> Self {
        Self {
            samples:    [0.0; WINDOW_SIZE],
            timestamps: [0;   WINDOW_SIZE],
            head: 0,
            count: 0,
        }
    }

    pub fn push(&mut self, value: f32, timestamp_us: u64) {
        self.samples[self.head]    = value;
        self.timestamps[self.head] = timestamp_us;
        self.head = (self.head + 1) % WINDOW_SIZE;
        if self.count < WINDOW_SIZE {
            self.count += 1;
        }
    }

    pub fn is_full(&self) -> bool {
        self.count == WINDOW_SIZE
    }

    fn ordered(&self) -> impl Iterator<Item = (f32, u64)> + '_ {
        let start = if self.is_full() { self.head } else { 0 };
        (0..self.count).map(move |i| {
            let idx = (start + i) % WINDOW_SIZE;
            (self.samples[idx], self.timestamps[idx])
        })
    }

    /// Compute statistical features over the current window.
    pub fn extract_features(&self) -> Option<SignalFeatures> {
        if self.count < 2 {
            return None;
        }

        let vals: Vec<f32>  = self.ordered().map(|(v, _)| v).collect();
        let times: Vec<u64> = self.ordered().map(|(_, t)| t).collect();
        let n = vals.len() as f32;

        let mean = vals.iter().sum::<f32>() / n;
        let variance = vals.iter().map(|&v| (v - mean).powi(2)).sum::<f32>() / n;
        let min_val  = vals.iter().cloned().fold(f32::INFINITY, f32::min);
        let max_val  = vals.iter().cloned().fold(f32::NEG_INFINITY, f32::max);

        let mean_derivative = vals.windows(2)
            .map(|w| (w[1] - w[0]).abs())
            .sum::<f32>()
            / (n - 1.0);

        let iats: Vec<f32> = times.windows(2)
            .map(|w| (w[1] - w[0]) as f32 / 1000.0)   // us -> ms
            .collect();
        let iat_n    = iats.len() as f32;
        let iat_mean = iats.iter().sum::<f32>() / iat_n;
        let iat_variance = iats.iter()
            .map(|&t| (t - iat_mean).powi(2))
            .sum::<f32>()
            / iat_n;

        Some(SignalFeatures {
            mean,
            variance,
            min_val,
            max_val,
            range: max_val - min_val,
            mean_derivative,
            iat_mean,
            iat_variance,
        })
    }
}
```

---

### 2. Isolation Forest Inference

```rust
// src/isolation_forest.rs

/// A single node in a serialised isolation tree.
#[derive(Clone, Copy)]
pub struct IsolationNode {
    pub feature_idx: usize,
    pub threshold:   f32,
    /// `None` for leaf nodes.
    pub left_child:  Option<usize>,
    pub right_child: Option<usize>,
    /// Depth at which this leaf was reached (for scoring correction).
    pub leaf_depth:  f32,
}

/// Walk one tree and return the path length for the given feature vector.
pub fn path_length(nodes: &[IsolationNode], features: &[f32]) -> f32 {
    let mut idx = 0usize;
    let mut depth = 0.0f32;

    loop {
        let node = &nodes[idx];
        match (node.left_child, node.right_child) {
            (None, _) | (_, None) => {
                depth += node.leaf_depth;
                break;
            }
            (Some(l), Some(r)) => {
                depth += 1.0;
                if features[node.feature_idx] <= node.threshold {
                    idx = l;
                } else {
                    idx = r;
                }
            }
        }
    }
    depth
}

/// Compute anomaly score from an ensemble of isolation trees.
///
/// Returns a score in (0.0, 1.0); values > 0.6 are anomalous.
pub fn anomaly_score(
    forest: &[Vec<IsolationNode>],
    features: &[f32],
    norm_factor: f32,
) -> f32 {
    let mean_depth: f32 = forest.iter()
        .map(|tree| path_length(tree, features))
        .sum::<f32>()
        / forest.len() as f32;

    2.0_f32.powf(-mean_depth / norm_factor)
}

/// Compute c(n) — expected path length for a Binary Search Tree with n nodes.
pub fn expected_path_length(n: usize) -> f32 {
    if n <= 1 { return 0.0; }
    let n = n as f32;
    2.0 * (f32::ln(n - 1.0) + std::f32::consts::EGAMMA) - 2.0 * (n - 1.0) / n
}
```

---

### 3. CAN Frame Pipeline with ML Scoring

```rust
// src/can_ml_pipeline.rs
use std::collections::HashMap;

use crate::feature_extractor::CircularBuffer;
use crate::isolation_forest::{anomaly_score, IsolationNode};

pub const ALERT_THRESHOLD: f32 = 0.62;

/// A decoded CAN frame (after DBC processing).
#[derive(Debug)]
pub struct DecodedFrame {
    pub id:           u32,
    pub timestamp_us: u64,
    pub signals:      Vec<(String, f32)>,   // (signal_name, physical_value)
    pub raw_bytes:    Vec<u8>,
}

struct IdState {
    iat_buf:   CircularBuffer,
    last_time: Option<u64>,
}

impl IdState {
    fn new() -> Self {
        Self { iat_buf: CircularBuffer::new(), last_time: None }
    }
}

/// Byte entropy for raw-frame intrusion detection.
fn byte_entropy(data: &[u8]) -> f32 {
    if data.is_empty() { return 0.0; }
    let mut freq = [0u32; 256];
    for &b in data { freq[b as usize] += 1; }
    let n = data.len() as f32;
    freq.iter()
        .filter(|&&f| f > 0)
        .map(|&f| { let p = f as f32 / n; -p * p.log2() })
        .sum()
}

pub struct CanMlPipeline {
    id_states:   HashMap<u32, IdState>,
    forest:      Vec<Vec<IsolationNode>>,
    norm_factor: f32,
    alert_count: u64,
}

impl CanMlPipeline {
    pub fn new(forest: Vec<Vec<IsolationNode>>, norm_factor: f32) -> Self {
        Self {
            id_states: HashMap::new(),
            forest,
            norm_factor,
            alert_count: 0,
        }
    }

    /// Process a single decoded CAN frame. Returns `Some(score)` if an alert fires.
    pub fn process(&mut self, frame: &DecodedFrame) -> Option<f32> {
        let state = self.id_states
            .entry(frame.id)
            .or_insert_with(IdState::new);

        let iat_ms = state.last_time
            .map(|t| (frame.timestamp_us - t) as f32 / 1000.0)
            .unwrap_or(0.0);
        state.last_time = Some(frame.timestamp_us);

        state.iat_buf.push(iat_ms, frame.timestamp_us);

        if !state.iat_buf.is_full() {
            return None;
        }

        let feats = state.iat_buf.extract_features()?;
        let entropy = byte_entropy(&frame.raw_bytes);

        let feature_vec: Vec<f32> = vec![
            feats.mean,
            feats.variance,
            feats.iat_mean,
            feats.iat_variance,
            feats.mean_derivative,
            entropy,
            frame.raw_bytes.len() as f32,
            frame.id as f32,
        ];

        let score = anomaly_score(&self.forest, &feature_vec, self.norm_factor);

        if score > ALERT_THRESHOLD {
            self.alert_count += 1;
            eprintln!(
                "[ALERT #{:04}] CAN ID=0x{:03X}  score={:.3}  \
                 iat_var={:.2} ms^2  entropy={:.2} bits",
                self.alert_count, frame.id, score,
                feats.iat_variance, entropy
            );
            Some(score)
        } else {
            None
        }
    }

    pub fn alert_count(&self) -> u64 { self.alert_count }
}
```

---

### 4. Predictive Maintenance — Gradient Boosted Trees (Rust Inference)

```rust
// src/predictive_maintenance.rs
//
// Minimal gradient boosted tree (GBT) inference engine.
// Trees are trained offline (XGBoost / LightGBM) and exported
// to JSON/binary, then loaded here for RUL prediction.

/// A single regression tree node.
#[derive(Clone)]
pub enum TreeNode {
    Leaf { value: f32 },
    Split {
        feature:   usize,
        threshold: f32,
        left:      Box<TreeNode>,
        right:     Box<TreeNode>,
    },
}

impl TreeNode {
    pub fn predict(&self, features: &[f32]) -> f32 {
        match self {
            TreeNode::Leaf { value } => *value,
            TreeNode::Split { feature, threshold, left, right } => {
                if features[*feature] <= *threshold {
                    left.predict(features)
                } else {
                    right.predict(features)
                }
            }
        }
    }
}

/// Gradient boosted ensemble for RUL regression.
pub struct GradientBoostedRegressor {
    trees:         Vec<TreeNode>,
    learning_rate: f32,
    base_score:    f32,
}

impl GradientBoostedRegressor {
    pub fn new(trees: Vec<TreeNode>, learning_rate: f32, base_score: f32) -> Self {
        Self { trees, learning_rate, base_score }
    }

    /// Predict remaining useful life (RUL) in hours.
    pub fn predict_rul(&self, features: &[f32]) -> f32 {
        let sum: f32 = self.trees.iter()
            .map(|t| t.predict(features))
            .sum();
        self.base_score + self.learning_rate * sum
    }
}

/// Battery health features derived from CAN signals over a trip.
#[derive(Debug)]
pub struct BatteryHealthFeatures {
    pub soc_drop_per_km:        f32,  // % / km
    pub voltage_sag_under_load: f32,  // V
    pub regen_efficiency:       f32,  // ratio
    pub temp_rise_rate:         f32,  // deg C / min
    pub cycle_count:            f32,  // estimated
    pub internal_resistance:    f32,  // mΩ
}

impl BatteryHealthFeatures {
    pub fn to_vec(&self) -> Vec<f32> {
        vec![
            self.soc_drop_per_km,
            self.voltage_sag_under_load,
            self.regen_efficiency,
            self.temp_rise_rate,
            self.cycle_count,
            self.internal_resistance,
        ]
    }
}

#[derive(Debug)]
pub struct MaintenanceReport {
    pub component: &'static str,
    pub rul_hours: f32,
    pub severity:  Severity,
}

#[derive(Debug, PartialEq)]
pub enum Severity { Normal, Advisory, Warning, Critical }

fn severity_level(rul_hours: f32) -> Severity {
    match rul_hours as u32 {
        0..=50    => Severity::Critical,
        51..=200  => Severity::Warning,
        201..=500 => Severity::Advisory,
        _         => Severity::Normal,
    }
}

pub struct PredictiveMaintenanceSystem {
    battery_model: GradientBoostedRegressor,
    brake_model:   GradientBoostedRegressor,
}

impl PredictiveMaintenanceSystem {
    pub fn new(battery: GradientBoostedRegressor, brakes: GradientBoostedRegressor) -> Self {
        Self { battery_model: battery, brake_model: brakes }
    }

    pub fn assess_battery(&self, feats: &BatteryHealthFeatures) -> MaintenanceReport {
        let rul = self.battery_model.predict_rul(&feats.to_vec()).max(0.0);
        MaintenanceReport { component: "HV Battery", rul_hours: rul, severity: severity_level(rul) }
    }

    pub fn assess_brakes(&self, feats: &[f32]) -> MaintenanceReport {
        let rul = self.brake_model.predict_rul(feats).max(0.0);
        MaintenanceReport { component: "Brake Pads", rul_hours: rul, severity: severity_level(rul) }
    }
}
```

---

### 5. Integration Harness

```rust
// src/main.rs
mod feature_extractor;
mod isolation_forest;
mod can_ml_pipeline;
mod predictive_maintenance;

use can_ml_pipeline::{CanMlPipeline, DecodedFrame};
use isolation_forest::{expected_path_length, IsolationNode};

fn make_dummy_forest(n_trees: usize) -> Vec<Vec<IsolationNode>> {
    // Placeholder: production loads from binary/JSON
    (0..n_trees).map(|_| vec![
        IsolationNode { feature_idx: 2, threshold: 10.0,
                        left_child: Some(1), right_child: Some(2), leaf_depth: 0.0 },
        IsolationNode { feature_idx: 0, threshold: 5.0,
                        left_child: None, right_child: None, leaf_depth: 4.5 },
        IsolationNode { feature_idx: 3, threshold: 2.0,
                        left_child: None, right_child: None, leaf_depth: 3.2 },
    ]).collect()
}

fn main() {
    let forest       = make_dummy_forest(50);
    let norm_factor  = expected_path_length(256);
    let mut pipeline = CanMlPipeline::new(forest, norm_factor);

    // Simulate 200 normal frames at a 10 ms period
    for i in 0u64..200 {
        let frame = DecodedFrame {
            id:           0x1A0,
            timestamp_us: i * 10_000,
            signals:      vec![("EngineRPM".into(), 1500.0 + i as f32 * 0.5)],
            raw_bytes:    vec![0x12, 0x34, 0x56, 0x78],
        };
        pipeline.process(&frame);
    }

    // Inject anomalous frame: early arrival + impossible RPM + high-entropy bytes
    let anomalous = DecodedFrame {
        id:           0x1A0,
        timestamp_us: 200 * 10_000 + 1_500,   // arrives 1.5 ms early
        signals:      vec![("EngineRPM".into(), 9999.0)],
        raw_bytes:    vec![0xFF, 0xFF, 0xFF, 0xFF],
    };
    let result = pipeline.process(&anomalous);
    println!("Anomaly result: {:?}", result);
    println!("Total alerts fired: {}", pipeline.alert_count());
}
```

---

## Pipeline Overview

```
Raw CAN Bus
     |
     v
+------------------+
|  CAN Driver /    |  SocketCAN (Linux) or hardware HAL (AUTOSAR)
|  SocketCAN       |
+--------+---------+
         | can_frame {id, dlc, data[8], timestamp}
         v
+------------------+
|  DBC Decoder     |  Map raw bytes -> physical signal values
|  (optional)      |  using scaling, offsets, and mux rules
+--------+---------+
         | decoded signals {name, value, timestamp}
         v
+------------------+
|  Feature         |  Sliding window statistics per signal:
|  Extractor       |  mean, variance, IAT stats, derivatives,
|                  |  byte entropy, spectral features
+--------+---------+
         | feature vector [f32; N]
         v
+----------------------------------------------+
|               ML Inference Layer             |
|                                              |
|  +------------------+  +------------------+  |
|  |  Anomaly         |  |  Predictive      |  |
|  |  Detection       |  |  Maintenance     |  |
|  |  (Isolation      |  |  (GBT / LSTM)    |  |
|  |   Forest /       |  |                  |  |
|  |   LSTM AE)       |  |  -> RUL estimate |  |
|  +--------+---------+  +--------+---------+  |
|           |                     |            |
|  +--------v---------------------v---------+  |
|  |   Pattern Classifier (optional)        |  |
|  |   Driving mode / operating state       |  |
|  +----------------------------------------+  |
+--------------------+-------------------------+
                     | alerts, RUL, classification
                     v
+------------------+
|  Alert /         |  CAN DTC, UDS response,
|  Reporting Layer |  cloud telemetry, OBD-II
+------------------+
```

---

## Summary

Machine Learning on CAN data transforms a passive communication bus into an active diagnostic and safety intelligence layer. The table below summarises the three major application domains, their recommended techniques, and deployment considerations:

| Domain | Best Techniques | Key Features | Deployment |
|---|---|---|---|
| **Anomaly Detection** | Isolation Forest, LSTM Autoencoder, One-Class SVM | IAT variance, byte entropy, signal deviation | Real-time, per-frame (< 1 ms) |
| **Predictive Maintenance** | Gradient Boosted Trees, LSTM/GRU regression | Degradation proxies, physics-derived features, RUL labels | Trip-level batch (< 1 s) |
| **Pattern Recognition** | Random Forest, 1D-CNN, naïve Bayes | Window statistics, signal correlations, mode indicators | Window-level (100 ms – 1 s) |

### Core Engineering Principles

**Feature engineering is paramount.** Raw CAN bytes rarely make good ML input directly. Derived features — especially inter-arrival timing, signal derivatives, and cross-signal correlations — are far more discriminative and stable across vehicles.

**Embedded deployment demands frugality.** Models must be quantised to fixed-point, serialised as flat arrays, and evaluated within tight latency and memory envelopes (< 64 KB RAM for ECU deployment, < 1 ms per frame for intrusion detection).

**Rust and C/C++ are natural fits.** C/C++ dominates existing AUTOSAR/CAN middleware ecosystems. Rust offers equivalent performance with memory safety guarantees that are particularly valuable in safety-critical automotive applications.

**Offline training, online inference.** Train on large labelled or unlabelled datasets collected from fleet vehicles or test rigs. Deploy only the inference artifact to the ECU. Retrain periodically as fleet data accumulates.

**Unsupervised methods dominate in practice.** Labelled anomaly data is scarce. Isolation Forest and LSTM autoencoders trained solely on normal traffic are the most practical starting point for production systems.

---

*Document: 81_Machine_Learning_on_CAN_Data.md | CAN Bus Reference Series*