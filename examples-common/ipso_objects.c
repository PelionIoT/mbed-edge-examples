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

#include "examples-common/ipso_objects.h"
#include "common/constants.h"
#include "common/integer_length.h"
#include "pt-client/pt_api.h"
#include "byte-order/byte_order.h"
#include "mbed-trace/mbed_trace.h"

#define TRACE_GROUP "ipso-objects"

const int32_t ipso_get_next_free_object_instance_id(pt_object_t *object)
{
    for (uint16_t id = 0; id < UINT16_MAX; id++) {
        if (NULL == pt_object_find_object_instance(object, id)) {
            return id;
        }
    }
    return -1;
}

void ipso_add_min_max_fields(pt_object_instance_t *instance, pt_resource_callback reset_callback)
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

    (void)pt_object_instance_add_resource(instance, MIN_MEASURED_VALUE,
                                          LWM2M_FLOAT,
                                          min_default_data,
                                          sizeof(float),
                                          &status);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create resource with id (%d) to the object_instance (%d).",
               MIN_MEASURED_VALUE, instance->id);
    }

    (void)pt_object_instance_add_resource(instance, MAX_MEASURED_VALUE,
                                          LWM2M_FLOAT,
                                          max_default_data,
                                          sizeof(float),
                                          &status);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create resource with id (%d) to the object_instance (%d).",
               MAX_MEASURED_VALUE, instance->id);
    }

    (void)pt_object_instance_add_resource_with_callback(instance, RESET_MIN_MAX_MEASURED_VALUES,
                                                        LWM2M_OPAQUE,
                                                        OPERATION_EXECUTE,
                                                        NULL,
                                                        0,
                                                        &status, reset_callback);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create resource with id (%d) to the object_instance (%d).",
               RESET_MIN_MAX_MEASURED_VALUES, instance->id);
    }
}

pt_status_t ipso_add_resource(pt_object_instance_t *instance, enum IPSO_RESOURCES object, Lwm2mResourceType type, uint8_t operations, const uint8_t *value, uint32_t value_size, pt_resource_callback callback)
{

    pt_status_t status = PT_STATUS_SUCCESS;
    // Add resource
    (void)pt_object_instance_add_resource_with_callback(instance, object, type, operations, (uint8_t*)value, value_size, &status, callback);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create a resource with id %d ", object);
    }

    return status;

}

pt_object_instance_t* ipso_create_next_free_object_instance(pt_device_t *device,
                                                            enum IPSO_OBJECTS object_id)
{
    pt_status_t status = PT_STATUS_SUCCESS;

    pt_object_t *object = pt_device_find_object(device, object_id);
    if (object == NULL) {
        object = pt_device_add_object(device, object_id, &status);
        if (status != PT_STATUS_SUCCESS || object == NULL) {
            tr_err("Could not create an object with id (%d) to the device (%s).",
                   object_id, device->device_id);
            return NULL;
        }
    }

    // Create next new instance for this object
    int32_t id = ipso_get_next_free_object_instance_id(object);
    if (id < 0 || id > UINT16_MAX) {
        tr_err("Could not create new object instance!");
        return NULL;
    }
    uint16_t instance_id = id;

    pt_object_instance_t *instance =
        pt_object_add_object_instance(object, instance_id, &status);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create an object instance with id (%d) to the object (%d).",
               instance_id, object_id);
    }

    return instance;
}

pt_object_instance_t* ipso_create_custom_object(pt_device_t *device,
                                                enum IPSO_OBJECTS object_id,
                                                enum IPSO_RESOURCES resourceId,
                                                Lwm2mResourceType type,
                                                uint8_t operations,
                                                const void* value,
                                                const uint32_t value_size,
                                                pt_resource_callback callback)
{
    pt_object_instance_t *instance = ipso_create_next_free_object_instance(device, object_id);
    if (instance == NULL) {
        tr_err("Could not create new object instance to the object (%d).", object_id);
        return NULL;
    }

    uint8_t *sensor_data = malloc(value_size);
    if (sensor_data == NULL) {
        tr_err("Could not allocate sensor_data");
        return NULL;
    }

    memcpy(sensor_data, value, value_size);

    ipso_add_resource(instance, resourceId, type, operations, sensor_data, value_size, callback);

    return instance;
}

