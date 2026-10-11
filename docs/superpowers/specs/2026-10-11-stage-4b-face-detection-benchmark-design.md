# Stage 4b: ESP-DL Human Face Detection Benchmark & Overlay on ESP32-S31 Design

> Date: 2026-10-11  
> Target: ESP32-S31 (ESP-Mosaico V1.2)  
> Branch: `feature/vision`  
> Deliverable: `docs/superpowers/specs/2026-10-11-stage-4b-face-detection-benchmark-design.md`

---

## 1. Context & Motivation

In Stage 4a, the camera preview pipeline was brought up and verified on the real ESP-Mosaico development board:
* **Camera Sensor**: SC101IOT (1280×720 UYVY).
* **Display Pipeline**: PPA hardware crop & scale (720×720 -> 480×480 RGB565), byte swap, AMOLED display.
* **Measured Performance**: 15.6 fps (`loop 63.9 ms`), `wait 39.7 ms`, `ppa 12.8 ms`, `swap 11.2 ms`, free PSRAM 8399 KB.
* **RF Safety**: Static link audit passed; no radio stack linked (`RF Verification Pending`).

Stage 4b aims to benchmark Espressif's deep learning face detection (`espressif/human_face_detect`, MSRMNP-s8 model) on the ESP32-S31 dual-core RISC-V processor to obtain real inference latency figures before designing the full interactive applications (tilt maze control, gesture navigation) in Stage 4c.

---

## 2. Core Constraints & Guarantees

1. **Zero RF Guarantee**: The firmware must not call or enable Wi-Fi, Bluetooth, ESP-NOW, or 802.15.4. `rf_map_check.py` must pass after build.
2. **Flash Partition Table Untouched**: The model (~191 KB) must be placed in Flash `.rodata` inside the 5 MB Factory App partition. The partition table and the 4 MB `model` partition (containing the offline speech models at `0x510000`) must remain completely untouched.
3. **Decoupled Frame Rate**: Neural network inference must run asynchronously on CPU Core 1 so that the camera preview frame rate on Core 0 remains at 15.6 fps without stutter or freezing.
4. **Target Board Safety**: Serial port `/dev/cu.usbmodem11201` is strictly forbidden. Flashing is restricted to the verified ESP32-S31 board (`MAC 1c:29:04:d0:a3:36`) using `scripts/vision_flash.sh update`.

---

## 3. Architecture & Data Flow

```
Camera Sensor (SC101IOT)
        │
        │ DVP DMA (1280x720 UYVY)
        ▼
   [Core 0] Camera Ingestion Task
        │
        ├───> PPA Scale (480x480 RGB565) ───> Framebuffer (480x480)
        │                                             │
        │ (Non-blocking handoff when worker idle)      ▼
        ▼                                      [Core 0] Display Present
   [Core 1] Inference Worker Task                      │
        │                                      (Draws camera frame +
        ▼                                       yellow bounding box +
   ESPDet-pico MSRMNP-s8 (Flash rodata)         metric text overlay)
        │                                             │
        ▼                                             ▼
   Face Coordinates [x, y, w, h] + Latency      AMOLED Screen
   (Atomic publish to global state)
```

### 3.1 Task Allocation
* **Core 0 (Camera & Present Task)**:
  * Manages DVP DMA frame receiving, PPA hardware scaling, and AMOLED display output.
  * When a frame is ready, if the inference worker is currently `IDLE`, it copies/points the downscaled image to the inference buffer and signals the worker.
  * Reads the latest bounding box coordinates and latency metrics atomically, drawing a 2-pixel-thick box around detected faces and overlaying the text statistics.
* **Core 1 (Face Detection Worker Task)**:
  * Priority: `tskIDLE_PRIORITY + 3` (lower than audio/display tasks).
  * Stack size: 16 KB allocated in PSRAM/Internal RAM.
  * Model input format: `dl::image::img_t` (RGB565 or RGB888 format as required by `esp-dl`).
  * Measures pure inference time (`esp_timer_get_time()`) from model invocation start to postprocessor finish.

---

## 4. Component & Build Configuration

### 4.1 Component Integration
* Add `espressif/human_face_detect` and `espressif/esp-dl` into `firmware/main/idf_component.yml` (conditioned on `EXP_VISION`).
* Adapt `IDF_TARGET` detection for `esp32s31`:
  * Map `esp32s31` to use RISC-V single-float ABI prebuilts/kernels compatible with `esp32p4` (`0x3, RVC, single-float ABI`).
* Model embedding:
  * Enable `CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=y` in `firmware/sdkconfig.defaults.vision`.
  * Pack `human_face_detect_msr_s8_v1.espdl` and `human_face_detect_mnp_s8_v1.espdl` into `human_face_detect.espdl.S` and link into the main binary.

### 4.2 Build Artefacts
* Firmware binary: `firmware/build_vision/mosaico_experience.bin`.
* Expected size: ~1.2 MB (fits easily within the 5.0 MB factory partition limit).

---

## 5. UI Metrics & Verification Indicators

In the camera preview test view, the debug overlay will display:
* `face: FOUND` (green) or `face: NONE` (gray).
* `box: [x, y, w, h]` (pixel coordinates normalized to 480×480 screen space).
* `infer: XX.X ms` (pure neural network execution time).
* `face fps: X.X` (inferences completed per second).
* Visual box: 2-pixel yellow/green outline around the face region.

---

## 6. Verification Plan & Success Criteria

1. **Compilation & Link**:
   * `idf.py -B build_vision -DEXP_VISION=1 build` succeeds with zero errors.
   * `scripts/rf_map_check.py` returns PASS (0 findings).
2. **Flash & Execution**:
   * Flashing via `scripts/vision_flash.sh update` completes with hash verified.
   * Device boots normally without crash or watchdog panic.
3. **Functional Metrics on Hardware**:
   * Triple-tapping bottom center opens camera view with real-time stream.
   * Moving a face in front of the lens triggers `face: FOUND` and overlays a bounding box.
   * Real inference latency on ESP32-S31 is recorded (e.g. XX ms) and entered into `docs/12_VISION_SETUP.md` and `docs/10_KNOWN_ISSUES.md`.
