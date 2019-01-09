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

/**
 * \file pt_ble_supported_translations.c
 * \brief Translation functions for BLE devices, services and characteristics.
 */
#include <string.h>
#include <assert.h>
#include <math.h>
#include <arpa/inet.h>
#include <gio/gio.h>
#include "pt-client-2/pt_api.h"
#include "pt-client-2/pt_device_object.h"
#include "devices.h"
#include "pt_ble.h"
#include "pt_edge.h"
#include "pt_ble_supported_translations.h"
#include "pt_ble_translations.h"
#include "examples-common-2/ipso_objects.h"
#include "byte-order/byte_order.h"

#define TRACE_GROUP "btsv"
#include "mbed-trace/mbed_trace.h"


// Define supported characteristics table for each supported service
static const ble_characteristic_t environment_sensing_characteristics[] =
{
    // Temperature
    {.uuid = "00002A6E-0000-1000-8000-00805F9B34FB",
     .characteristic_construct = ble_services_construct_temperature_characteristic,
     .characteristic_value_decode = ble_services_decode_value_with_format_descriptor,
     .characteristic_value_encode = NULL,
     .value_format_descriptor = {
            .format = VALUE_FORMAT_INT16,
            .exponent = -2
        }
    },
    // Humidity
    {.uuid = "00002A6F-0000-1000-8000-00805F9B34FB",
     .characteristic_construct = ble_services_construct_humidity_characteristic,
     .characteristic_value_decode = ble_services_decode_value_with_format_descriptor,
     .characteristic_value_encode = NULL,
     .value_format_descriptor = {
            .format = VALUE_FORMAT_UINT16,
            .exponent = -2
        }
    },
    // Barometer
    {.uuid = "00002A6D-0000-1000-8000-00805F9B34FB",
     .characteristic_construct = ble_services_construct_barometer_characteristic,
     .characteristic_value_decode = ble_services_decode_value_with_format_descriptor,
     .characteristic_value_encode = NULL,
     .value_format_descriptor = {
            .format = VALUE_FORMAT_UINT32,
            .exponent = -1
        }
    }
};

static const ble_characteristic_t automation_io_characteristics[] =
{
    {.uuid = "00002A56-0000-1000-8000-00805F9B34FB",
     .characteristic_construct = ble_services_construct_automation_io_characteristic,
     .characteristic_value_decode = ble_services_decode_2bit_bitfield_value,
     .characteristic_value_encode = ble_services_encode_2bit_bitfield_value
    }
};

const ble_service_t ble_services[] =
{
    {.uuid = "0000181A-0000-1000-8000-00805F9B34FB",
     .service_construct = NULL,
     .characteristic_count = sizeof(environment_sensing_characteristics) / sizeof(ble_characteristic_t),
     .characteristics = environment_sensing_characteristics},
    {.uuid = "00001815-0000-1000-8000-00805F9B34FB",
     .service_construct = NULL,
     .characteristic_count = sizeof(automation_io_characteristics) / sizeof(ble_characteristic_t),
     .characteristics = automation_io_characteristics},
    {.uuid = "0000180A-0000-1000-8000-00805F9B34FB",
     .service_construct = ble_services_construct_dis_service,
     .characteristic_count = 0,
     .characteristics = NULL},
    {.uuid = "0000A000-0000-1000-8000-00805F9B34FB",
     .service_construct = NULL,
     .characteristic_count = 0,
     .characteristics = NULL},
    {.uuid = "0000ED00-0000-1000-8000-00805F9B34FB",
     .service_construct = NULL,
     .characteristic_count = 0,
     .characteristics = NULL},
    {.uuid = "0000F000-0000-1000-8000-00805F9B34FB",
     .service_construct = NULL,
     .characteristic_count = 0,
     .characteristics = NULL},
};
const size_t ble_services_count = sizeof(ble_services) / sizeof(ble_service_t);


