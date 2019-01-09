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

#ifndef _PT_BLE_TRANSLATIONS_H_
#define _PT_BLE_TRANSLATIONS_H_

#include <gio/gio.h>
#include "compat.h"
#include "pt-client-2/pt_api.h"

typedef struct dbus_resource_mapping {
    int a;
} dbus_resource_mapping_t;

typedef struct ble_characteristic ble_characteristic_t;
typedef struct ble_service ble_service_t;

/** /brief Possible values for characteristics supported operations.
 *         The supported operations is a combination of these values
 */
typedef enum {
    CHARACTERISTIC_OPERATION_NONE   = 0,
    CHARACTERISTIC_OPERATION_READ   = 1,
    CHARACTERISTIC_OPERATION_WRITE  = (1 << 1),
    CHARACTERISTIC_OPERATION_NOTIFY = (1 << 2)
} ble_characteristic_operations_t;

typedef enum {
    VALUE_FORMAT_RESERVED1,
    VALUE_FORMAT_BOOLEAN,
    VALUE_FORMAT_UINT2,
    VALUE_FORMAT_UINT4,
    VALUE_FORMAT_UINT8,
    VALUE_FORMAT_UINT12,
    VALUE_FORMAT_UINT16,
    VALUE_FORMAT_UINT24,
    VALUE_FORMAT_UINT32,
    VALUE_FORMAT_UINT48,
    VALUE_FORMAT_UINT64,
    VALUE_FORMAT_UINT128,
    VALUE_FORMAT_INT8,
    VALUE_FORMAT_INT12,
    VALUE_FORMAT_INT16,
    VALUE_FORMAT_INT24,
    VALUE_FORMAT_INT32,
    VALUE_FORMAT_INT48,
    VALUE_FORMAT_INT64,
    VALUE_FORMAT_INT128,
    VALUE_FORMAT_FLOAT32_IEEE754,
    VALUE_FORMAT_FLOAT64_IEEE754,
    VALUE_FORMAT_SFLOAT16_IEEE11073,
    VALUE_FORMAT_FLOAT32_IEEE11073,
    VALUE_FORMAT_IEEE20601,
    VALUE_FORMAT_UTF8,
    VALUE_FORMAT_UTF16,
    VALUE_FORMAT_OPAQUE,
    VALUE_FORMAT_RESERVED2,
} value_format_t;

typedef enum {
    VALUE_NAMESPACE_BLUETOOTH_SIG = 1,
    VALUE_NAMESPACE_RESERVED = 2
} value_namespace_t;

typedef struct ble_services_characteristic_value_format {
    value_format_t format       : 8;
    int exponent                : 8;
    unsigned uuid               : 16;
    value_namespace_t namespace : 8;
    unsigned description        : 16;
} ble_services_characteristic_value_format_t;

/**
 * \brief A function prototype for service constructor.
 */
typedef bool (*ble_services_service_construct_cb_t)(struct ble_device *device,
                                                    const int sv_idx,
                                                    const ble_service_t *service_descriptor);

/**
 * \brief A function prototype for characteristic constructor.
 *
 * \param pt_device Pointer to pt_device_t struct.
 * \param characteristic_dbus_path DBus path of the characteristic object.
 * \param ble_characteristic Pointer to the ble_characteristic_t struct.
 *
 * \return Returns true when characteristic successfully constructed and false on failure.
 */
typedef bool (*ble_services_characteristic_construct_cb_t)(struct ble_device *device,
                                                           const int sv_idx,
                                                           const int ch_idx,
                                                           const ble_characteristic_t *ble_characteristic);

/**
 * \brief A function prototype for characteristic value decoder. This function decodes
 *        the bluetooth characteristic value from the bluetooth representation into
 *        correct representation format for the protocol translator resource.
 *
 * \param resource_mapping Pointer to the dbus_resource_mapping_t struct for this characteristic and
 *                         resource combination.
 * \param value Pointer to GVariant containing a array of bytes with the bluetooth characteristic
 *              representation of the value. See GLib documentation for GVariant for reference
 *              how to access array members inside the GVariant.
 * \param data_buffer Pointer to a pointer to uint8_t or char. This is an output parameter and after
 *                    successfully decoding the value this should be set to point to a buffer containing
 *                    the decoded value. The buffer should be allocated using malloc as ownership is
 *                    transferred to the caller and the caller is responsible for freeing it.
 * \param data_size Pointer to a size_t. This is an output parameter and the data_buffer size should
 *                  be set to the location pointed to by this pointer.
 *
 * \return Returns true when characteristic successfully decoded and false on failure.
 */
typedef bool (*ble_services_characteristic_value_decode_cb_t)(const struct ble_device *device,
                                                              const struct translation_context *ctx,
                                                              const uint8_t *value,
                                                              const size_t value_size,
                                                              uint8_t **data_buffer,
                                                              size_t *data_size);

