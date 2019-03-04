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

#include <float.h>
#include <string.h>

#include <stdlib.h>
#include "examples-common-2/ipso_objects.h"
#include "common/constants.h"
#include "common/integer_length.h"
#include "pt-client-2/pt_api.h"
#include "byte-order/byte_order.h"
#include "mbed-trace/mbed_trace.h"

#define TRACE_GROUP "ipso-objects"

void ipso_add_min_max_fields(connection_id_t connection_id,
                             const char *device_id,
                             const uint16_t object_id,
                             const uint16_t object_instance_id,
                             pt_resource_callback reset_callback)
{
    pt_status_t status = PT_STATUS_SUCCESS;

    float min_default = FLT_MAX; // Set minimum measured on default to max float
    uint8_t *min_default_data = malloc(sizeof(float));
    if (min_default_data == NULL) {
        tr_err("Could not allocate min_default_data");
        return;
    }
    convert_float_value_to_network_byte_order(min_default, min_default_data);

    float max_default = -FLT_MAX; // Set maximum measured on default to min float
    uint8_t *max_default_data = malloc(sizeof(float));
    if (max_default_data == NULL) {
        tr_err("Could not allocate max_default_data");
        return;
    }

    convert_float_value_to_network_byte_order(max_default, max_default_data);

    status = pt_device_add_resource(connection_id,
                                    device_id,
                                    object_id,
                                    object_instance_id,
                                    MIN_MEASURED_VALUE,
                                    LWM2M_FLOAT,
                                    min_default_data,
                                    sizeof(float),
                                    free);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("A Could not create resource with id (%d) to the object_instance (%d).",
               MIN_MEASURED_VALUE,
               object_instance_id);
    }

    status = pt_device_add_resource(connection_id,
                                    device_id,
                                    object_id,
                                    object_instance_id,
                                    MAX_MEASURED_VALUE,
                                    LWM2M_FLOAT,
                                    max_default_data,
                                    sizeof(float),
                                    free);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("B Could not create resource with id (%d) to the object_instance (%d).",
               MAX_MEASURED_VALUE,
               object_instance_id);
    }

    status = pt_device_add_resource_with_callback(connection_id,
                                                  device_id,
                                                  object_id,
                                                  object_instance_id,
                                                  RESET_MIN_MAX_MEASURED_VALUES,
                                                  LWM2M_OPAQUE,
                                                  OPERATION_EXECUTE,
                                                  NULL,
                                                  0,
                                                  NULL,
                                                  reset_callback);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("C Could not create resource with id (%d) to the object_instance (%d).",
               RESET_MIN_MAX_MEASURED_VALUES,
               object_instance_id);
    }
}

pt_status_t ipso_add_resource(connection_id_t connection_id,
                              const char *device_id,
                              enum IPSO_OBJECTS object_id,
                              const uint16_t object_instance_id,
                              enum IPSO_RESOURCES resource_id,
                              Lwm2mResourceType type,
                              uint8_t operations,
                              uint8_t *value,
                              const uint32_t value_size,
                              pt_resource_callback callback)
{

    // Add resource
    pt_status_t status = pt_device_add_resource_with_callback(connection_id,
                                                              device_id,
                                                              object_id,
                                                              object_instance_id,
                                                              resource_id,
                                                              type,
                                                              operations,
                                                              value,
                                                              value_size,
                                                              free,
                                                              callback);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("D Could not create a resource with id %d ", object_id);
    }

    return status;
}

void ipso_create_custom_object(connection_id_t connection_id,
                               const char *device_id,
                               enum IPSO_OBJECTS object_id,
                               const uint16_t object_instance_id,
                               enum IPSO_RESOURCES resource_id,
                               Lwm2mResourceType type,
                               uint8_t operations,
                               void *value,
                               const uint32_t value_size,
                               pt_resource_callback callback)
{
    uint8_t *sensor_data = malloc(value_size);
    if (sensor_data == NULL) {
        tr_err("Could not allocate sensor_data");
        return;
    }

    memcpy(sensor_data, value, value_size);

    ipso_add_resource(connection_id,
                      device_id,
                      object_id,
                      object_instance_id,
                      resource_id,
                      type,
                      operations,
                      sensor_data,
                      value_size,
                      callback);
}