static bool ble_services_construct_sensor(struct ble_device *device,
                                          const int sv_idx,
                                          const int ch_idx,
                                          const ble_characteristic_t *ble_characteristic,
                                          const uint16_t sensor_id,
                                          const char *sensor_units,
                                          const char *sensor_description)
{
    assert(device != NULL);
    assert(ble_characteristic != NULL);
    assert(sensor_units != NULL);
    assert(sensor_description != NULL);

    const struct ble_gatt_service *service = &device->attrs.services[sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ch_idx];

    int32_t instance_id = pt_device_get_next_free_object_instance_id(edge_get_connection_id(), device->device_id, sensor_id);
    if (instance_id < 0 || instance_id > UINT16_MAX) {
        tr_warning("Could not create new instance for object %d", sensor_id);
        return false;
    }

    ipso_create_sensor_object(edge_get_connection_id(), device->device_id, sensor_id, instance_id, sensor_units, sensor_description);

    tr_debug("Construct path %s as IPSO sensor (%s) /%d/%d", characteristic->dbus_path, sensor_description, sensor_id, instance_id);

    ipso_add_min_max_fields(edge_get_connection_id(), device->device_id, sensor_id, instance_id, ipso_reset_min_max_object);

    ble_services_configure_translation_context(device, sv_idx, ch_idx, sensor_id, instance_id, SENSOR_VALUE, 0);
    return true;
}

/*
 * Instantiate default IPSO temperature object instance from characteristic
 */
bool ble_services_construct_temperature_characteristic(struct ble_device *device,
                                                       const int sv_idx,
                                                       const int ch_idx,
                                                       const ble_characteristic_t *ble_characteristic)
{
    return ble_services_construct_sensor(device, sv_idx, ch_idx, ble_characteristic, TEMPERATURE_SENSOR, "Cel", "Ambient temperature");
}

/*
 * Instantiate default IPSO humidity object instance from characteristic
 */
bool ble_services_construct_humidity_characteristic(struct ble_device *device,
                                                    const int sv_idx,
                                                    const int ch_idx,
                                                    const ble_characteristic_t *ble_characteristic)
{
    return ble_services_construct_sensor(device, sv_idx, ch_idx, ble_characteristic, HUMIDITY_SENSOR, "RH%", "Relative humidity");
}

/*
 * Instantiate default IPSO barometer object instance from characteristic
 */
bool ble_services_construct_barometer_characteristic(struct ble_device *device,
                                                     const int sv_idx,
                                                     const int ch_idx,
                                                     const ble_characteristic_t *ble_characteristic)
{
    return ble_services_construct_sensor(device, sv_idx, ch_idx, ble_characteristic, BAROMETER_SENSOR, "Pa", "Atmospheric pressure");
}

bool ble_services_construct_lightcontrol_from_characteristic(struct ble_device *device,
                                                             const int sv_idx,
                                                             const int ch_idx,
                                                             const ble_characteristic_t *ble_characteristic)
{
    assert(device != NULL);
    assert(ble_characteristic != NULL);

    uint16_t object_id = LIGHT_CONTROL;

    const struct ble_gatt_service *service = &device->attrs.services[sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ch_idx];

    // This implementation assumes there are 2 digital outputs each controlling a led.
    // Number of outputs could be read from the 0x2909 descriptor of this characteristic.
    // In case of multiple same characteristics controlling multiple leds, there would need
    // to be some way to determine which light control object instance belongs to which
    // characteristic and it's offset inside the bit field.
    const int digital_count = 2; // Could be replaced by descriptor 0x2909 value (number of digital io's)x

    for (int bitfield_offset = 0; bitfield_offset < digital_count; bitfield_offset++) {
        // Create new instance for this characteristic
        int32_t id = pt_device_get_next_free_object_instance_id(edge_get_connection_id(), device->device_id, object_id);
        if (id < 0 || id > UINT16_MAX) {
            tr_error("Could not create new light control object instance!");
            return false;
        }
        uint16_t instance_id = id;

        tr_debug("Construct path %s as IPSO object (light control) /%d/%d (offset=%d)", characteristic->dbus_path, object_id, instance_id, bitfield_offset);

        // This initializes on/off value to 0, we could read this from the characteristic incase it is already some other value.
        uint8_t value = 0;
        if (!edge_add_resource(device->device_id,
                               object_id,
                               id,
                               ON_OFF_VALUE,
                               LWM2M_BOOLEAN,
                               OPERATION_READ | OPERATION_WRITE,
                               &value,
                               sizeof(uint8_t))) {
            tr_warning("Could not create light control resource!");
            return false;
        }

        ble_services_configure_translation_context(device, sv_idx, ch_idx, object_id, instance_id, ON_OFF_VALUE, bitfield_offset);
    }

    return true;
}