void ipso_create_thermometer(pt_device_t *device, const uint16_t object_instance_id, const float temperature, bool optional_fields, pt_resource_callback reset_thermometer_callback)
{
    pt_status_t status = PT_STATUS_SUCCESS;

    pt_object_instance_t *instance = ipso_create_next_free_object_instance(device, TEMPERATURE_SENSOR);
    if (instance == NULL) {
        tr_err("Could not create new object instance to the object (%d).", TEMPERATURE_SENSOR);
    }

    uint8_t *temperature_data = malloc(sizeof(float));
    convert_float_value_to_network_byte_order(temperature, (uint8_t *) temperature_data);

    // Add sensor value resource
    (void)pt_object_instance_add_resource(instance, SENSOR_VALUE,
                                          LWM2M_FLOAT,
                                          temperature_data,
                                          sizeof(float),
                                          &status);

    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create a resource with id (%d) to the object_instance (%d).",
               SENSOR_VALUE, object_instance_id);
    }

    // Add units resource
    (void)pt_object_instance_add_resource(instance, SENSOR_UNITS,
                                          LWM2M_STRING,
                                          (uint8_t*) strdup("Cel"),
                                          strlen("Cel"),
                                          &status);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create a resource with id (%d) to the object_instance (%d).",
               SENSOR_UNITS, object_instance_id);
    }

    if (optional_fields) {
        if (reset_thermometer_callback) {
            ipso_add_min_max_fields(instance, reset_thermometer_callback);
        } else {
            ipso_add_min_max_fields(instance, ipso_reset_min_max_object);
        }
    }
}

pt_object_instance_t* ipso_create_sensor_object(pt_device_t *device, enum IPSO_OBJECTS sensor_id, const char *sensor_units, const char *sensor_type)
{
    pt_object_instance_t *instance = ipso_create_next_free_object_instance(device, sensor_id);
    if (instance == NULL) {
        tr_err("Could not create new object instance to the object (%d).", sensor_id);
        return NULL;
    }

    uint8_t *sensor_data = malloc(sizeof(float));
    if (sensor_data == NULL) {
        tr_err("Could not allocate sensor_data");
        return NULL;
    }
    convert_float_value_to_network_byte_order(0.0, sensor_data);

    ipso_add_resource(instance, SENSOR_VALUE, LWM2M_FLOAT, OPERATION_READ, sensor_data, (uint32_t)sizeof(float), NULL);

    if (sensor_units != NULL ){
       ipso_add_resource(instance, SENSOR_UNITS, LWM2M_STRING, OPERATION_READ, (uint8_t*)strdup(sensor_units), strlen((char*)sensor_units), NULL);
    }

    if (sensor_type != NULL) {
      ipso_add_resource(instance, SENSOR_TYPE, LWM2M_STRING, OPERATION_READ, (uint8_t*)strdup(sensor_type), strlen((char*)sensor_type), NULL);
    }

    return instance;
}

void ipso_reset_min_max_object(const pt_resource_t *resource, const uint8_t *value, uint32_t value_len, void *userdata)
{
    tr_info("Resetting min and max to default values on '%s'.", resource->parent->parent->parent->device_id);
    pt_resource_t *min = pt_object_instance_find_resource(resource->parent, MIN_MEASURED_VALUE);
    if (min) {
        float min_default = FLT_MAX; // Set minimum measured on reset to max float
        uint8_t *min_default_data = malloc(sizeof(float));
        if (min_default_data == NULL) {
            tr_err("Could not allocate min_default_data");
            return;
        }
        convert_float_value_to_network_byte_order(min_default, min_default_data);
        memcpy(min->value, min_default_data, sizeof(float));
        free(min_default_data);
    }

    pt_resource_t *max = pt_object_instance_find_resource(resource->parent, MAX_MEASURED_VALUE);
    if (max) {
        float max_default = -FLT_MAX; // Set maximum measured on reset to min float
        uint8_t *max_default_data = malloc(sizeof(float));
        if (max_default_data == NULL) {
            tr_err("Could not allocate max_default_data");
            return;
        }
        convert_float_value_to_network_byte_order(max_default, max_default_data);
        memcpy(max->value, max_default_data, sizeof(float));
        free(max_default_data);
    }
}

void ipso_write_set_point_value(const pt_resource_t *resource, const uint8_t* value, const uint32_t value_size, void *ctx)
{
    tr_warn("Set point default value write not implemented.");
}

void ipso_create_set_point(pt_device_t *device, uint16_t object_instance_id, float target_temperature)
{
    pt_status_t status = PT_STATUS_SUCCESS;

    pt_object_instance_t *instance = ipso_create_next_free_object_instance(device, SET_POINT);
    if (instance == NULL) {
        tr_err("Could not create new object instance to the object (%d).", SET_POINT);
    }

    uint8_t *temperature_data = malloc(sizeof(float));
    if (temperature_data == NULL) {
        tr_err("Could not allocate temperature_data");
        return;
    }
    memcpy(temperature_data, &target_temperature, sizeof(float));

    // Add set point read write resource
    (void)pt_object_instance_add_resource_with_callback(instance, SET_POINT_VALUE,
                                                        LWM2M_FLOAT,
                                                        OPERATION_READ_WRITE,
                                                        temperature_data,
                                                        sizeof(float),
                                                        &status, ipso_write_set_point_value);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create a resource with id (%d) to the object_instance (%d/%d).",
               SET_POINT_VALUE, SET_POINT, object_instance_id);
    }

    // Add units resource
    (void)pt_object_instance_add_resource(instance, SENSOR_UNITS,
                                          LWM2M_STRING,
                                          (uint8_t*) strdup("Cel"),
                                          strlen("Cel"),
                                          &status);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Could not create a resource with id (%d) to the object_instance (%d/%d).",
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
