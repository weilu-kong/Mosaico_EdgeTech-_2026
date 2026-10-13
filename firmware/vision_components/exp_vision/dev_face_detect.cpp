// SPDX-License-Identifier: Apache-2.0
// Asynchronous hand spatial tracking worker for ESP-Mosaico Experience (Stage 4c - Scheme A).
// Runs ESPDet-Pico 224x224 hand detector on Core 1 asynchronously.
// Ingests downsampled 224x224 RGB565BE frames without blocking camera preview stream.
// Maps hand position relative to center with high gain and 15-frame hold for crisp, immediate maze control.
// No wireless or network functions.

#include "dev_face_detect.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "hand_detect.hpp"

static const char *TAG = "dev_hand";

#define HAND_DETECT_W 224
#define HAND_DETECT_H 224
#define HAND_DETECT_BYTES ((size_t)HAND_DETECT_W * HAND_DETECT_H * sizeof(uint16_t))
#define FACE_TASK_STACK 24576
#define FACE_TASK_PRIO 2 /* Strictly lower than camera task (prio 4) */
#define FACE_TASK_CORE 1
#define HOLD_MAX_MISSES 15 /* ~1.5 - 2.0 s hold when hand moves fast or temporarily blurs */

#define HAND_SCORE_ACQUIRE 0.18f
#define HAND_SCORE_MAINTAIN 0.12f

static SemaphoreHandle_t s_result_mutex = NULL;
static SemaphoreHandle_t s_frame_ready_sem = NULL;
static TaskHandle_t s_task = NULL;
static uint16_t *s_frame_buf = NULL;

static volatile bool s_active = false;
static volatile bool s_busy = false;
static uint32_t s_runs = 0;
static dev_face_result_t s_result = {};

