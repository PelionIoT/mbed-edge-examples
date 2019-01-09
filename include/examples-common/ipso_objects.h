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

#ifndef EDGE_IPSO_OBJECTS_H
#define EDGE_IPSO_OBJECTS_H

#include "pt-client/pt_api.h"

enum IPSO_OBJECTS {
    DIGITAL_OUTPUT          = 3201,
    TEMPERATURE_SENSOR      = 3303,
    HUMIDITY_SENSOR         = 3304,
    SET_POINT               = 3308,
    LIGHT_CONTROL           = 3311,
    BAROMETER_SENSOR        = 3315,
    CONCENTRATION_SENSOR    = 3325,
    PUSH_BUTTON             = 3347,
    FIRMWARE_UPDATE         = 5
};

enum IPSO_RESOURCES {
    DIGITAL_INPUT_STATE           = 5500,
    DIGITAL_INPUT_COUNTER         = 5501,
    MIN_MEASURED_VALUE            = 5601,
    MAX_MEASURED_VALUE            = 5602,
    RESET_MIN_MAX_MEASURED_VALUES = 5605,
    SENSOR_VALUE                  = 5700,
    SENSOR_UNITS                  = 5701,
    SENSOR_TYPE                   = 5751,
    ON_OFF_VALUE                  = 5850,
    SET_POINT_VALUE               = 5900
};

const int32_t ipso_get_next_free_object_instance_id(pt_object_t *object);
pt_object_instance_t* ipso_create_next_free_object_instance(pt_device_t *device,
                                                            enum IPSO_OBJECTS object_id);

void ipso_create_thermometer(pt_device_t *device, uint16_t object_instance_id,float temperature,
                             bool optional_fields, pt_resource_callback reset_thermometer_callback);

pt_object_instance_t* ipso_create_sensor_object(pt_device_t *device, enum IPSO_OBJECTS sensor_id, const char *sensor_units, const char *sensor_type);
pt_object_instance_t* ipso_create_custom_object(pt_device_t *device, enum IPSO_OBJECTS object_id, enum IPSO_RESOURCES resourceId, Lwm2mResourceType type,
                                                uint8_t operations, const void* value, const uint32_t value_size, pt_resource_callback callback);
void ipso_create_set_point(pt_device_t *device, uint16_t object_instance_id, float target_temperature);
void ipso_create_set_point(pt_device_t *device, uint16_t object_instance_id, float target_temperature);

/**
 * \brief Default example thermometer mix and max reset callback
 * See ::pt_device_resource_execute
 */
void ipso_reset_min_max_object(const pt_resource_t *resource,
                               const uint8_t *value,
                               const uint32_t value_length,
                               void *userdata);
void ipso_add_min_max_fields(pt_object_instance_t *instance, pt_resource_callback reset_callback);
int ipso_object_to_json_string(pt_object_t *object, char **data);

#endif /* EDGE_IPSO_OBJECTS_H */
