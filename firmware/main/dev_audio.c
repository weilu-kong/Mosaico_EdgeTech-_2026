// SPDX-License-Identifier: Apache-2.0
#include "dev_audio.h"

#include <string.h>

#include "bsp/esp_mosaico.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#if EXP_VOICE
#include "dev_voice.h"
#endif

static const char *TAG = "dev_audio";

#define SPK_CHUNK_FRAMES 480          /* 10 ms of stereo frames per codec write */
#define CUE_QUEUE_LEN 4
#define MIC_ERR_BACKOFF_MS 50

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dev_audio_t s_a = {
#if EXP_USE_AUDIO && EXP_USE_MIC
    .mic_state = "starting",
#else
    .mic_state = "off",
#endif
#if EXP_USE_AUDIO && EXP_USE_SPEAKER
    .spk_state = "starting",
#else
    .spk_state = "off",
#endif
#if EXP_USE_MOTOR
    .motor_state = "starting",
#else
    .motor_state = "off",
#endif
    .last_cue = -1,
};
static int64_t s_last_mic_us = -1;

static void set_state(const char **field, const char *state)
{
    portENTER_CRITICAL(&s_lock);
    *field = state;
    portEXIT_CRITICAL(&s_lock);
}

void dev_audio_mark_skipped(bool audio, bool motor)
{
    if (audio) {
        set_state(&s_a.mic_state, "skipped (crash guard)");
        set_state(&s_a.spk_state, "skipped (crash guard)");
        ESP_LOGW(TAG, "audio init skipped: it was running when the previous boot ended abnormally (power-cycle to retry)");
    }
    if (motor) {
        set_state(&s_a.motor_state, "skipped (crash guard)");
        ESP_LOGW(TAG, "motor init skipped: it was running when the previous boot ended abnormally (power-cycle to retry)");
    }
}

/* ---- Codec tasks -------------------------------------------------------------------------------------------------- */
#if EXP_USE_AUDIO
#if EXP_USE_MIC
static esp_codec_dev_handle_t s_mic;
#endif
#if EXP_USE_SPEAKER
static esp_codec_dev_handle_t s_spk;
static QueueHandle_t s_cue_q;
#endif

#if EXP_USE_MIC
static void mic_task(void *arg)
{
    (void)arg;
    int16_t *st = heap_caps_malloc(sizeof(int16_t) * 2 * DEV_MIC_FRAMES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!st) {
        ESP_LOGE(TAG, "no memory for the microphone buffer");
        set_state(&s_a.mic_state, "init failed");
        vTaskDelete(NULL);
        return;
    }
    int16_t mono[DEV_MIC_OUT_N];
    int consecutive_errors = 0;
    for (;;) {
        const int r = esp_codec_dev_read(s_mic, st, (int)(sizeof(int16_t) * 2 * DEV_MIC_FRAMES));
        if (r != ESP_CODEC_DEV_OK) {
            portENTER_CRITICAL(&s_lock);
            ++s_a.mic_errors;
            if (++consecutive_errors >= 3) s_a.mic_state = "read failed";
            portEXIT_CRITICAL(&s_lock);
            if (consecutive_errors == 3) ESP_LOGW(TAG, "microphone read failed (codec error %d)", r);
            vTaskDelay(pdMS_TO_TICKS(MIC_ERR_BACKOFF_MS));
            continue;
        }
        consecutive_errors = 0;
        dev_pcm_info_t info;
        const int n = dev_pcm_decimate(st, DEV_MIC_FRAMES, mono, &info);
        portENTER_CRITICAL(&s_lock);
        memcpy(s_a.pcm, mono, (size_t)n * sizeof(int16_t));
        s_a.pcm_count = (uint16_t)n;
        s_a.peak_l = info.peak_l;
        s_a.peak_r = info.peak_r;
        s_a.mic_slot = info.used;
        ++s_a.mic_reads;
        s_a.mic_state = "ok";
        s_last_mic_us = esp_timer_get_time();
        portEXIT_CRITICAL(&s_lock);
#if EXP_VOICE
        dev_voice_feed_stereo(st, DEV_MIC_FRAMES);       /* 48 kHz stereo in; the speech path filters and decimates itself */
#endif
    }
}
#endif

