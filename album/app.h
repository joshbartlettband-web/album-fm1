/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album 01: an fm1-base firmware. This header is the app's half of the contract with the platform
 * (base/device/unity.c, base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "ALBUM"
#define APP_TAGLINE "01"
#define APP_SPLASH_RGB RGB(232, 40, 52)
#define APP_PALETTE 3u                   /* gfx.c's RED theme for the few words on screen */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 48u)       /* the screen is drawn in strips of 48 rows: the photo needs no more */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */
#define APP_BTDEV                        /* Bluetooth MIDI devices: another FM-1 to link to (lib/btdev.h) */
#define APP_BLE_AT_BOOT (!album_ble_off())
int album_ble_off(void);

/* the second core: started at boot unless the last session's ended badly (cpu1.c); APP_CSTART holds it first thing
 * after a reset, APP_FAULT gives it up when anything crashes */
#ifndef PLAT_HOST
#define APP_CSTART
void app_cstart(void);
#define APP_FAULT
#define APP_SPLASH                       /* the first screen: the boot photo and ALBUM 01 (the intro's first frame) */
void app_splash(void);
#endif
#define APP_OVERLOAD                     /* the render near its limit: fewer voices */
void app_overload(void);

/* the store: the app's settings, the panel calibration (the platform's), Bluetooth's address and pairing */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
