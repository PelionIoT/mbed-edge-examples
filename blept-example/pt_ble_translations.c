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
 * \file pt_ble_translations.c
 * \brief Translation functions for BLE devices, services and characteristics.
 */
#include <string.h>
#include <assert.h>
#include <math.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "pt-client-2/pt_api.h"
#include "pt-client-2/pt_device_object.h"
#include "devices.h"
#include "pt_edge.h"
#include "pt_ble_translations.h"
#include "pt_ble_supported_translations.h"
#include "examples-common-2/ipso_objects.h"
#include "byte-order/byte_order.h"

#define TRACE_GROUP "btsv"
#include "mbed-trace/mbed_trace.h"

/**
 * \brief Get handle to GATT service descriptor for given uuid if it exists.
 *
 * \param service_uuid GATT service uuid.
 * \return Returns pointer to the service descriptor if found, NULL otherwise.
 */
static const ble_service_t* ble_services_get_service_descriptor(const char *service_uuid)
{
    assert(service_uuid != NULL);

    int i = 0;
    for (i = 0; i < ble_services_count; i++) {
        if (strncasecmp(ble_services[i].uuid, service_uuid, FORMATTED_UUID_LEN) == 0) {
            return &ble_services[i];
        }
    }

    return NULL;
}

/**
 * \brief Get handle to GATT characteristic with given uuid if it exists within given GATT service descriptor.
 *
 * \param service GATT service descriptor.
 * \param characteristic_uuid GATT characteristic uuid.
 * \return Returns pointer to the GATT characteristic descriptor if found, NULL otherwise.
 */
static const ble_characteristic_t* ble_services_get_characteristic_descriptor(const ble_service_t *service, const char *characteristic_uuid)
{
    assert(service != NULL);
    assert(characteristic_uuid != NULL);

    int i = 0;
    for (i = 0; i < service->characteristic_count; i++) {
        if (strncasecmp(service->characteristics[i].uuid, characteristic_uuid, FORMATTED_UUID_LEN) == 0) {
            return &service->characteristics[i];
        }
    }

    return NULL;
}

struct translation_context* ble_services_find_translation_context(const struct ble_device *device,
                                                                  const uint16_t object_id,
                                                                  const uint16_t instance_id,
                                                                  const uint16_t resource_id)
{
    assert(device != NULL);
    ns_list_foreach(struct translation_context, ctx, &device->translations) {
        if (ctx->object_id == object_id &&
            ctx->object_instance_id == instance_id &&
            ctx->resource_id == resource_id) {
            return ctx;
        }
    }
    return NULL;
}

const ble_characteristic_t* ble_services_get_characteristic_descriptor_by_uuids(const char *service_uuid, const char *characteristic_uuid)
{
    if (service_uuid == NULL || characteristic_uuid == NULL) {
        return NULL;
    }

    const ble_service_t *service = ble_services_get_service_descriptor(service_uuid);
    if (service == NULL) {
        return NULL;
    }

    return ble_services_get_characteristic_descriptor(service, characteristic_uuid);
}

bool ble_services_is_supported_service(const char *service_uuid)
{
    if (ble_services_get_service_descriptor(service_uuid)) {
        return true;
    }
    return false;
}

bool ble_services_is_supported_characteristic(const char *service_uuid, const char *characteristic_uuid)
{
    if (ble_services_get_characteristic_descriptor_by_uuids(service_uuid, characteristic_uuid)) {
        return true;
    }
    return false;
}

GVariant* ble_services_get_service_uuid_filter()
{
    GVariant *ret_val = NULL;
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    int i = 0;
    for (i = 0; i < ble_services_count; i++) {
        g_variant_builder_add(&builder, "s", ble_services[i].uuid);
    }
    ret_val = g_variant_builder_end(&builder);
    return ret_val;

}

void ble_services_free_translation_contexts(struct ble_device *device)
{
    assert(device != NULL);
    ns_list_foreach_safe(struct translation_context, ctx, &device->translations) {
        ns_list_remove(&(device->translations), ctx);
        free(ctx);
    }
}

void ble_services_configure_translation_context(struct ble_device *device,
                                                const int sv_idx,
                                                const int ch_idx,
                                                const uint16_t object_id,
                                                const uint16_t instance_id,
                                                const uint16_t resource_id,
                                                const uint32_t characteristic_extra_flags)
{
    assert(device != NULL);

    struct translation_context *tc = calloc(1, sizeof(struct translation_context));
    if (tc == NULL) {
        // TODO: error handling
        return;
    }

    tr_info("svid=%d, chid=%d", sv_idx, ch_idx);
    tr_info("    Created translation of %s to /%s/%d/%d/%d",
            device->attrs.services[sv_idx].chars[ch_idx].uuid,
            device->device_id,
            object_id,
            instance_id,
            resource_id);

    tc->object_id = object_id;
    tc->object_instance_id = instance_id;
    tc->resource_id = resource_id;
    tc->characteristic_extra_flags = characteristic_extra_flags;
    tc->ch_idx = ch_idx;
    tc->sv_idx = sv_idx;
    ns_list_add_to_end(&(device->translations), tc);
}