#if EXP_USE_SPEAKER
static void spk_task(void *arg)
{
    (void)arg;
    int16_t *pcm = heap_caps_malloc(sizeof(int16_t) * 2 * SPK_CHUNK_FRAMES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!pcm) {
        ESP_LOGE(TAG, "no memory for the speaker buffer");
        set_state(&s_a.spk_state, "init failed");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        int cue = 0;
        if (xQueueReceive(s_cue_q, &cue, portMAX_DELAY) != pdTRUE) continue;
        dev_tone_t tone;
        if (!dev_tone_start(&tone, cue)) continue;
        portENTER_CRITICAL(&s_lock);
        ++s_a.spk_cues;
        s_a.last_cue = cue;
        portEXIT_CRITICAL(&s_lock);
#if EXP_VOICE
        dev_voice_speaker_active(true);                 /* the microphone is fed silence while a cue plays */
#endif
        while (tone.active) {
            int n = dev_tone_render(&tone, pcm, SPK_CHUNK_FRAMES, EXP_SPK_AMP);
            if (n <= 0) break;
            if (n < SPK_CHUNK_FRAMES) memset(&pcm[2 * n], 0, sizeof(int16_t) * 2 * (size_t)(SPK_CHUNK_FRAMES - n));
            const int w = esp_codec_dev_write(s_spk, pcm, (int)(sizeof(int16_t) * 2 * SPK_CHUNK_FRAMES));
            if (w != ESP_CODEC_DEV_OK) {
                portENTER_CRITICAL(&s_lock);
                ++s_a.spk_errors;
                s_a.spk_state = "write failed";
                portEXIT_CRITICAL(&s_lock);
                break;                                    /* abandon this cue; the next one tries again */
            }
            set_state(&s_a.spk_state, "ok");
        }
#if EXP_VOICE
        dev_voice_speaker_active(false);                /* plus a short tail (DEV_VHOLD_TAIL_MS) */
#endif
    }
}
#endif

#if EXP_USE_MIC
static void mic_open(esp_codec_dev_sample_info_t *info)
{
    s_mic = bsp_audio_codec_microphone_init();
    if (!s_mic) {
        ESP_LOGE(TAG, "ES8311 microphone device could not be created");
        set_state(&s_a.mic_state, "init failed");
        return;
    }
    int r = esp_codec_dev_open(s_mic, info);
    if (r == ESP_CODEC_DEV_OK) r = esp_codec_dev_set_in_gain(s_mic, EXP_MIC_GAIN_DB);
    if (r != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "microphone open/gain failed (codec error %d)", r);
        set_state(&s_a.mic_state, "init failed");
    } else if (xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 4, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "microphone task could not be created");
        set_state(&s_a.mic_state, "no task");
    } else {
        ESP_LOGI(TAG, "microphone running: 48 kHz stereo -> 16 kHz mono, gain %.0f dB, chunks of %d samples",
                 (double)EXP_MIC_GAIN_DB, DEV_MIC_OUT_N);
    }
}
#endif

#if EXP_USE_SPEAKER
static void spk_open(esp_codec_dev_sample_info_t *info)
{
    s_spk = bsp_audio_codec_speaker_init();
    if (!s_spk) {
        ESP_LOGE(TAG, "ES8311 speaker device could not be created");
        set_state(&s_a.spk_state, "init failed");
        return;
    }
    int q = esp_codec_dev_open(s_spk, info);
    if (q == ESP_CODEC_DEV_OK) q = esp_codec_dev_set_out_vol(s_spk, EXP_SPK_VOLUME);
    s_cue_q = xQueueCreate(CUE_QUEUE_LEN, sizeof(int));
    if (q != ESP_CODEC_DEV_OK || !s_cue_q) {
        ESP_LOGE(TAG, "speaker open/volume/queue failed (codec error %d)", q);
        set_state(&s_a.spk_state, "init failed");
    } else if (xTaskCreatePinnedToCore(spk_task, "spk", 4096, NULL, 3, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "speaker task could not be created");
        set_state(&s_a.spk_state, "no task");
    } else {
        ESP_LOGI(TAG, "speaker ready: volume %d, cue amplitude %d (nothing plays until the app asks for a cue)",
                 EXP_SPK_VOLUME, EXP_SPK_AMP);
    }
}
#endif

