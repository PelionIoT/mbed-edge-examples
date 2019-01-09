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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <string.h>
#include <stdlib.h>

#include "common/integer_length.h"
#include "pt-client-2/pt_api.h"
#include "pt-client-2/pt_device_object.h"
#include "examples-common-2/client_config.h"
#include "examples-common-2/ipso_objects.h"
#include "device-interface/thermal_zone.h"
#include "mbed-trace/mbed_trace.h"
#include <stdio.h>
#define TRACE_GROUP "clnt-example"

#define LIFETIME           86400
#define THERMOSTAT_PREFIX  "thermostat"
#define THERMOMETER_PREFIX "thermometer"

void client_config_create_device_with_userdata(connection_id_t connection_id,
                                               const char *device_id,
                                               pt_userdata_t *userdata)
{
    pt_status_t status = pt_device_create_with_userdata(connection_id, device_id, LIFETIME, QUEUE, userdata);
    if (status != PT_STATUS_SUCCESS) {
        tr_warn("Could not create a device '%s' - error code: %d", device_id, (int32_t) status);
    }
}

void client_config_create_device(connection_id_t connection_id, const char *device_id)
{
    client_config_create_device_with_userdata(connection_id, device_id, NULL);
}

static pt_status_t client_config_example_reboot_callback(const connection_id_t connection_id,
                                                         const char *device_id,
                                                         const uint16_t object_id,
                                                         const uint16_t object_instance_id,
                                                         const uint16_t resource_id,
                                                         const uint8_t operation,
                                                         const uint8_t *value,
                                                         const uint32_t value_length,
                                                         void *userdata)
{
    tr_info("Example /3 device reboot resource executed.");
    return PT_STATUS_SUCCESS;
}

void client_config_blink_callback(const connection_id_t connection_id,
                                  const char *device_id,
                                  const uint16_t object_id,
                                  const uint16_t object_instance_id,
                                  const uint16_t resource_id,
                                  const uint8_t operation,
                                  const uint8_t *value,
                                  const uint32_t size,
                                  void *userdata)
{
    tr_info("blink_callback,  value %s", value);
}

void client_config_upgrade_callback(connection_id_t connection_id,
                                    const char *device_id,
                                    const uint16_t object_id,
                                    const uint16_t object_instance_id,
                                    const uint16_t resource_id,
                                    const uint8_t *value,
                                    const uint32_t size,
                                    void *userdata)
{
    tr_info("upgrade_callback,  value %s", value);
}

void client_config_create_cpu_temperature_device(connection_id_t connection_id, const char *device_id)
{
    /* Check if the themal zone 0 temp file is available */
    if (tzone_has_cpu_thermal_zone() == 1) {
        client_config_create_device_with_parameters(connection_id,
                                                    device_id,
                                                    NULL,      // userdata
                                                    "ARM",     // manufacturer
                                                    "example", // model_number
                                                    "001",     // serial_number
                                                    "example"  // device_type
        );

        ipso_create_sensor_object(connection_id, device_id, TEMPERATURE_SENSOR, 0, "CEL", NULL);

        ipso_add_min_max_fields(connection_id,
                                device_id,
                                TEMPERATURE_SENSOR,
                                0,
                                ipso_reset_min_max_object);
    }
}

void client_config_create_device_with_parameters(connection_id_t connection_id,
                                                 const char *device_id,
                                                 pt_userdata_t *userdata,
                                                 const char *manufacturer,
                                                 const char *model_number,
                                                 const char *serial_number,
                                                 const char *device_type)
{
    client_config_create_device_with_userdata(connection_id, device_id, userdata);

    ptdo_device_object_data_t device_object_data = {0};
    device_object_data.manufacturer = manufacturer;
    device_object_data.model_number = model_number;
    device_object_data.serial_number = serial_number;
    device_object_data.firmware_version = "N/A";
    device_object_data.hardware_version = "N/A";
    device_object_data.software_version = "N/A";
    device_object_data.device_type = device_type;
    device_object_data.reboot_callback = client_config_example_reboot_callback;
    device_object_data.factory_reset_callback = NULL;
    device_object_data.reset_error_code_callback = NULL;

    ptdo_initialize_device_object(connection_id, device_id, &device_object_data);
}

bool client_config_create_devices(connection_id_t connection_id, const char *endpoint_postfix)
{
    int TWO_DEVICES = 2;
    pt_status_t status = PT_STATUS_SUCCESS;
    const char *thermometer_prefix = THERMOMETER_PREFIX;
    const char *thermostat_prefix = THERMOSTAT_PREFIX;
    char device_id[128];
    /* Create two devices */
    for (int i = 0; i < TWO_DEVICES; i++) {
        if (i % TWO_DEVICES == 0) {
            snprintf(device_id, 128, "%s%s", thermometer_prefix, endpoint_postfix);
            status = pt_device_create(connection_id, device_id, LIFETIME, NONE);
            if (status != PT_STATUS_SUCCESS) {
                tr_warn("Could not create a device '%s' - error code: %d", device_id, (int32_t) status);
                return false;
            }
            ipso_create_sensor_object(connection_id, device_id, TEMPERATURE_SENSOR, 0, "CEL", NULL);
        } else {
            snprintf(device_id, 128, "%s%s", thermostat_prefix, endpoint_postfix);
            status = pt_device_create(connection_id, device_id, LIFETIME, NONE);
            if (status != PT_STATUS_SUCCESS) {
                tr_warn("Could not create a device '%s' - error code: %d", device_id, (int32_t) status);
                return false;
            }
            ipso_create_set_point(connection_id, device_id, 0, 25);
        }
    }

    return true;
}

void client_config_free()
{
    tzone_free();
}
