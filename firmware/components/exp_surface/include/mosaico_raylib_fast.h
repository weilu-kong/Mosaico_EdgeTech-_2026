// SPDX-License-Identifier: Apache-2.0
// Route B drawing shim. The Experience core (main/exp_*.c) draws only through the handful of
// Raylib-style calls declared here. On the device this header REPLACES the engine's
// mosaico_raylib_fast.h, so the firmware needs neither esp-gsp, raylib, LVGL nor the Iris/Vibe
// stack. Semantics mirror raylib-lite-engine (rgb565 packing, alpha blend, triangle edge test,
// scissor); tests/surface_equivalence compares the two pixel for pixel on the Host.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Color { uint8_t r, g, b, a; } Color;
typedef struct Vector2 { float x, y; } Vector2;

#ifdef __cplusplus
extern "C" {
#endif

void MosaicoFastBeginDrawing(void);
bool MosaicoFastFrameAvailable(void);
void MosaicoFastEndDrawing(void);
void MosaicoFastBeginScissorMode(int x, int y, int width, int height);
void MosaicoFastEndScissorMode(void);
void MosaicoFastDrawPixel(int x, int y, Color color);
void MosaicoFastDrawRectangle(int x, int y, int width, int height, Color color);
void MosaicoFastDrawRectangleLines(int x, int y, int width, int height, Color color);
void MosaicoFastDrawTriangle(Vector2 a, Vector2 b, Vector2 c, Color color);

#ifdef __cplusplus
}
#endif

#define BeginDrawing MosaicoFastBeginDrawing
#define EndDrawing MosaicoFastEndDrawing
#define BeginScissorMode MosaicoFastBeginScissorMode
#define EndScissorMode MosaicoFastEndScissorMode
#define DrawPixel MosaicoFastDrawPixel
#define DrawRectangle MosaicoFastDrawRectangle
#define DrawRectangleLines MosaicoFastDrawRectangleLines
#define DrawTriangle MosaicoFastDrawTriangle