void dev_audio_open_codecs(void)
{
#if EXP_USE_MIC || EXP_USE_SPEAKER
    /* Same configuration as the official audio_loopback example: 48 kHz, 16 bit, stereo on both devices. */
    esp_codec_dev_sample_info_t info = {
        .sample_rate = BSP_AUDIO_DEFAULT_SAMPLE_RATE,
        .bits_per_sample = BSP_AUDIO_DEFAULT_BITS_PER_SAMPLE,
        .channel = BSP_AUDIO_DEFAULT_CHANNELS,
        .channel_mask = 0,
    };
#endif
#if EXP_USE_MIC
    mic_open(&info);
#endif
#if EXP_USE_SPEAKER
    spk_open(&info);
#endif
}
#else
void dev_audio_open_codecs(void) {}
#endif

void dev_audio_play(int cue)
{
#if EXP_USE_AUDIO && EXP_USE_SPEAKER
    if (!s_cue_q) return;
    if (xQueueSend(s_cue_q, &cue, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_lock);
        ++s_a.spk_dropped;
        portEXIT_CRITICAL(&s_lock);
    }
#else
    (void)cue;
#endif
}

/* ---- Motor -------------------------------------------------------------------------------------------------------- */
#if EXP_USE_MOTOR
static esp_timer_handle_t s_haptic_timer;
static dev_haptic_gate_t s_gate;     /* touched only by the UI thread (dev_haptic_pulse) */
static bool s_motor_ready;

static void haptic_off_cb(void *arg)
{
    (void)arg;
    (void)bsp_motor_set_strength(0);
}

void dev_motor_open(void)
{
    esp_err_t e = bsp_motor_init();
    if (e == ESP_OK) e = bsp_motor_set_strength(0);          /* known state first: off */
    const esp_timer_create_args_t args = {.callback = haptic_off_cb, .arg = NULL, .dispatch_method = ESP_TIMER_TASK,
                                          .name = "haptic_off", .skip_unhandled_events = false};
    if (e == ESP_OK) e = esp_timer_create(&args, &s_haptic_timer);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "motor init failed: %s", esp_err_to_name(e));
        set_state(&s_a.motor_state, "init failed");
        (void)bsp_motor_set_strength(0);
        return;
    }
    s_motor_ready = true;
    set_state(&s_a.motor_state, "ok");
    ESP_LOGI(TAG, "motor ready: pulses of at most %u ms, %d %% strength, at most %u ms per second", (unsigned)DEV_HAPTIC_MAX_MS,
             EXP_MOTOR_STRENGTH, (unsigned)DEV_HAPTIC_BUDGET_MS);
}

void dev_haptic_pulse(uint16_t ms)
{
    if (!s_motor_ready) return;
    const uint16_t grant = dev_haptic_grant(&s_gate, (uint32_t)(esp_timer_get_time() / 1000), ms);
    if (grant == 0) {
        portENTER_CRITICAL(&s_lock);
        ++s_a.haptic_denied;
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    (void)esp_timer_stop(s_haptic_timer);                     /* a running pulse is simply extended */
    if (bsp_motor_set_strength(EXP_MOTOR_STRENGTH) != ESP_OK ||
        esp_timer_start_once(s_haptic_timer, (uint64_t)grant * 1000U) != ESP_OK) {
        (void)bsp_motor_set_strength(0);                      /* never leave the motor running without its off-timer */
        portENTER_CRITICAL(&s_lock);
        ++s_a.motor_errors;
        s_a.motor_state = "write failed";
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    portENTER_CRITICAL(&s_lock);
    ++s_a.haptic_pulses;
    portEXIT_CRITICAL(&s_lock);
}
#else
void dev_motor_open(void) {}
void dev_haptic_pulse(uint16_t ms) { (void)ms; }
#endif

void dev_audio_take(dev_audio_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_a;
    const int64_t t = s_last_mic_us;
    portEXIT_CRITICAL(&s_lock);
    out->mic_ok = t >= 0 && (esp_timer_get_time() - t) / 1000 < DEV_MIC_FRESH_MS && out->mic_state &&
                  strcmp(out->mic_state, "ok") == 0;
}

bool dev_audio_mic_ready(void)
{
    dev_audio_t a;
    dev_audio_take(&a);
    return a.mic_ok;
}
