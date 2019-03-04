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

#include <string.h>

#include "common/integer_length.h"
#include "pt-client/pt_api.h"
#include "pt-client/pt_device_object.h"
#include "examples-common/client_config.h"
#include "examples-common/ipso_objects.h"
#include "device-interface/thermal_zone.h"
#include "mbed-trace/mbed_trace.h"
#include <stdio.h>
#define TRACE_GROUP "clnt-example"

#define LIFETIME           86400
#define THERMOSTAT_PREFIX  "thermostat"
#define THERMOMETER_PREFIX "thermometer"

pt_device_t *client_config_create_device_with_userdata(const char *device_id,
                                                       const char *endpoint_postfix,
                                                       pt_device_userdata_t *userdata)
{
    pt_status_t status = PT_STATUS_SUCCESS;
    char *endpoint_id = malloc(strlen(device_id) + strlen(endpoint_postfix) + 1);
    sprintf(endpoint_id, "%s%s", device_id, endpoint_postfix);
    pt_device_t *device = pt_create_device_with_userdata(endpoint_id, LIFETIME, QUEUE, &status, userdata);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not allocate device structure. status: %d", status);
        return NULL;
    }
    return device;
}

pt_device_t *client_config_create_device(const char *device_id, const char *endpoint_postfix)
{
    return client_config_create_device_with_userdata(device_id, endpoint_postfix, NULL);
}

static void client_config_example_reboot_callback(const pt_resource_t *resource,
                                                  const uint8_t *value,
                                                  const uint32_t value_length,
                                                  void *userdata)
{
    tr_info("Example /3 device reboot resource executed.");
}

void client_config_blink_callback(const pt_resource_t *resource, const uint8_t *value, const uint32_t size, void* userdata)
{
    tr_info("blink_callback,  value %s", value);
}

void client_config_upgrade_callback(const pt_resource_t *resource, const uint8_t *value, const uint32_t size, void* userdata)
{
    tr_info("upgrade_callback,  value %s", value);
}

pt_device_t *client_config_create_cpu_temperature_device(const char *device_id, const char *endpoint_postfix)
{
    pt_device_t *device = NULL;
    /* Check if the themal zone 0 temp file is available */
    if (tzone_has_cpu_thermal_zone() == 1) {
        device = client_config_create_device_with_parameters(device_id,
                                                             endpoint_postfix,
                                                             NULL,      // userdata
                                                             "ARM",     // manufacturer
                                                             "example", // model_number
                                                             "001",     // serial_number
                                                             "example"  // device_type
        );

        pt_object_instance_t *instance_temperature = ipso_create_sensor_object(device, TEMPERATURE_SENSOR, "CEL", NULL);
        ipso_add_min_max_fields(instance_temperature, ipso_reset_min_max_object);
    }
    return device;
}

pt_device_t *client_config_create_device_with_parameters(const char *device_id,
                                                         const char *endpoint_postfix,
                                                         pt_device_userdata_t *userdata,
                                                         const char *manufacturer,
                                                         const char *model_number,
                                                         const char *serial_number,
                                                         const char *device_type)
{
    pt_device_t *device = NULL;
    device = client_config_create_device_with_userdata(device_id, endpoint_postfix, userdata);

    ptdo_device_object_data_t *device_object_data = malloc(sizeof(ptdo_device_object_data_t));
    device_object_data->manufacturer = strdup(manufacturer);
    device_object_data->model_number = strdup(model_number);
    device_object_data->serial_number = strdup(serial_number);
    device_object_data->firmware_version = strdup("N/A");
    device_object_data->hardware_version = strdup("N/A");
    device_object_data->software_version = strdup("N/A");
    device_object_data->device_type = strdup(device_type);
    device_object_data->reboot_callback = client_config_example_reboot_callback;
    device_object_data->factory_reset_callback = NULL;
    device_object_data->reset_error_code_callback = NULL;

    ptdo_initialize_device_object(device, device_object_data);
    /* Free the helper struct */
    free(device_object_data);

    return device;
}


pt_device_list_t *client_config_create_device_list(const char *endpoint_postfix)
{
    int TWO_DEVICES = 2;
    pt_status_t status = PT_STATUS_SUCCESS;
    pt_device_list_t *device_list = malloc(sizeof(pt_device_list_t));
    ns_list_init(device_list);
    const char *thermometer_prefix = THERMOMETER_PREFIX;
    const char *thermostat_prefix = THERMOSTAT_PREFIX;

    /* Create two devices */
    for (int i = 0; i < TWO_DEVICES; i++) {
        pt_device_t *device = NULL;
        if (i % TWO_DEVICES == 0) {
            char *thermometer_id = malloc(strlen(thermometer_prefix) + strlen(endpoint_postfix) + 1);
            sprintf(thermometer_id, "%s%s", thermometer_prefix, endpoint_postfix);
            device = pt_create_device(thermometer_id, LIFETIME, QUEUE, &status);
            if (status != PT_STATUS_SUCCESS) {
                tr_err("Could not allocate device structure. status: %s", status);
                return NULL;
            }
            ipso_create_sensor_object(device, TEMPERATURE_SENSOR, "CEL",  NULL);
        } else {
            char *thermostat_id = malloc(strlen(thermostat_prefix) + strlen(endpoint_postfix) + 1);
            sprintf(thermostat_id, "%s%s", thermostat_prefix, endpoint_postfix);
            device = pt_create_device(thermostat_id, LIFETIME, QUEUE, &status);
            if (status != PT_STATUS_SUCCESS) {
                tr_err("Could not allocate device structure. statis: %s", status);
                return NULL;
            }
            ipso_create_set_point(device, 0, 25);
        }

        pt_device_entry_t *device_entry = malloc(sizeof(pt_device_entry_t));
        device_entry->device = device;
        ns_list_add_to_end(device_list, device_entry);
    }

    return device_list;
}


pt_device_list_t *client_config_create_empty_device_list()
{
    pt_device_list_t *device_list = malloc(sizeof(pt_device_list_t));
    ns_list_init(device_list);

    return device_list;

}

void client_config_add_device_to_config(pt_device_list_t *device_list, pt_device_t *device)
{
    pt_device_entry_t *device_entry = malloc(sizeof(pt_device_entry_t));
    device_entry->device = device;
    ns_list_add_to_end(device_list, device_entry);
}


void client_config_free()
{
    tzone_free();
}
