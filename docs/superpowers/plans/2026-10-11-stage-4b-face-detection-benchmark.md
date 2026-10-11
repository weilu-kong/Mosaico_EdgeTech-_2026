# Stage 4b: Face Detection Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate ESPDet-pico human face detection into the ESP32-S31 camera pipeline, embed the model in Flash rodata without modifying partitions, benchmark real inference latency on hardware, and render real-time bounding box and latency metrics on the display.

**Architecture:** An asynchronous inference task on CPU Core 1 ingests downscaled camera frames non-blockingly, runs `HumanFaceDetect::run()`, and publishes bounding box coordinates and execution timing metrics atomically to the Core 0 display renderer without impacting the 15.6 fps camera preview stream.

**Tech Stack:** ESP-IDF 6.2 (riscv32-esp-elf), FreeRTOS, `espressif/human_face_detect` (MSRMNP-s8), `espressif/esp-dl`, PPA, C11/C++17.

---

### Task 1: Component & Kconfig Dependencies for Face Detection

**Files:**
- Modify: `firmware/vision_components/exp_vision/idf_component.yml`
- Modify: `firmware/sdkconfig.defaults.vision`
- Modify: `firmware/vision_components/exp_vision/CMakeLists.txt`

- [ ] **Step 1: Update `idf_component.yml` to declare `human_face_detect` and `esp-dl`**

```yaml
# firmware/vision_components/exp_vision/idf_component.yml
dependencies:
  idf: ">=6.2"
  mosaico_module_camera:
    version: "*"
    override_path: "../../../workspace/esp-mosaico-vibe/submodule/esp-mosaico-bsp/components/mosaico_module_camera"
  espressif/human_face_detect: "^0.3.0"
  espressif/esp-dl: "^3.2.0"
```

- [ ] **Step 2: Update `sdkconfig.defaults.vision` for rodata model packing**

Append the following lines to `firmware/sdkconfig.defaults.vision`:
```ini
CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=y
CONFIG_FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1=y
CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL=0
```

- [ ] **Step 3: Update `exp_vision/CMakeLists.txt` to register C++ sources and requirements**

Update `firmware/vision_components/exp_vision/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "dev_camera.c" "dev_face_detect.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mosaico_module_camera esp_driver_ppa esp_mm esp_timer freertos heap log human_face_detect esp-dl)
```

- [ ] **Step 4: Verify CMake configuration dry-run**

Run: `cd firmware && . ~/esp/esp-idf-mosaico-7b9cc1a/export.sh && idf.py -B build_vision -DEXP_VISION=1 reconfigure`
Expected: CMake generates build files with `human_face_detect` registered.

- [ ] **Step 5: Commit**

```bash
git add firmware/vision_components/exp_vision/idf_component.yml firmware/sdkconfig.defaults.vision firmware/vision_components/exp_vision/CMakeLists.txt
git commit -m "build(vision): add human_face_detect and esp-dl dependencies for stage 4b"
```

---

### Task 2: Portable Face Bounding Box & Metric Core with Host Unit Tests

**Files:**
- Create: `firmware/vision_components/exp_vision/include/dev_face_core.h`
- Create: `app/mosaico_experience/tests/dev_face_core_test.c`
- Modify: `scripts/run_all_tests.sh:42-45`

- [ ] **Step 1: Write the failing host unit test**

Create `app/mosaico_experience/tests/dev_face_core_test.c`:
```c
#include <assert.h>
#include <stdio.h>
#include "dev_face_core.h"

int main(void)
{
    // Test bounding box clamp to 480x480 view
    dev_face_box_t b1 = {-10, 50, 100, 100};
    dev_face_box_t c1 = dev_face_box_clamp(b1, 480, 480);
    assert(c1.x == 0 && c1.y == 50 && c1.w == 90 && c1.h == 100);

    // Test box with coordinates overflowing screen
    dev_face_box_t b2 = {400, 420, 120, 100};
    dev_face_box_t c2 = dev_face_box_clamp(b2, 480, 480);
    assert(c2.x == 400 && c2.y == 420 && c2.w == 80 && c2.h == 60);

    // Test aspect center
    dev_face_box_t b3 = {100, 100, 200, 200};
    int cx = 0, cy = 0;
    dev_face_box_center(b3, &cx, &cy);
    assert(cx == 200 && cy == 200);

    printf("dev_face_core: all checks passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cc -I firmware/vision_components/exp_vision/include app/mosaico_experience/tests/dev_face_core_test.c -o /tmp/face_test && /tmp/face_test`