void ipso_create_thermometer(const connection_id_t connection_id,
                             const char *device_id,
                             const uint16_t object_instance_id,
                             const float temperature,
                             bool optional_fields,
                             pt_resource_callback reset_thermometer_callback)
{

    uint8_t *temperature_data = malloc(sizeof(float));
    convert_float_value_to_network_byte_order(temperature, (uint8_t *) temperature_data);

    pt_status_t status = pt_device_add_resource(connection_id,
                                                device_id,
                                                TEMPERATURE_SENSOR,
                                                object_instance_id,
                                                SENSOR_VALUE,
                                                LWM2M_FLOAT,
                                                temperature_data,
                                                sizeof(float),
                                                free);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("E Could not create a resource with id (%d) to the object_instance (%d).",
               SENSOR_VALUE,
               object_instance_id);
    }

    // Add units resource
    status = pt_device_add_resource(connection_id,
                                    device_id,
                                    TEMPERATURE_SENSOR,
                                    object_instance_id,
                                    SENSOR_UNITS,
                                    LWM2M_STRING,
                                    (uint8_t *) "Cel",
                                    strlen("Cel"),
                                    NULL);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("F Could not create a resource with id (%d) to the object_instance (%d).",
               SENSOR_UNITS, object_instance_id);
    }

    if (optional_fields) {
        if (reset_thermometer_callback) {
            ipso_add_min_max_fields(connection_id, device_id, TEMPERATURE_SENSOR, object_instance_id, reset_thermometer_callback);
        } else {
            ipso_add_min_max_fields(connection_id, device_id, TEMPERATURE_SENSOR, object_instance_id, ipso_reset_min_max_object);
        }
    }
}

void ipso_create_sensor_object(connection_id_t connection_id,
                               const char *device_id,
                               enum IPSO_OBJECTS sensor_id,
                               const uint16_t object_instance_id,
                               const char *sensor_units,
                               const char *sensor_type)
{
    uint8_t *sensor_data = malloc(sizeof(float));
    if (sensor_data == NULL) {
        tr_err("Could not allocate sensor_data");
        return;
    }
    convert_float_value_to_network_byte_order(0.0, sensor_data);

    ipso_add_resource(connection_id,
                      device_id,
                      sensor_id,
                      object_instance_id,
                      SENSOR_VALUE,
                      LWM2M_FLOAT,
                      OPERATION_READ,
                      sensor_data,
                      (uint32_t) sizeof(float),
                      NULL);

    if (sensor_units != NULL ){
        ipso_add_resource(connection_id,
                          device_id,
                          sensor_id,
                          object_instance_id,
                          SENSOR_UNITS,
                          LWM2M_STRING,
                          OPERATION_READ,
                          (uint8_t *) strdup(sensor_units),
                          strlen((char *) sensor_units),
                          NULL);
    }

    if (sensor_type != NULL) {
        ipso_add_resource(connection_id,
                          device_id,
                          sensor_id,
                          object_instance_id,
                          SENSOR_TYPE,
                          LWM2M_STRING,
                          OPERATION_READ,
                          (uint8_t *) strdup(sensor_type),
                          strlen((char *) sensor_type),
                          NULL);
    }
}

pt_status_t ipso_reset_min_max_object(const connection_id_t connection_id,
                                      const char *device_id,
                                      const uint16_t object_id,
                                      const uint16_t object_instance_id,
                                      const uint16_t resource_id,
                                      const uint8_t operation,
                                      const uint8_t *value,
                                      const uint32_t value_len,
                                      void *userdata)
{
    tr_info("Resetting min and max to default values on '%s'.", device_id);

    float min_default = FLT_MAX; // Set minimum measured on reset to max float
    uint8_t *min_default_data = malloc(sizeof(float));
    if (min_default_data == NULL) {
        tr_err("Could not get resource value buffer");
        return PT_STATUS_ALLOCATION_FAIL;
    }
    convert_float_value_to_network_byte_order(min_default, min_default_data);
    pt_device_set_resource_value(connection_id,
                                 device_id,
                                 object_id,
                                 object_instance_id,
                                 MIN_MEASURED_VALUE,
                                 min_default_data,
                                 sizeof(float),
                                 free);

    float max_default = -FLT_MAX; // Set maximum measured on reset to min float
    uint8_t *max_default_data = malloc(sizeof(float));
    if (max_default_data == NULL) {
        tr_err("Could not get resource value buffer");
        return PT_STATUS_ALLOCATION_FAIL;
    }
    convert_float_value_to_network_byte_order(max_default, max_default_data);
    pt_device_set_resource_value(connection_id,
                                 device_id,
                                 object_id,
                                 object_instance_id,
                                 MAX_MEASURED_VALUE,
                                 max_default_data,
                                 sizeof(float),
                                 free);
    return PT_STATUS_SUCCESS;
}