bool ble_services_construct_pushbutton_from_characteristic(struct ble_device *device,
                                                           const int sv_idx,
                                                           const int ch_idx,
                                                           const ble_characteristic_t *ble_characteristic)
{
    assert(device != NULL);
    assert(ble_characteristic != NULL);

    uint16_t object_id = PUSH_BUTTON;

    const struct ble_gatt_service *service = &device->attrs.services[sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ch_idx];

    // This implementation assumes there are 2 digital inputs each controlled by a button.
    // Number of inputs could be read from the 0x2909 descriptor of this characteristic.
    // In case of multiple same characteristics controlled by multiple buttons, there would need
    // to be some way to determine which push button object instance belongs to which
    // characteristic and it's offset inside the bit field.
    const int digital_count = 2; // Could be replaced by descriptor 0x2909 value (number of digital io's)x

    for (int bitfield_offset = 0; bitfield_offset < digital_count; bitfield_offset++) {
        // Create new instance for this characteristic
        int32_t id = pt_device_get_next_free_object_instance_id(edge_get_connection_id(), device->device_id, object_id);
        if (id < 0 || id > UINT16_MAX) {
            tr_error("Could not create new push button object instance!");
            return false;
        }
        uint16_t instance_id = id;

        tr_debug("Construct path %s as IPSO push button /%d/%d (offset=%d)", characteristic->dbus_path, object_id, id, bitfield_offset);

        // This initializes on/off value to 0, we could read this from the characteristic incase it is already some other value.
        uint8_t value = 0;
        if (!edge_add_resource(device->device_id,
                               object_id,
                               id,
                               DIGITAL_INPUT_STATE,
                               LWM2M_BOOLEAN,
                               OPERATION_WRITE,
                               &value,
                               sizeof(uint8_t))) {
            tr_warning("Could not create push button resource!");
            return false;
        }

        ble_services_configure_translation_context(device, sv_idx, ch_idx, object_id, instance_id, DIGITAL_INPUT_STATE, bitfield_offset);
    }

    return true;
}

/*
 * Instantiate default IPSO object instances for automation io characteristic
 */
bool ble_services_construct_automation_io_characteristic(struct ble_device *device,
                                                         const int sv_idx,
                                                         const int ch_idx,
                                                         const ble_characteristic_t *ble_characteristic)
{
    assert(device != NULL);
    assert(ble_characteristic != NULL);

    const struct ble_gatt_service *service = &device->attrs.services[sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ch_idx];

    tr_debug("Construct path %s as automation io", characteristic->dbus_path);

    if (characteristic->properties & BLE_GATT_PROP_PERM_WRITE) {
        // Construct to light control IPSO (it's possible it's both readable and writable)
        return ble_services_construct_lightcontrol_from_characteristic(device, sv_idx, ch_idx, ble_characteristic);
    }
    else if (characteristic->properties & BLE_GATT_PROP_PERM_READ) {
        return ble_services_construct_pushbutton_from_characteristic(device, sv_idx, ch_idx, ble_characteristic);
    }
    else {
        // Not readable or writable, what to do in such case?
        return false;
    }

    return true;
}

pt_status_t ble_services_reboot_callback(const connection_id_t connection_id,
                                         const char *device_id,
                                         const uint16_t object_id,
                                         const uint16_t object_instance_id,
                                         const uint16_t resource_id,
                                         const uint8_t operation,
                                         const uint8_t *value,
                                         const uint32_t size,
                                         void *userdata)
{
    // Reboot resource is mandatory for LwM2M device object, so this is a dummy reboot if device
    // does not actually support reboot operation
    (void)connection_id;
    (void)object_id;
    (void)object_instance_id;
    (void)resource_id;
    (void)operation;
    (void)value;
    (void)size;
    (void)userdata;
    tr_debug("Reboot requested for device %s", device_id);
    return PT_STATUS_SUCCESS;
}