/**
 * \brief A function prototype for characteristic value encoder. This function encodes
 *        the protocol translator resource's value format representation into the correct
 *        representation format for the bluetooth characteristic value.
 *
 * \param resource_mapping Pointer to the dbus_resource_mapping_t struct for this characteristic and
 *                         resource combination.
 * \param current_characteristic_value Pointer to a buffer containing the current value of the bluetooth
 *                                     characteristic in the characteristic representation format. This is
 *                                     mainly useful if the characteristic is for example a bitfield that is
 *                                     translated into multiple protocol translator resources, in which case
 *                                     the encoder would only modify the bits that represent this specific
 *                                     protocol translator resource.
 * \param current_characteristic_size Size of the current_characteristic_value buffer.
 * \param new_value Pointer to a buffer containing the new value in protocol translator resource's value
 *                  representation format. This is the value that should be encoded into the characteristic
 *                  value representation format.
 * \param new_size Size of the new_value buffer.
 * \param new_characteristic_value Pointer to a pointer to uint8_t or char. This is an output parameter and after
 *                                 successfully encoding the value this should be set to point to a buffer containing
 *                                 the encoded value. The buffer should be allocated using malloc as ownership is
 *                                 transferred to the caller and the caller is responsible for freeing it.
 * \param new_characteristic_size Pointer to a size_t. This is an output parameter and the data_buffer size should
 *                                be set to the location pointed to by this pointer.
 *
 * \return Returns true when characteristic successfully encoded and false on failure.
 */
typedef bool (*ble_services_characteristic_value_encode_cb_t)(const struct ble_device *device,
                                                              const struct translation_context *ctx,
                                                              const uint8_t *current_characteristic_value,
                                                              const size_t current_characteristic_size,
                                                              const uint8_t *new_value,
                                                              const size_t new_size,
                                                              uint8_t **new_characteristic_value,
                                                              size_t *new_characteristic_size);

/**
 * \struct ble_characteristic
 * \brief A structure for defining a characteristic translation.
 *
 * \var uuid UUID of the GATT characteristic.
 * \var characteristic_construct Function pointer for characteristic constructor function used to
 *                               construct the objects and/or resources in protocol translator for
 *                               representing this characteristic.
 * \var characteristic_value_decode Function pointer for characteristic value decoder function. This
 *                                  is used to decode value from bluetooth characteristic value representation
 *                                  format into the protocol translator resource's value format.
 * \var characteristic_value_encode Function pointer for characteristic value encoder function. This
 *                                  is used to encode value from protocol translator resource's value format
 *                                  into bluetooth characteristic value representation format.
 * \var value_format_descriptor Value format descriptor containing some additional information how the bluetooth
 *                              characteristic value is encoded.
 */
struct ble_characteristic {
    char                                          uuid[FORMATTED_UUID_LEN+1];
    ble_services_characteristic_construct_cb_t    characteristic_construct;
    ble_services_characteristic_value_decode_cb_t characteristic_value_decode;
    ble_services_characteristic_value_encode_cb_t characteristic_value_encode;
    ble_services_characteristic_value_format_t    value_format_descriptor;
};

/**
 * \struct ble_service
 * \brief A structure for defining a service translation.
 *
 * \var uuid UUID of the GATT service.
 * \var service_construct Function pointer for service constructor function used to
 *                        construct the objects and/or resources in protocol translator for
 *                        representing this service.
 * \var characteristic_count Number of characteristics in the characteristics array.
 * \var characteristics An array of characteristics. The array should contain characteristic_count number of elements.
 */
struct ble_service {
    char                                  uuid[FORMATTED_UUID_LEN+1];
    ble_services_service_construct_cb_t   service_construct;
    const int                             characteristic_count;
    const ble_characteristic_t            *characteristics;
};

bool ble_services_is_supported_service(const char *service_uuid);
bool ble_services_is_supported_characteristic(const char *service_uuid, const char *characteristic_uuid);

struct translation_context* ble_services_find_translation_context(const struct ble_device *device,
                                                                  const uint16_t object_id,
                                                                  const uint16_t instance_id,
                                                                  const uint16_t resource_id);
const ble_characteristic_t* ble_services_get_characteristic_descriptor_by_uuids(const char *service_uuid, const char *characteristic_uuid);
GVariant* ble_services_get_service_uuid_filter();
void ble_services_configure_translation_context(struct ble_device *device,
                                                const int sv_idx,
                                                const int ch_idx,
                                                const uint16_t object_id,
                                                const uint16_t instance_id,
                                                const uint16_t resource_id,
                                                const uint32_t characteristic_extra_flags);
void ble_services_free_translation_contexts(struct ble_device *device);
void ble_services_decode_and_write_characteristic_translation(const struct ble_device *ble_dev,
                                                              const int sv_idx,
                                                              const int ch_idx,
                                                              const uint8_t *value,
                                                              const size_t value_size);
void ble_services_construct_service(struct ble_device *device,
                                    const int sv_idx);
void ble_services_construct_characteristic(struct ble_device *device,
                                           const int sv_idx,
                                           const int ch_idx);
void ble_services_encode_characteristic_value(const struct ble_device *device,
                                              const struct translation_context *ctx,
                                              const uint8_t *current_characteristic_value,
                                              const size_t current_characteristic_size,
                                              const uint8_t *new_value,
                                              const size_t new_size,
                                              uint8_t **new_characteristic_value,
                                              size_t *new_characteristic_size);

#endif /* _PT_BLE_TRANSLATIONS_H_ */
