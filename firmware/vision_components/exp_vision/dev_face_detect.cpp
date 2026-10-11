// SPDX-License-Identifier: Apache-2.0
// Asynchronous face detection worker for ESP-Mosaico Experience (Stage 4b).
// Runs ESPDet-pico / MSRMNP-s8 face detector on Core 1 asynchronously.
// Ingests RGB565BE frames without blocking camera preview stream.
// No wireless or network functions.

#include "dev_face_detect.h"

#include <cstring>
#include <list>

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "human_face_detect.hpp"

static const char *TAG = "dev_face";

#define FACE_DETECT_W 480
#define FACE_DETECT_H 480
#define FACE_DETECT_BYTES ((size_t)FACE_DETECT_W * FACE_DETECT_H * sizeof(uint16_t))
#define FACE_TASK_STACK 24576
#define FACE_TASK_PRIO 2 /* Strictly lower than camera task (prio 4) */
#define FACE_TASK_CORE 1

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
    ESP_LOGI(TAG, "worker started on core %d, initializing model...", xPortGetCoreID());
    HumanFaceDetect *model = new HumanFaceDetect();
    if (!model) {
        ESP_LOGE(TAG, "model initialization failed, face detect worker exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "HumanFaceDetect model loaded successfully");

    for (;;) {
        if (xSemaphoreTake(s_frame_ready_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!s_active) {
            s_busy = false;
            continue;
        }

        // Invalidate cache to ensure CPU reads newest frame written to PSRAM
        esp_cache_msync(s_frame_buf, FACE_DETECT_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

        dl::image::img_t img = {
            .data = s_frame_buf,
            .width = (uint16_t)FACE_DETECT_W,
            .height = (uint16_t)FACE_DETECT_H,
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

        if (!results.empty()) {
            auto best_it = results.begin();
            for (auto it = results.begin(); it != results.end(); ++it) {
                if (it->score > best_it->score) {
                    best_it = it;
                }
            }
            res.found = true;
            res.score = best_it->score;
            dev_face_box_t raw = {
                .x = best_it->box[0],
                .y = best_it->box[1],
                .w = best_it->box[2] - best_it->box[0],
                .h = best_it->box[3] - best_it->box[1],
            };
            res.box = dev_face_box_clamp(raw, FACE_DETECT_W, FACE_DETECT_H);
        } else {
            res.found = false;
        }

        if (s_result_mutex && xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_result = res;
            xSemaphoreGive(s_result_mutex);
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
            128, 1, FACE_DETECT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_frame_buf) {
            ESP_LOGE(TAG, "failed to allocate %u bytes in PSRAM for frame buffer", (unsigned)FACE_DETECT_BYTES);
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

    ESP_LOGI(TAG, "face detection initialized");
}

void dev_face_detect_start(void)
{
    dev_face_detect_init();
    s_active = true;
    s_busy = false;
    ESP_LOGI(TAG, "face detection activated");
}

void dev_face_detect_stop(void)
{
    s_active = false;
    s_busy = false;
    if (s_result_mutex && xSemaphoreTake(s_result_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memset(&s_result, 0, sizeof(s_result));
        xSemaphoreGive(s_result_mutex);
    }
    ESP_LOGI(TAG, "face detection paused");
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
    memcpy(s_frame_buf, rgb565_frame, FACE_DETECT_BYTES);
    esp_cache_msync(s_frame_buf, FACE_DETECT_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (s_frame_ready_sem) {
        xSemaphoreGive(s_frame_ready_sem);
    }
}

} // extern "C"