bool ble_services_construct_dis_service(struct ble_device *device,
                                        const int sv_idx,
                                        const ble_service_t *service_descriptor)
{
    assert(device != NULL);

    uint8_t manufacturer[128];
    uint8_t model[128];
    uint8_t serial[128];
    uint8_t firmware[128];
    uint8_t hardware[128];

    ptdo_device_object_data_t device_object_data = {0};
    device_object_data.software_version = "N/A";
    device_object_data.device_type = "Bluetooth 4.0";
    device_object_data.reboot_callback = ble_services_reboot_callback;
    device_object_data.factory_reset_callback = NULL;
    device_object_data.reset_error_code_callback = NULL;

    tr_debug("Resolving characteristics");
    // Loop each included characteristic path
    for (int i = 0; i < device->attrs.services[sv_idx].chars_count; i++) {
        size_t buf_size = 128;
        struct ble_gatt_char *ch = &device->attrs.services[sv_idx].chars[i];
        tr_debug("New characteristic at %s", ch->dbus_path);
        if (ch->dbus_path) {
            char *characteristic_uuid = ch->uuid;
            if (strcasecmp(characteristic_uuid, "00002A29-0000-1000-8000-00805F9B34FB") == 0) {
                if (ble_read_characteristic(ch->dbus_path, manufacturer, &buf_size) == 0) {
                    device_object_data.manufacturer = (char*)manufacturer;
                }
            }
            else if (strcasecmp(characteristic_uuid, "00002A24-0000-1000-8000-00805F9B34FB") == 0) {
                if (ble_read_characteristic(ch->dbus_path, model, &buf_size) == 0) {
                    device_object_data.model_number = (char*)model;
                }
            }
            else if (strcasecmp(characteristic_uuid, "00002A25-0000-1000-8000-00805F9B34FB") == 0) {
                if (ble_read_characteristic(ch->dbus_path, serial, &buf_size) == 0) {
                    device_object_data.serial_number = (char*)serial;
                }
            }
            else if (strcasecmp(characteristic_uuid, "00002A26-0000-1000-8000-00805F9B34FB") == 0) {
                if (ble_read_characteristic(ch->dbus_path, firmware, &buf_size) == 0) {
                    device_object_data.firmware_version = (char*)firmware;
                }
            }
            else if (strcasecmp(characteristic_uuid, "00002A27-0000-1000-8000-00805F9B34FB") == 0) {
                if (ble_read_characteristic(ch->dbus_path, hardware, &buf_size) == 0) {
                    device_object_data.hardware_version = (char*)hardware;
                }
            }
        }
    }

    ptdo_initialize_device_object(edge_get_connection_id(), device->device_id, &device_object_data);
    return true;
}

bool ble_services_decode_value_with_format_descriptor(const struct ble_device *device,
                                                      const struct translation_context *ctx,
                                                      const uint8_t *value,
                                                      const size_t value_size,
                                                      uint8_t **value_out,
                                                      size_t *value_out_len)
{
    assert(ctx != NULL);
    assert(value != NULL);
    assert(value_out != NULL);

    tr_debug("ble_services_decode_value_with_format_descriptor");

    const struct ble_gatt_service *service = &device->attrs.services[ctx->sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ctx->ch_idx];

    const ble_characteristic_t* char_descriptor = ble_services_get_characteristic_descriptor_by_uuids(service->uuid, characteristic->uuid);

    value_format_t format = char_descriptor->value_format_descriptor.format;
    int8_t exponent = char_descriptor->value_format_descriptor.exponent;
    if (format == 0) {
        return false;
    }

    // Only supports upto 32bit size value presentations at the moment
    int num_bytes = (value_size > 4 ? 4 : value_size);
    uint32_t raw_value = 0;
    for (int i = 0; i < num_bytes; i++) {
        raw_value = raw_value + (((uint64_t)value[i]) << (i * 8));
    }

    uint8_t *temp_buf = NULL;
    if (exponent < 0 || format == VALUE_FORMAT_FLOAT32_IEEE754) {
        // Represent as float
        float float_value = (float)raw_value * (pow(10, exponent));
        temp_buf = malloc(sizeof(float));
        convert_float_value_to_network_byte_order(float_value, temp_buf);
        *value_out_len = sizeof(float);
        tr_debug("Float value %f", float_value);
        tr_debug("Float format buffer %s", tr_array(temp_buf, *value_out_len));
        // Update the min and max resources if they exist
        ipso_update_min_max_fields(edge_get_connection_id(),
                                   device->device_id,
                                   ctx->object_id,
                                   ctx->object_instance_id,
                                   float_value);
    }
    else {
        // Represent as int
        raw_value = raw_value * pow(10, exponent);
        switch (format) {
        case VALUE_FORMAT_BOOLEAN:
            temp_buf = malloc(sizeof(uint8_t));
            *temp_buf = ((raw_value & 0xFF) > 0);
            *value_out_len = sizeof(uint8_t);
            tr_debug("Integer value %d", ((raw_value & 0xFF) > 0));
            tr_debug("Boolean format buffer %s", tr_array(temp_buf, *value_out_len));
            break;
        case VALUE_FORMAT_UINT2:
        case VALUE_FORMAT_UINT4:
        case VALUE_FORMAT_UINT8:
        case VALUE_FORMAT_INT8:
            temp_buf = malloc(sizeof(uint8_t));
            *temp_buf = raw_value & 0xFF;
            *value_out_len = sizeof(uint8_t);
            tr_debug("Integer value %d", raw_value & 0xFF);
            tr_debug("8-bit integer format buffer %s", tr_array(temp_buf, *value_out_len));
            break;
        case VALUE_FORMAT_UINT12:
        case VALUE_FORMAT_UINT16:
        case VALUE_FORMAT_INT12:
        case VALUE_FORMAT_INT16:
            temp_buf = malloc(sizeof(uint16_t));
            *temp_buf = htonl((uint16_t)raw_value & 0xFFFF);
            *value_out_len = sizeof(uint16_t);
            tr_debug("Integer value %d", raw_value & 0xFFFF);
            tr_debug("16-bit integer format buffer %s",
                     tr_array(temp_buf, *value_out_len));
            break;
        case VALUE_FORMAT_UINT24:
        case VALUE_FORMAT_UINT32:
        case VALUE_FORMAT_INT24:
        case VALUE_FORMAT_INT32:
            temp_buf = malloc(sizeof(uint32_t));
            *temp_buf = htonl((uint32_t)raw_value & 0xFFFFFFFF);
            *value_out_len = sizeof(uint32_t);
            tr_debug("Integer value %d", raw_value & 0xFFFFFFFF);
            tr_debug("32-bit integer format buffer %s", tr_array(temp_buf, *value_out_len));
            break;
        default:
            tr_warning("Unsupported value format presentation descriptor");
            return false;
        }
    }

    *value_out = temp_buf;
    return true;
}

