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
#ifndef __PT_EDGE_H__
#define __PT_EDGE_H__

#include "compat.h"
#include "devices.h"
#include <pt-client-2/pt_api.h>

/**
 * \brief Structure to pass the protocol translator initialization
 * data to the protocol translator API.
 */
typedef struct protocol_translator_api_start_ctx {
    const char *socket_path;
    const char *name;
} protocol_translator_api_start_ctx_t;

#define IPSO_OID_BLE_INTROSPECT 18131
#define IPSO_OID_BLE_SERVICE 18135

bool
edge_add_resource(const char *device_id,
                  const uint16_t object_id,
                  const uint16_t instance_id,
                  const uint16_t resource_id,
                  const Lwm2mResourceType type,
                  const uint8_t operations,
                  const uint8_t *value,
                  const uint32_t value_size);
void
edge_set_resource_value(const char *device_id,
                        const uint16_t object_id,
                        const uint16_t instance_id,
                        const uint16_t resource_id,
                        const uint8_t *value,
                        const uint32_t value_size);

void unregister_devices();
void write_value_failure(const char* device_id, void *userdata);
void write_value_success(const char* device_id, void *userdata);
void start_protocol_translator_api(protocol_translator_api_start_ctx_t *ctx);
void stop_protocol_translator_api();
void stop_protocol_translator_api_thread();

bool edge_is_connected();
void edge_write_values();
bool edge_device_exists(const char *device_id);
void edge_register_device(const char *device_id);
void pt_edge_del_device(struct ble_device *ble_de);
// Returns true if the call succeeded and false otherwise.
bool edge_unregister_device(struct ble_device *dev, bool remove_device_context);
connection_id_t edge_get_connection_id();
bool edge_create_device(const char *device_id,
                        const char *manufacturer,
                        const char *model_number,
                        const char *serial_number,
                        const char *device_type,
                        uint32_t lifetime,
                        pt_resource_callback reboot_callback);
#endif /* PT_EDGE_H */