Expected: FAIL (`dev_face_core.h: No such file or directory`)

- [ ] **Step 3: Implement `dev_face_core.h`**

Create `firmware/vision_components/exp_vision/include/dev_face_core.h`:
```c
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int x;
    int y;
    int w;
    int h;
} dev_face_box_t;

typedef struct {
    bool found;
    dev_face_box_t box;
    float score;
    int infer_us;
    float fps;
    uint32_t total_runs;
} dev_face_result_t;

static inline dev_face_box_t dev_face_box_clamp(dev_face_box_t b, int max_w, int max_h)
{
    int x0 = b.x < 0 ? 0 : b.x;
    int y0 = b.y < 0 ? 0 : b.y;
    int x1 = b.x + b.w > max_w ? max_w : b.x + b.w;
    int y1 = b.y + b.h > max_h ? max_h : b.y + b.h;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    dev_face_box_t out = {x0, y0, x1 - x0, y1 - y0};
    return out;
}

static inline void dev_face_box_center(dev_face_box_t b, int *cx, int *cy)
{
    if (cx) *cx = b.x + b.w / 2;
    if (cy) *cy = b.y + b.h / 2;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cc -std=c11 -Wall -Wextra -Werror -I firmware/vision_components/exp_vision/include app/mosaico_experience/tests/dev_face_core_test.c -o /tmp/face_test && /tmp/face_test`
Expected: PASS (`dev_face_core: all checks passed`)

- [ ] **Step 5: Integrate test into `scripts/run_all_tests.sh` and commit**

Add step to `scripts/run_all_tests.sh` after camera preview helpers step:
```bash
step "face detect helpers (bounding box clamp and center; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror -I "$ROOT/firmware/vision_components/exp_vision/include" \
   "$APP/tests/dev_face_core_test.c" -o /tmp/dev_face_core_bin && /tmp/dev_face_core_bin || rc=1
```

Commit:
```bash
git add firmware/vision_components/exp_vision/include/dev_face_core.h app/mosaico_experience/tests/dev_face_core_test.c scripts/run_all_tests.sh
git commit -m "feat(vision): add dev_face_core bounding box helpers and host unit tests"
```

---

### Task 3: Asynchronous Face Detection Worker Component

**Files:**
- Create: `firmware/vision_components/exp_vision/include/dev_face_detect.h`
- Create: `firmware/vision_components/exp_vision/dev_face_detect.cpp`

- [ ] **Step 1: Define `dev_face_detect.h` interface**

Create `firmware/vision_components/exp_vision/include/dev_face_detect.h`:
```c
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dev_face_core.h"

#ifdef __cplusplus
extern "C" {
#endif

void dev_face_detect_init(void);
void dev_face_detect_start(void);
void dev_face_detect_stop(void);
void dev_face_detect_get_result(dev_face_result_t *out);

// Called by camera task on Core 0 when a new 480x480 frame is ready
void dev_face_detect_submit_frame(const uint16_t *rgb565_frame);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Implement `dev_face_detect.cpp`**

Create `firmware/vision_components/exp_vision/dev_face_detect.cpp`:
Implement worker FreeRTOS task on Core 1:
- Instantiates `HumanFaceDetect` (lazy load or on `start()`).
- Waits on a notification/semaphore when a frame is submitted.
- Converts input RGB565 frame to `dl::image::img_t`.
- Invokes `model.run()`.
- Records timing with `esp_timer_get_time()`.
- Updates `s_result` protected by a spinlock/mutex.

- [ ] **Step 3: Verify compile syntax with IDF clang / riscv32 gcc**

Run: `cd firmware && . ~/esp/esp-idf-mosaico-7b9cc1a/export.sh && idf.py -B build_vision -DEXP_VISION=1 build`
Expected: Clean compilation and object file generation.

- [ ] **Step 4: Commit**

```bash
git add firmware/vision_components/exp_vision/include/dev_face_detect.h firmware/vision_components/exp_vision/dev_face_detect.cpp
git commit -m "feat(vision): implement asynchronous face detection worker task on Core 1"
```

---

### Task 4: Camera View Integration & Bounding Box Overlay

**Files:**
- Modify: `firmware/vision_components/exp_vision/dev_camera.c`
- Modify: `firmware/main/device_main.c`

- [ ] **Step 1: Hook frame submission in `dev_camera.c`**

In `dev_camera.c` main loop, when a new frame is completed and swapped:
```c
dev_face_detect_submit_frame(s_view[s_pub]);
```

- [ ] **Step 2: Update `device_main.c` to start/stop face detection with camera view**

In `cam_view_open()`:
```c
dev_face_detect_start();
```
In `cam_view_close()`:
```c
dev_face_detect_stop();
```

- [ ] **Step 3: Implement bounding box outline drawing & metric text in `draw_cam_view()`**

In `device_main.c:draw_cam_view()`:
Read `dev_face_result_t face_res; dev_face_detect_get_result(&face_res);`
If `face_res.found`:
Draw 2-pixel yellow rectangle outline:
```c
dev_face_box_t b = dev_face_box_clamp(face_res.box, 480, 480);
exp_rect(b.x, b.y, b.w, 2, yel);
exp_rect(b.x, b.y + b.h - 2, b.w, 2, yel);
exp_rect(b.x, b.y, 2, b.h, yel);
exp_rect(b.x + b.w - 2, b.y, 2, b.h, yel);
```
Add face metrics line in overlay:
```c
snprintf(l, sizeof(l), "face %s  infer %d.%d ms  box [%d,%d,%d,%d]",
         face_res.found ? "FOUND" : "NONE",
         face_res.infer_us / 1000, (face_res.infer_us % 1000) / 100,
         b.x, b.y, b.w, b.h);