bool ble_services_decode_2bit_bitfield_value(const struct ble_device *device,
                                             const struct translation_context *ctx,
                                             const uint8_t *value,
                                             const size_t value_size,
                                             uint8_t **value_out,
                                             size_t *value_out_len)
{
    assert(ctx != NULL);
    assert(value != NULL);
    assert(value_out != NULL);

    tr_debug("ble_services_decode_2bit_bitfield_value");

    // value_array should be array type (array of bytes) length of 2
    uint8_t raw_value = value[0];

    // We need to right shift characteristic_extra_flags amount * 2bit
    uint32_t offset = ctx->characteristic_extra_flags;
    raw_value = raw_value >> (offset * 2);
    raw_value = raw_value & 0x03; // Clear all other bits than 2 lower most

    uint8_t *value_buf = malloc(sizeof(uint8_t));
    if (value_buf == NULL) {
        return false;
    }

    *value_buf = raw_value;

    *value_out = value_buf;
    *value_out_len = sizeof(uint8_t);

    return true;
}

bool ble_services_encode_2bit_bitfield_value(const struct ble_device *device,
                                             const struct translation_context *ctx,
                                             const uint8_t *current_characteristic_value,
                                             const size_t current_characteristic_size,
                                             const uint8_t *new_value,
                                             const size_t new_size,
                                             uint8_t **new_characteristic_value,
                                             size_t *new_characteristic_size)
{
    assert(ctx != NULL);
    assert(current_characteristic_value != NULL);
    assert(new_value != NULL);
    assert(new_characteristic_value != NULL);

    tr_debug("ble_services_encode_2bit_bitfield_value");
    tr_debug("current value %s", tr_array(current_characteristic_value, current_characteristic_size));
    tr_debug("new value: %s", tr_array(new_value, new_size));

    // Update correct offset in original value with new value
    // The offset is stored in characteristic_extra_flags, and is multiplied by
    // 2 because each field is 2 bits size
    size_t offset = ctx->characteristic_extra_flags * 2;
    size_t byte_offset = offset / 8; // Byte offset inside buffer
    size_t bit_offset = offset % 8; // Bit offset inside byte
    // NOTE: temp_size should probably be constant based on number of digital io's
    // defined for characteristic. Set to byte_offset+1 now as number of digitals io's
    // is not known and current buffer is not allocated based on any knowledge about it.
    // Currently we get the 512 byte buffer allocated by the service object creation here.
    size_t out_size = byte_offset + 1;
    if (out_size > current_characteristic_size) {
        // Can't encode as offset larger than buffer size
        tr_warning("Can't encode, bitfield offset bigger than buffer size");
        return false;
    }

    uint8_t *buf = calloc(out_size, sizeof(uint8_t));
    if (buf == NULL) {
        // Could not allocate value buffer
        return false;
    }

    // Copy current value
    memcpy(buf, current_characteristic_value, out_size);

    // Change only the 2 bits we're working with
    buf[byte_offset] = buf[byte_offset] & (~(3 << bit_offset));
    if (new_value[0]) {
        // Set active if value is other than 0
        buf[byte_offset] = buf[byte_offset] | (1 << bit_offset);
    }

    *new_characteristic_value = buf;
    *new_characteristic_size = out_size;

    return true;
}