pt_status_t ipso_update_min_max_fields(const connection_id_t connection_id,
                                       const char *device_id,
                                       const uint16_t object_id,
                                       const uint16_t object_instance_id,
                                       const float new_value)
{
    tr_debug("Updating min and max values on '%s/%d/%d'.", device_id, object_id, object_instance_id);

    float temp_value = 0.0f;

    // Get the current min and max values
    uint8_t *min_current_data = NULL;
    uint32_t min_size = 0;
    uint8_t *max_current_data = NULL;
    uint32_t max_size = 0;
    pt_status_t status = PT_STATUS_ERROR;

    status = pt_device_get_resource_value(connection_id,
                                 device_id,
                                 object_id,
                                 object_instance_id,
                                 MIN_MEASURED_VALUE,
                                 &min_current_data,
                                 &min_size);

    if (status != PT_STATUS_SUCCESS || min_current_data == NULL) {
        tr_error("Cannot update min value, resource missing? (error = %d)", status);
        return status;
    }

    status = pt_device_get_resource_value(connection_id,
                                          device_id,
                                          object_id,
                                          object_instance_id,
                                          MAX_MEASURED_VALUE,
                                          &max_current_data,
                                          &max_size);
    if (status != PT_STATUS_SUCCESS || max_current_data == NULL) {
        tr_error("Cannot update min value, resource missing? (error = %d)", status);
        return status;
    }

    convert_value_to_host_order_float(min_current_data, &temp_value);
    if (new_value < temp_value) {
        tr_debug("Setting new min value");
        uint8_t *min_data = malloc(sizeof(float));
        if (min_data == NULL) {
            tr_err("Could not get resource value buffer");
            return PT_STATUS_ALLOCATION_FAIL;
        }
        convert_float_value_to_network_byte_order(new_value, min_data);
        (void)pt_device_set_resource_value(connection_id,
                                           device_id,
                                           object_id,
                                           object_instance_id,
                                           MIN_MEASURED_VALUE,
                                           min_data,
                                           sizeof(float),
                                           free);
    }


    convert_value_to_host_order_float(max_current_data, &temp_value);
    if (new_value > temp_value) {
        tr_debug("Setting new max value");
        uint8_t *max_data = malloc(sizeof(float));
        if (max_data == NULL) {
            tr_err("Could not get resource value buffer");
            return PT_STATUS_ALLOCATION_FAIL;
        }
        convert_float_value_to_network_byte_order(new_value, max_data);
        (void)pt_device_set_resource_value(connection_id,
                                           device_id,
                                           object_id,
                                           object_instance_id,
                                           MAX_MEASURED_VALUE,
                                           max_data,
                                           sizeof(float),
                                           free);
    }

    return PT_STATUS_SUCCESS;
}

pt_status_t ipso_write_set_point_value(const connection_id_t connection_id,
                                const char *device_id,
                                const uint16_t object_id,
                                const uint16_t object_instance_id,
                                const uint16_t resource_id,
                                const uint8_t operation,
                                const uint8_t *value,
                                const uint32_t value_size,
                                void *userdata)
{
    tr_warn("Set point default value write not implemented.");
    return PT_STATUS_SUCCESS;
}

void ipso_create_set_point(connection_id_t connection_id,
                           const char *device_id,
                           uint16_t object_instance_id,
                           float target_temperature)
{
    pt_status_t status = PT_STATUS_SUCCESS;

    uint8_t *temperature_data = malloc(sizeof(float));
    if (temperature_data == NULL) {
        tr_err("Could not allocate temperature_data");
        return;
    }
    memcpy(temperature_data, &target_temperature, sizeof(float));

    status = pt_device_add_resource_with_callback(connection_id,
                                                  device_id,
                                                  SET_POINT,
                                                  object_instance_id,
                                                  SENSOR_VALUE,
                                                  LWM2M_FLOAT,
                                                  OPERATION_READ_WRITE,
                                                  temperature_data,
                                                  sizeof(float),
                                                  free,
                                                  ipso_write_set_point_value);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("G Could not create a resource with id (%d) to the object_instance (%d/%d).",
               SET_POINT_VALUE,
               SET_POINT,
               object_instance_id);
        return;
    }

    status = pt_device_add_resource(connection_id,
                                    device_id,
                                    SET_POINT,
                                    object_instance_id,
                                    SENSOR_UNITS,
                                    LWM2M_STRING,
                                    (uint8_t *) "Cel",
                                    strlen("Cel"),
                                    NULL);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("H Could not create a resource with id (%d) to the object_instance (%d/%d).",
               SENSOR_UNITS, SET_POINT, object_instance_id);
    }
}

#define HEX_CHARS "0123456789ABCDEF"
char* ipso_convert_value_to_hex_string(uint8_t *data, const uint32_t value_size)
{
    // Representation is in format AA:BB:CC...
    char *str = calloc(value_size * 3 + /* NUL */ 1, sizeof(char));
    if (str == NULL) {
        tr_err("Could not allocate str");
        return NULL;
    }
    uint8_t * data_offset = data;
    int str_index = 0;
    for (int i = 0; i < value_size; i++, data_offset++, str_index+=3) {
        str[str_index] = HEX_CHARS[(*data_offset >> 4) & 0xF];
        str[str_index+1] = HEX_CHARS[*data_offset & 0xF];
        str[str_index+2] = ':';
    }

    return str;
}
