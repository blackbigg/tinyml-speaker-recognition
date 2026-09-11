# TinyML On-Device Speaker Verification & Acoustic Control System
### 基於 TinyML 之語者特徵辨識與微控制器極限量化部署實作

[![Platform](https://img.shields.io/badge/Platform-Arduino%20Nano%2033%20BLE-00979D?logo=arduino)](https://store.arduino.cc/products/arduino-nano-33-ble)
[![MCU](https://img.shields.io/badge/MCU-Nordic%20nRF52840%20(Cortex--M4F%20%40%2064MHz)-blue)](https://www.nordicsemi.com/products/nrf52840)
[![Inference Latency](https://img.shields.io/badge/NN%20Inference-6%20ms-success)](https://github.com/blackbigg/tinyml-speaker-recognition)
[![Peak RAM](https://img.shields.io/badge/Peak%20RAM-31.3%20KB%20(NN%2013.5KB)-orange)](https://github.com/blackbigg/tinyml-speaker-recognition)
[![Test Accuracy](https://img.shields.io/badge/Test%20Accuracy-94.1%25-brightgreen)](https://github.com/blackbigg/tinyml-speaker-recognition)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> **Author**: **Cheng-Feng Liu (劉誠豐)**  
> Department of Electronic Engineering, National Taipei University of Technology (國立臺北科技大學 電子工程系)  
> Application Profile: National Yang Ming Chiao Tung University (NYCU), College of Industry-Academia Innovation, Institute of Intelligent Systems (840)

---

## 📌 Executive Summary (TL;DR)

* **Core Objective**: Conventional edge Keyword Spotting (KWS) systems lack **Speaker Verification (SV)** and often rely on cloud offloading, creating latency bottlenecks and privacy vulnerabilities. This project implements an **end-to-end, 100% offline, speaker-dependent acoustic control system** directly on a resource-constrained microcontroller (Nordic nRF52840, Arm Cortex-M4F @ 64MHz, 256KB SRAM, no NPU).
* **Quantitative Highlights**:
  * **Ultra-Low Memory Footprint**: Neural network tensor arena consumes only **13.5 KB SRAM** (Peak runtime RAM: **31.3 KB**, Flash: **31.4 KB**), taking less than **5.3%** of internal MCU memory.
  * **Real-Time Edge Response**: Single INT8 inference latency of **6 ms** (total processing delay **~24 ms** including 512-FFT MFCC extraction), operating within a 250 ms sliding window.
  * **Biometric Security**: Achieved **94.1% test accuracy** (ROC-AUC = 1.00) across 5 classes, with an unauthorized imposter False Acceptance Rate (**FAR**) of **3.6%** (GO) and **8.6%** (STOP).
  * **Acoustic Gating**: Implemented dynamic RMS Voice Activity Detection (VAD) with runtime noise self-calibration, eliminating unnecessary inference passes during ambient silence.

---

## 📊 Hardware Benchmarks & Performance Scorecard

| Dimension (評估維度) | Experimental Metric (實測數據與規格) | Engineering Significance (硬體限制考量與技術意涵) |
| :--- | :--- | :--- |
| **MCU & Architecture** | Arm Cortex-M4F @ 64MHz ｜ 1MB Flash ｜ 256KB SRAM | Standard industrial ultra-low-power MCU; no dedicated NPU accelerator |
| **Acoustic Frontend** | 16 kHz / 16-bit Mono PDM Microphone (MP34DT05) | Covers full human vocal spectrum up to 8 kHz Nyquist limit via DMA interrupt |
| **Pipeline Architecture** | Asynchronous Ping-Pong Double Buffering (250 ms/slice) | Completely decouples audio sampling from inference; zero sample drops |
| **Power Gating** | Dynamic RMS-VAD Ambient Noise Self-Calibration | Bypasses DSP and neural network when silent; minimizes standby power |
| **DSP Feature Extraction** | MFCC (20 cepstral coefficients, 512-FFT, 32 Mel filters) | Captures formant tracks ($F_1 \sim F_3$); feature extraction takes ~18 ms |
| **Quantization & Topology** | 1D-CNN (2 Conv1D + MaxPool + Dropout) ｜ INT8 Quantized | Compiled via Edge Impulse EON Compiler; weights and activations in 8-bit |
| **Memory Footprint** | **NN SRAM: 13.5 KB** (System Peak RAM: **31.3 KB** / Flash: **31.4 KB**) | Consumes only 5.3% of 256KB SRAM, leaving ample headroom for control logic |
| **Inference Latency** | **NN Inference: 6 ms** ｜ End-to-End System Delay: **~24 ms** | Sub-100ms real-time responsiveness; supports 4-slice sliding continuous mode |
| **Model Generalization** | Validation Acc: **93.9%** ｜ Test Acc: **94.1%** ｜ F1-Score: **0.94** | 5-class high-separability acoustic manifold (Owner/Imposter GO/STOP/Noise) |
| **Biometric Security** | False Acceptance Rate (FAR): **3.6% (GO) / 8.6% (STOP)**<br>False Rejection Rate (FRR): **5.4% (GO) / 0.0% (STOP)** | Verified robust imposter rejection while maintaining seamless authorized trigger |

---

## 🏗️ End-to-End System Architecture & Firmware Pipeline

The firmware operates on bare-metal C++ without an RTOS, structured into 4 synchronized execution phases:

![System Firmware Pipeline](docs/figures/fig1_system_flowchart_horizontal.png)

1. **Asynchronous Ping-Pong Double Buffering**:  
   Audio processing (DSP + NN) requires ~24 ms. A blocking architecture would drop incoming microphone samples during inference. We implement an `inference_t` double-buffer structure: the hardware PDM interrupt (`pdm_data_ready_inference_callback`) continuously fills the `Active Buffer` in the background, while the foreground CPU processes the `Ready Buffer`.
2. **Dynamic RMS-VAD Ambient Noise Self-Calibration**:  
   During system `setup()`, the MCU samples 8 consecutive audio slices to calculate baseline ambient background RMS noise. It dynamically establishes the VAD threshold with a safety margin (`VAD_THRESHOLD = RMS_avg + 0.01`). During runtime, any audio slice failing the RMS threshold is immediately bypassed without executing MFCC or NN inference.
3. **4-Slice Continuous Sliding Window (1000 ms Window / 250 ms Hop)**:  
   A full speech command is evaluated across a 1000 ms window composed of 4 x 250 ms slices. Every 250 ms, a new slice is pushed, maintaining continuous real-time listening with a reaction latency under 250 ms.
4. **Multi-Label Probability Resolution & Gating**:  
   Outputs from the softmax layer are gated by `CONFIDENCE_THRESHOLD = 0.55`:
   * $p_{\text{go\_me}} \ge 0.55$: Verified Authorized Owner $\rightarrow$ Assert **HIGH** (Turn ON LED).
   * $p_{\text{stop\_me}} \ge 0.55$: Verified Authorized Owner $\rightarrow$ Assert **LOW** (Turn OFF LED).
   * $p_{\text{others}} \ge 0.55$: Imposter Attempt Detected $\rightarrow$ Trigger **REJECT** security barrier.
   * Otherwise: Inconclusive / Background Noise $\rightarrow$ Preserve previous state.

---

## 🧠 Neural Network Topology & INT8 Quantization

![1D-CNN Topology](docs/figures/fig2_cnn_pipeline_horizontal.png)

* **Acoustic Preprocessing**: Input audio slices are transformed via 20-channel MFCC spanning **300 Hz ~ 6000 Hz** (pre-emphasis 0.98, 512-point FFT, 32 triangular filter banks).
* **Lightweight 1D-CNN Topology**:
  * Input Tensor: $(49 \times 20)$ Reshaped MFCC Spectrogram matrix.
  * **Conv1D Layer 1**: 8 Filters, Kernel Size = 3, ReLU activation.
  * **Conv1D Layer 2**: 16 Filters, Kernel Size = 3, ReLU activation.
  * **Regularization**: 1D Max-Pooling + Dropout ($p = 0.25$) to prevent acoustic overfitting.
  * **Output Layer**: Dense Softmax projecting into 5 distinct classes (`go_me`, `stop_me`, `go_others`, `stop_others`, `noise`).
* **INT8 Quantization (CMSIS-NN & EON Compiler)**:  
  Both weights and activations are quantized to 8-bit integers. Inference memory is reduced from ~54 KB (Float32) to **13.5 KB (INT8)**, with zero accuracy degradation.

---

## 📈 Experimental Validation & Biometric Evaluation

![Confusion Matrix & Performance](docs/figures/fig3_confusion_matrix_academic.png)

* **High Class Separability**: The INT8 quantized model demonstrates sharp diagonal clustering on the unseen test set, maintaining an average F1-score of **0.94** and overall accuracy of **94.1%**.
* **Zero Imposter Confusion**: Owner commands (`go_me`, `stop_me`) exhibit negligible cross-activation with unauthorized speakers (`go_others`, `stop_others`), confirming that vocal tract formant characteristics are accurately embedded into the latent representation.

---

## 🎬 Real-World On-Device Acoustic Verification (Demos)

The system was flashed onto an physical **Arduino Nano 33 BLE** and evaluated in real-world acoustic environments:

| Demo Video | Speaker & Spoken Command | System Decision & Gating Action | Physical Device State | Video Link |
| :---: | :---: | :---: | :---: | :---: |
| **Demo 1** | Authorized Owner: **"GO"** | `OWNER_GO` Confidence $\ge 0.55$ $\rightarrow$ High | **SUCCESS (LED ON)** | [me_test1.mp4](demo_videos/me_test1.mp4) |
| **Demo 2** | Authorized Owner: **"STOP"** | `OWNER_STOP` Confidence $\ge 0.55$ $\rightarrow$ Low | **SUCCESS (LED OFF)** | [me_test2.mp4](demo_videos/me_test2.mp4) |
| **Demo 3** | Unauthorized Imposter: **"GO"** | `IMPOSTER_GO` $\rightarrow$ Intercepted by REJECT | **BLOCKED (LED Kept OFF)** | [others_go.mp4](demo_videos/others_go.mp4) |
| **Demo 4** | Unauthorized Imposter: **"STOP"** | `IMPOSTER_STOP` $\rightarrow$ Intercepted by REJECT | **BLOCKED (LED Kept ON)** | [others_stop.mp4](demo_videos/others_stop.mp4) |

---

## 🔍 Engineering Limitations & Academic Insights

1. **Loudspeaker Secondary Replay Distortion (Natural Anti-Replay Defense)**:  
   When replaying pre-recorded owner voices through laptop speakers, the system consistently rejected the audio as `others`. Acoustic analysis revealed that commercial speaker frequency response non-linearities and diaphragm harmonic distortion alter human formant distributions. This physical acoustic attenuation naturally forms an inherent **Anti-Replay Attack barrier**, boosting practical access control security.
2. **Transient Power-On Spikes & Adaptive Noise Tracking**:  
   Hardware plug-in transient spikes can elevate the initial static VAD threshold. Future work introduces a sliding median filter during initialization to strip outlier spikes, alongside a leaky integrator in the main loop to track slow ambient noise drift (e.g., HVAC units) without manual recalibration.
3. **Temporal Window Dilution on Brief Utterances**:  
   With a fixed 1000 ms window, brief utterances (~300 ms) occupy a small fraction of the feature tensor. Introducing lightweight acoustic endpointing (Trimming) or Global Temporal Average Pooling will mitigate duration sensitivity.

---

## 🚀 Quick Start & Deployment Guide

### Prerequisites
* **Hardware**: [Arduino Nano 33 BLE](https://store.arduino.cc/products/arduino-nano-33-ble) (Nordic nRF52840) with onboard MP34DT05 PDM microphone.
* **Software**: Arduino IDE 2.x with `Arduino Mbed OS Nano Boards` package installed.

### Steps
1. **Clone the Repository**:
   ```bash
   git clone https://github.com/blackbigg/tinyml-speaker-recognition.git
   cd tinyml-speaker-recognition
   ```
2. **Install the Edge Impulse Model Library**:
   * Open Arduino IDE.
   * Navigate to `Sketch` $\rightarrow$ `Include Library` $\rightarrow$ `Add .ZIP Library...`
   * Select `models/ei-edge_computing_project-arduino-1.0.1.zip`.
3. **Flash the Firmware**:
   * Open `firmware/speaker_verification/speaker_verification.ino`.
   * Select Board: **Arduino Nano 33 BLE** and the corresponding COM port.
   * Click **Upload**.
4. **Monitor Output**:
   * Open Serial Monitor at **115200 baud**.
   * Wait 2 seconds for dynamic noise calibration.
   * Speak "GO" or "STOP" to command the system.

---

## 📂 Repository Directory Structure

```
tinyml-speaker-recognition/
├── README.md                      # Comprehensive academic & engineering documentation
├── LICENSE                        # MIT License
├── .gitignore                     # Arduino / C++ gitignore specification
├── firmware/
│   └── speaker_verification/
│       └── speaker_verification.ino # Clean, commented Arduino firmware source
├── models/
│   └── ei-edge_computing_project-arduino-1.0.1.zip # Compiled Edge Impulse EON Library
├── docs/
│   ├── figures/
│   │   ├── fig1_system_flowchart_horizontal.png # Horizontal system pipeline
│   │   ├── fig2_cnn_pipeline_horizontal.png    # 1D-CNN topology diagram
│   │   └── fig3_confusion_matrix_academic.png  # Confusion matrix & scorecard
│   └── reports/
│       ├── 推甄專用_邊緣運算專題實作報告.md      # Full academic project report (Markdown)
│       └── 推甄專用_邊緣運算專題實作報告.docx    # Formatted Word report (24pt/20pt/12pt/10pt)
└── demo_videos/
    ├── me_test1.mp4               # Owner "GO" -> LED ON verification
    ├── me_test2.mp4               # Owner "STOP" -> LED OFF verification
    ├── others_go.mp4              # Imposter "GO" -> Blocked verification
    └── others_stop.mp4            # Imposter "STOP" -> Blocked verification
```

---

## 📜 Academic Citation & Contact

If this repository or its firmware pipeline assists your research or projects, please cite:

```bibtex
@misc{liu2024tinymlspeaker,
  author = {Cheng-Feng Liu (劉誠豐)},
  title = {TinyML On-Device Speaker Verification & Acoustic Control System on Nordic nRF52840},
  year = {2024},
  publisher = {GitHub},
  howpublished = {\url{https://github.com/blackbigg/tinyml-speaker-recognition}}
}
```

* **Author**: 劉誠豐 (Cheng-Feng Liu)
* **Email**: tw0900172332@gmail.com
* **Affiliation**: Department of Electronic Engineering, National Taipei University of Technology (國立臺北科技大學 電子工程系)
