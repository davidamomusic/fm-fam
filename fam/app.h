/* SPDX-License-Identifier: MPL-2.0 */
/* FM-FAM: a drum synth and sequencer for the M-VAVE FM-1. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FM-FAM"
#define APP_VERSION "0.2.5"
#define APP_SPLASH_RGB RGB(255, 140, 30)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz (the step clock is per sample) */

/* the store: the panel calibration (the platform's), the settings and sound, eleven banks of sixteen patterns, the user
 * presets of five kinds (one object each), and four projects of four objects (the sound, then banks A..C) */
enum { OBJ_SOUND, OBJ_PANEL, OBJ_BANK0, OBJ_BANK1, OBJ_BANK2, OBJ_BANK3, OBJ_BANK4, OBJ_BANK5, OBJ_BANK6, OBJ_BANK7, OBJ_BANK8, OBJ_BANK9, OBJ_BANK10, OBJ_PRE0, OBJ_PRE1, OBJ_PRE2, OBJ_PRE3, OBJ_PRE4, OBJ_PROJ0, OBJ_PROJ1, OBJ_PROJ2, OBJ_PROJ3, OBJ_PROJ4, OBJ_PROJ5, OBJ_PROJ6, OBJ_PROJ7, OBJ_PROJ8, OBJ_PROJ9, OBJ_PROJ10, OBJ_PROJ11, OBJ_PROJ12, OBJ_PROJ13, OBJ_PROJ14, OBJ_PROJ15, OBJ_BT, OBJ_NOBJ };
#define APP_STORE_MAP {0xC0000u, 0xDC000u, 0xC2000u, 0xC4000u, 0xC6000u, 0xC8000u, 0xCA000u, 0xCC000u, 0xCE000u, 0xD0000u, 0xD2000u, 0xD4000u, 0xD6000u, 0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, 0xA5000u, 0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u, 0xAF000u, 0xB1000u, 0xB3000u, 0xB5000u, 0xB7000u, 0xB9000u, 0xBB000u, 0xBD000u, 0xDE000u, 0xFC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* backup and restore over USB (fm1.toml backup = true; the mirror page's Projects panel and tools/fm1_backup.py): the
 * sound, the eleven banks, the user presets and the four projects (16 objects), never the panel's calibration (the unit's)
 * or what Bluetooth keeps. A project's four objects are OBJ_PROJ0 + 4 * slot + 0..3 */
#define APP_BACKUP_OBJS {OBJ_SOUND, OBJ_BANK0, OBJ_BANK1, OBJ_BANK2, OBJ_BANK3, OBJ_BANK4, OBJ_BANK5, OBJ_BANK6, OBJ_BANK7, OBJ_BANK8, OBJ_BANK9, OBJ_BANK10, OBJ_PRE0, OBJ_PRE1, OBJ_PRE2, OBJ_PRE3, OBJ_PRE4, OBJ_PROJ0, OBJ_PROJ1, OBJ_PROJ2, OBJ_PROJ3, OBJ_PROJ4, OBJ_PROJ5, OBJ_PROJ6, OBJ_PROJ7, OBJ_PROJ8, OBJ_PROJ9, OBJ_PROJ10, OBJ_PROJ11, OBJ_PROJ12, OBJ_PROJ13, OBJ_PROJ14, OBJ_PROJ15}
#define APP_BACKUP_MAGIC 0x424D4146u     /* "FAMB" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces all saved data"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* Bluetooth (fm1.toml ble, bt_audio): what the radio keeps and the setting live in one object of the app's own (OBJ_BT);
 * the radio starts at power-on only if the player left it on */
#define APP_BLE_STORE
#define APP_BLE_AT_BOOT (bt_boot())
int bt_boot(void);
int app_ble_load(struct plat_ble_rec *r);
int app_ble_save(const struct plat_ble_rec *r);

/* the screen mirror (fm1.toml mirror = true): what a slow link dropped is drawn again */
#define APP_REDRAW
void app_redraw(void);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