void ble_services_construct_service(struct ble_device *device, const int sv_idx)
{
    assert(device != NULL);

    const struct ble_gatt_service *service = &device->attrs.services[sv_idx];
    const ble_service_t *service_descriptor = NULL;

    tr_info("Constructing local service translation for %s", service->dbus_path);
    service_descriptor = ble_services_get_service_descriptor(service->uuid);
    if (NULL != service_descriptor && NULL != service_descriptor->service_construct) {
        service_descriptor->service_construct(device, sv_idx, service_descriptor);
    }
}

void ble_services_construct_characteristic(struct ble_device *device,
                                           const int sv_idx,
                                           const int ch_idx)
{
    assert(device != NULL);

    const struct ble_gatt_service *service = &device->attrs.services[sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ch_idx];

    const ble_service_t *service_descriptor = NULL;
    const ble_characteristic_t *characteristic_descriptor = NULL;

    tr_info("    Constructing local characteristic translation for %s", characteristic->dbus_path);
    service_descriptor = ble_services_get_service_descriptor(service->uuid);
    if (NULL != service_descriptor) {
        characteristic_descriptor = ble_services_get_characteristic_descriptor(service_descriptor, characteristic->uuid);
        if (NULL != characteristic_descriptor && NULL != characteristic_descriptor->characteristic_construct) {
            characteristic_descriptor->characteristic_construct(device, sv_idx, ch_idx, characteristic_descriptor);
        }
    }
}

void ble_services_decode_and_write_characteristic_translation(const struct ble_device *ble_dev,
                                                              const int sv_idx,
                                                              const int ch_idx,
                                                              const uint8_t *value,
                                                              const size_t value_size)
{
    assert(ble_dev != NULL);
    assert(value != NULL);

    const struct ble_gatt_service *service = &ble_dev->attrs.services[sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ch_idx];

    // Get pointer to the characteristic descriptor where we can find the decode function
    const ble_service_t *service_descriptor = ble_services_get_service_descriptor(service->uuid);
    const ble_characteristic_t *char_descriptor = ble_services_get_characteristic_descriptor(service_descriptor, characteristic->uuid);
    if (NULL == char_descriptor || NULL == char_descriptor->characteristic_value_decode) {
        tr_warning("No descriptor or decoder for characteristic at %s", characteristic->dbus_path);
        return;
    }

    // Find all translation contexts for this characteristic
    ns_list_foreach(struct translation_context, ctx, &ble_dev->translations) {
        if (ctx->sv_idx == sv_idx && ctx->ch_idx == ch_idx) {
            // Call the characteristic decoder to decode value
            uint8_t *decoded_value = NULL;
            size_t decoded_value_len = 0;
            if (char_descriptor->characteristic_value_decode(ble_dev, ctx, value, value_size, &decoded_value, &decoded_value_len)) {
                // Write value to resource
                pt_device_set_resource_value(edge_get_connection_id(),
                                             ble_dev->device_id,
                                             ctx->object_id,
                                             ctx->object_instance_id,
                                             ctx->resource_id,
                                             decoded_value,
                                             decoded_value_len,
                                             free);
            }
        }
    }
}

void ble_services_encode_characteristic_value(const struct ble_device *device,
                                              const struct translation_context *ctx,
                                              const uint8_t *current_characteristic_value,
                                              const size_t current_characteristic_size,
                                              const uint8_t *new_value,
                                              const size_t new_size,
                                              uint8_t **new_characteristic_value,
                                              size_t *new_characteristic_size)
{
    assert(device != NULL);
    assert(ctx != NULL);
    assert(current_characteristic_value != NULL);
    assert(new_value != NULL);
    assert(new_characteristic_value != NULL);
    assert(new_characteristic_size != NULL);

    const struct ble_gatt_service *service = &device->attrs.services[ctx->sv_idx];
    const struct ble_gatt_char *characteristic = &service->chars[ctx->ch_idx];

    // Get dbus resource mapping
    tr_info("ble_services_encode_characteristic_value");
    tr_debug("write data %s", tr_array(new_value, new_size));

    const ble_characteristic_t *characteristic_descriptor = ble_services_get_characteristic_descriptor_by_uuids(service->uuid, characteristic->uuid);
    if (characteristic_descriptor == NULL || characteristic_descriptor->characteristic_value_encode == NULL) {
        tr_warning("No characteristic descriptor or encoder for %s", characteristic->dbus_path);
        return;
    }

    uint8_t *encoded_value = NULL;
    size_t encoded_size = 0;

    // Call characteristic encode function
    bool success = characteristic_descriptor->characteristic_value_encode(device,
                                                                          ctx,
                                                                          current_characteristic_value,
                                                                          current_characteristic_size,
                                                                          new_value,
                                                                          new_size,
                                                                          &encoded_value,
                                                                          &encoded_size);
    if (success) {
        *new_characteristic_value = encoded_value;
        *new_characteristic_size = encoded_size;
    }

    return;
}
