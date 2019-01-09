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

#ifndef EDGE_CLIENT_CONFIG_H
#define EDGE_CLIENT_CONFIG_H

#include <stdbool.h>
#include "pt-client-2/pt_api.h"

const char *client_config_get_cpu_thermal_zone_file_path();
const char *client_config_get_protocol_translator_name();
bool client_config_create_devices(connection_id_t connection_id, const char *endpoint_postfix);
void client_config_create_cpu_temperature_device(connection_id_t connection_id,
                                                 const char *device_id);
void client_config_create_device_with_userdata(connection_id_t connection_id,
                                               const char *device_id,
                                               pt_userdata_t *userdata);

void client_config_create_device_with_parameters(connection_id_t connection_id,
                                                 const char *device_id,
                                                 pt_userdata_t *userdata,
                                                 const char *manufacturer,
                                                 const char *model_number,
                                                 const char *serial_number,
                                                 const char *device_type);
void client_config_free();
bool client_config_parse(const char *filename);

#endif /* EDGE_CLIENT_CONFIG_H */
