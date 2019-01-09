/*
 * ----------------------------------------------------------------------------
 * Copyright 2018 ARM Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ----------------------------------------------------------------------------
 */
#ifndef __PT_BLE_H__
#define __PT_BLE_H__

#include "compat.h"
#include "inttypes.h"
#include "stdlib.h"
#include "glib.h"

void ble_start(const char* postfix,
               const char *adapter,
               const char *address,
               int clear_device_cache,
               int extended_discovery_mode);

int ble_read_characteristic(const char *characteristic_path,
                            uint8_t    *data,
                            size_t     *size);

int ble_read_characteristic_async(char *characteristic_path,
                                  char *device_id,
                                  int srvc,
                                  int ch);

int ble_write_characteristic(const char    *characteristic_path,
                             const uint8_t *data,
                             size_t         size);

gboolean pt_ble_pt_ready(gpointer data);
gboolean pt_ble_graceful_shutdown(gpointer data);
gboolean pt_ble_g_main_quit_loop(gpointer data);

struct async_read_userdata {
    char *device_id;
    int srvc;
    int ch;
};

extern volatile int global_keep_running;

#endif /* PT_BLE_H */