static void face_detect_worker(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "worker started on core %d, initializing HandDetect model...", xPortGetCoreID());
    HandDetect *model = new HandDetect(HandDetect::ESPDET_PICO_224_224_HAND, false);
    if (!model) {
        ESP_LOGE(TAG, "HandDetect initialization failed, worker exiting");
        vTaskDelete(NULL);
        return;
    }

    model->set_score_thr(HAND_SCORE_MAINTAIN);
    ESP_LOGI(TAG, "HandDetect (ESPDet-Pico 224x224) loaded successfully, score_thr=%.2f", HAND_SCORE_MAINTAIN);

    bool have_smooth = false;
    float smooth_cx = 240.0f, smooth_cy = 240.0f;
    float smooth_w = 100.0f, smooth_h = 100.0f;
    float last_score = 0.0f;
    int miss_count = HOLD_MAX_MISSES + 1;

    for (;;) {
        if (xSemaphoreTake(s_frame_ready_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!s_active) {
            have_smooth = false;
            miss_count = HOLD_MAX_MISSES + 1;
            s_busy = false;
            continue;
        }

        // Invalidate cache to ensure CPU reads newest 224x224 frame written to PSRAM
        esp_cache_msync(s_frame_buf, HAND_DETECT_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

        dl::image::img_t img = {
            .data = s_frame_buf,
            .width = (uint16_t)HAND_DETECT_W,
            .height = (uint16_t)HAND_DETECT_H,
            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE,
        };

        const int64_t t0 = esp_timer_get_time();
        std::list<dl::detect::result_t> &results = model->run(img);
        const int64_t t_infer = esp_timer_get_time() - t0;

        dev_face_result_t res = {};
        res.infer_us = (int)t_infer;
        res.fps = t_infer > 0 ? (1000000.0f / (float)t_infer) : 0.0f;
        s_runs++;
        res.total_runs = s_runs;

        // Find best detection exceeding our threshold (hysteresis: lower threshold if already tracking)
        const float required_thr = have_smooth ? HAND_SCORE_MAINTAIN : HAND_SCORE_ACQUIRE;
        auto best_it = results.end();
        float best_score = required_thr;
        for (auto it = results.begin(); it != results.end(); ++it) {
            if (it->score >= best_score) {
                best_score = it->score;
                best_it = it;
            }
        }

        if (best_it != results.end()) {
            const int bx224 = best_it->box[0];
            const int by224 = best_it->box[1];
            const int bw224 = best_it->box[2] - best_it->box[0];
            const int bh224 = best_it->box[3] - best_it->box[1];

            // Scale to 480x480 coordinate space
            const float scale_to_480 = 480.0f / (float)HAND_DETECT_W;
            const float raw_cx = (float)(bx224 + bw224 / 2) * scale_to_480;
            const float raw_cy = (float)(by224 + bh224 / 2) * scale_to_480;

            const float dx_raw = raw_cx - 240.0f;
            const float dy_raw = raw_cy - 240.0f;

            // Scheme A High-Gain Spatial Control:
            // Deadzone of 18 px around centre (240, 240).
            // Outside 18 px, immediate crisp acceleration gain (1.6x) ensuring instant ball movement!
            float target_cx = 240.0f;
            float target_cy = 240.0f;

            if (std::abs(dx_raw) > 18.0f) {
                target_cx = 240.0f + (dx_raw > 0 ? 1.0f : -1.0f) * (24.0f + (std::abs(dx_raw) - 18.0f) * 1.6f);
            }
            if (std::abs(dy_raw) > 18.0f) {
                target_cy = 240.0f + (dy_raw > 0 ? 1.0f : -1.0f) * (24.0f + (std::abs(dy_raw) - 18.0f) * 1.6f);
            }

            if (target_cx < 40.0f) target_cx = 40.0f;
            if (target_cx > 440.0f) target_cx = 440.0f;
            if (target_cy < 40.0f) target_cy = 40.0f;
            if (target_cy > 440.0f) target_cy = 440.0f;

            if (!have_smooth) {
                smooth_cx = target_cx;
                smooth_cy = target_cy;
                smooth_w = (float)bw224 * scale_to_480;
                smooth_h = (float)bh224 * scale_to_480;
                have_smooth = true;
            } else {
                smooth_cx = smooth_cx * 0.35f + target_cx * 0.65f;
                smooth_cy = smooth_cy * 0.35f + target_cy * 0.65f;
                smooth_w = smooth_w * 0.50f + ((float)bw224 * scale_to_480) * 0.50f;
                smooth_h = smooth_h * 0.50f + ((float)bh224 * scale_to_480) * 0.50f;
            }
            last_score = best_it->score;
            miss_count = 0;

            dev_face_box_t sb = {
                .x = (int)(smooth_cx - smooth_w * 0.5f),
                .y = (int)(smooth_cy - smooth_h * 0.5f),
                .w = (int)smooth_w,
                .h = (int)smooth_h,
            };
            res.found = true;
            res.score = last_score;
            res.box = dev_face_box_clamp(sb, 480, 480);
        } else if (have_smooth && miss_count < HOLD_MAX_MISSES) {
            miss_count++;
            // Gently decay towards center during short dropout while keeping control active
            smooth_cx = smooth_cx * 0.94f + 240.0f * 0.06f;
            smooth_cy = smooth_cy * 0.94f + 240.0f * 0.06f;
            dev_face_box_t sb = {
                .x = (int)(smooth_cx - smooth_w * 0.5f),
                .y = (int)(smooth_cy - smooth_h * 0.5f),
                .w = (int)smooth_w,
                .h = (int)smooth_h,
            };
            res.found = true;
            res.score = last_score;
            res.box = dev_face_box_clamp(sb, 480, 480);
        } else {
            have_smooth = false;
            res.found = false;
        }

        if (s_result_mutex && xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_result = res;
            xSemaphoreGive(s_result_mutex);
        }

        if (res.found && (s_runs == 1 || (s_runs % 10) == 0)) {
            ESP_LOGI(TAG, "hand SCHEME_A pos=[%d,%d] score=%.2f infer=%.1fms (%.1ffps) runs=%u miss=%d",
                     (int)smooth_cx, (int)smooth_cy, (double)res.score,
                     (double)res.infer_us / 1000.0, (double)res.fps, (unsigned)s_runs, miss_count);
        } else if (!res.found && (s_runs % 30) == 0) {
            ESP_LOGI(TAG, "hand NONE infer=%.1fms (%.1ffps) runs=%u",
                     (double)res.infer_us / 1000.0, (double)res.fps, (unsigned)s_runs);
        }

        s_busy = false;
    }

    delete model;
    vTaskDelete(NULL);
}

extern "C" {

void dev_face_detect_init(void)
{
    if (s_task) return;

    if (!s_result_mutex) {
        s_result_mutex = xSemaphoreCreateMutex();
    }
    if (!s_frame_ready_sem) {
        s_frame_ready_sem = xSemaphoreCreateBinary();
    }

    if (!s_frame_buf) {
        s_frame_buf = (uint16_t *)heap_caps_aligned_calloc(
            128, 1, HAND_DETECT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_frame_buf) {
            ESP_LOGE(TAG, "failed to allocate %u bytes in PSRAM for frame buffer", (unsigned)HAND_DETECT_BYTES);
            return;
        }
    }

    if (!s_task) {
        BaseType_t ret = xTaskCreatePinnedToCore(
            face_detect_worker, "face_detect", FACE_TASK_STACK, NULL,
            FACE_TASK_PRIO, &s_task, FACE_TASK_CORE);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "failed to create face_detect task");
            s_task = NULL;
            return;
        }
    }

    ESP_LOGI(TAG, "hand detection (Scheme A) initialized");
}

void dev_face_detect_start(void)
{
    dev_face_detect_init();
    s_active = true;
    s_busy = false;
    ESP_LOGI(TAG, "hand detection activated");
}

void dev_face_detect_stop(void)
{
    s_active = false;
    s_busy = false;
    if (s_result_mutex && xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memset(&s_result, 0, sizeof(s_result));
        xSemaphoreGive(s_result_mutex);
    }
    ESP_LOGI(TAG, "hand detection paused");
}

void dev_face_detect_get_result(dev_face_result_t *out)
{
    if (!out) return;
    if (s_result_mutex && xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        *out = s_result;
        xSemaphoreGive(s_result_mutex);
    }
}

void dev_face_detect_submit_frame(const uint16_t *rgb565_frame)
{
    if (!s_active || !s_frame_buf || !rgb565_frame || s_busy) {
        return;
    }

    s_busy = true;
    // Fast nearest-neighbor downsample from 480x480 into 224x224 native model resolution
    for (int y = 0; y < HAND_DETECT_H; ++y) {
        const uint16_t *src_row = rgb565_frame + ((y * 480) / HAND_DETECT_H) * 480;
        uint16_t *dst_row = s_frame_buf + y * HAND_DETECT_W;
        for (int x = 0; x < HAND_DETECT_W; ++x) {
            dst_row[x] = src_row[(x * 480) / HAND_DETECT_W];
        }
    }
    esp_cache_msync(s_frame_buf, HAND_DETECT_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (s_frame_ready_sem) {
        xSemaphoreGive(s_frame_ready_sem);
    }
}

} // extern "C"