cam_text(l, 4, face_res.found ? yel : wht);
```

- [ ] **Step 4: Commit**

```bash
git add firmware/vision_components/exp_vision/dev_camera.c firmware/main/device_main.c
git commit -m "feat(ui): add real-time face bounding box and latency overlay in camera view"
```

---

### Task 5: Build & RF Security Audit

**Files:**
- Output: `firmware/build_vision/mosaico_experience.bin`
- Output: `firmware/build_vision/mosaico_experience.map`

- [ ] **Step 1: Execute full firmware build**

Run: `cd firmware && . ~/esp/esp-idf-mosaico-7b9cc1a/export.sh && idf.py -B build_vision -DEXP_VISION=1 build`
Expected: `Project build complete` (exit code 0), bin size fits 5 MB factory partition.

- [ ] **Step 2: Run RF map check script**

Run: `python3 scripts/rf_map_check.py firmware/build_vision/mosaico_experience.map firmware/build_vision/sdkconfig`
Expected: `PASS (no Wi-Fi MAC / BT / 802.15.4 / coex / lwIP code linked)`.

- [ ] **Step 3: Update dependency lock and commit**

```bash
git add firmware/dependencies.vision.lock
git commit -m "build(vision): update dependencies.vision.lock with esp-dl components"
```

---

### Task 6: Hardware Flashing & Real Benchmark Verification

**Files:**
- Modify: `docs/12_VISION_SETUP.md`
- Modify: `docs/10_KNOWN_ISSUES.md`
- Modify: `docs/HANDOFF.md`

- [ ] **Step 1: Put device into ROM download mode and flash**

Run:
```bash
bash scripts/vision_flash.sh check /dev/cu.usbmodem11401
cd firmware && VISION_FLASH_CONFIRM=yes bash ../scripts/vision_flash.sh update /dev/cu.usbmodem11401
```
Expected: `Hash of data verified.` `Hard resetting with a watchdog...`

- [ ] **Step 2: Verify on device**

1. Press Reset if screen stays dark.
2. Triple-tap bottom center to open camera view.
3. Observe live camera preview: face detection box appears when facing the camera.
4. Read latency (`infer: XX.X ms`) and frame rate.

- [ ] **Step 3: Document real measured numbers and push to GitHub**

Update `docs/12_VISION_SETUP.md`, `docs/10_KNOWN_ISSUES.md`, and `docs/HANDOFF.md` with:
- Exact inference latency on ESP32-S31.
- Inference FPS.
- Memory consumption.
Commit & push:
```bash
git add docs/12_VISION_SETUP.md docs/10_KNOWN_ISSUES.md docs/HANDOFF.md
git commit -m "docs: record Stage 4b face detection real inference benchmark numbers on ESP32-S31"
git push origin feature/vision
```
