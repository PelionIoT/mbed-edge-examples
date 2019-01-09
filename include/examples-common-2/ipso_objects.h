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

#include "pt-client-2/pt_api.h"

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

const int16_t ipso_get_next_free_object_instance_id(const char *device_id, uint16_t object_id);
void ipso_create_thermometer(connection_id_t connection_id, const char *device_id, uint16_t object_instance_id,float temperature,
                             bool optional_fields, pt_resource_callback reset_thermometer_callback);

void ipso_create_sensor_object(connection_id_t connection_id,
                               const char *device_id,
                               enum IPSO_OBJECTS sensor_id,
                               const uint16_t object_instance_id,
                               const char *sensor_units,
                               const char *sensor_type);
void ipso_create_custom_object(connection_id_t connection_id,
                               const char *device_id,
                               enum IPSO_OBJECTS object_id,
                               const uint16_t object_instance_id,
                               enum IPSO_RESOURCES resource_id,
                               Lwm2mResourceType type,
                               uint8_t operations,
                               void *value,
                               const uint32_t value_size,
                               pt_resource_callback callback);
void ipso_create_set_point(connection_id_t connection_id,
                           const char *device_id,
                           uint16_t object_instance_id,
                           float target_temperature);

/**
 * \brief Default example thermometer mix and max reset callback
 * See ::pt_device_resource_execute
 */

pt_status_t ipso_reset_min_max_object(const connection_id_t connection_id,
                                      const char *device_id,
                                      const uint16_t object_id,
                                      const uint16_t object_instance_id,
                                      const uint16_t resource_id,
                                      const uint8_t operation,
                                      const uint8_t *value,
                                      const uint32_t value_len,
                                      void *userdata);
void ipso_add_min_max_fields(connection_id_t connection_id,
                             const char *device_id,
                             const uint16_t object_id,
                             const uint16_t object_instance_id,
                             pt_resource_callback reset_callback);
pt_status_t ipso_update_min_max_fields(const connection_id_t connection_id,
                                       const char *device_id,
                                       const uint16_t object_id,
                                       const uint16_t object_instance_id,
                                       const float new_value);

#endif /* EDGE_IPSO_OBJECTS_H */
