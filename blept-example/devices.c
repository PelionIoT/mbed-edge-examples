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
#include "devices.h"

#include "pt_edge.h"
#include "pt_ble.h"

#include <mbed-trace/mbed_trace.h>
#include <pt-client-2/pt_api.h>
#include <pt-client-2/pt_device_object.h>
#include "pt_ble_translations.h"

#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/time.h>

#define LIFETIME                                                                                                       \
    300 // this setting currently has no effect. The translated endpoints are tracked withing the parent Edge device
        // lifetime. Please see BLE_MAX_BACK_OFF_TIME_SECS for related configuration.
#define TRACE_GROUP     "BLEC"

// ============================================================================
// Global Variables
// ============================================================================
struct mept_devices global_devices;
pt_api_mutex_t *g_pt_mutex = NULL;

// ============================================================================
// Code
// ============================================================================

size_t devices_make_device_id(char *out_buf, size_t out_size, const char *prefix, const char *ble_id, const char *postfix)
{
    return snprintf(out_buf, out_size, "%s-%s-%s", prefix, ble_id, postfix);
}

/**
 * The following routine is used to map BLE UUIDs to datatypes.
 * It can be modified to map published uuids as well as
 * deployment-specific uuids.  This mapping allows the contents
 * of a characteristic to be viewed  in the correct format
 * (integer, string, etc.) when displayed on the Pelion cloud.
 * In addition to datatype, an LWM2M resource identifier may
 * optionally be specified. (See LWM2M documentation for
 * meaningful resource identifiers)
 *
 * The mapping implemented here is for a test BLE device used in
 * development of this protocol translator.  It should be
 * replaced in an actual final product and might better be
 * implemented through an external configuration file.
 */

static void map_uuid_to_datatype(const char   *uuid,
                                 BLE_DATATYPE *dtype,
                                 size_t       *data_size,
                                 uint16_t     *lwm2m_resource_id)
{
    (void)lwm2m_resource_id;

    if (0 == strcmp(uuid, "0000ed01-0000-1000-8000-00805f9b34fb")) {
        /* a 32-bit integer */
        *dtype = BLE_INTEGER;
        *data_size = 4;
        /* allow default lwm2m resource id*/
    } else if (0 == strcmp(uuid, "0000ed02-0000-1000-8000-00805f9b34fb")) {
        /* a string of up to 32 bytes*/
        *dtype = BLE_STRING;
        *data_size = 32;
        /* allow default lwm2m resource id*/
#ifdef MEPT_BLE_ADD_FAKE_DEVICES
    } else if (0 == strcmp(uuid, FAKE_CHAR1_UUID)) {
        *dtype = BLE_INTEGER;
        *data_size = sizeof(int);
    } else if (0 == strcmp(uuid, FAKE_CHAR2_UUID)) {
        *dtype = BLE_INTEGER;
        *data_size = sizeof(int);
#endif
    } else {
        /*for unknown uuids, we'll condsider them to be opaque structs and
          will allocate 512 bytes of storage, keepeing in mind that a characteristic
          of greater than 23 bytes would require a negotiated higher BLE MTU*/
        *dtype = BLE_STRUCT;
        *data_size = 512;
        /* allow default lwm2m resource id*/
    }
}

bool device_is_connected(struct ble_device *ble)
{
    bool ret_value;
    device_mutex_lock(ble);
    ret_value = (ble->flags & BLE_DEVICE_FLAG_CONNECTED) != 0;
    device_mutex_unlock(ble);
    return ret_value;
}

void device_set_connected(struct ble_device *ble, bool is_connected)
{
    tr_debug("    device_set_connected device_id: '%s' connected: %d", ble->device_id, is_connected);
    device_mutex_lock(ble);
    if (is_connected) {
        ble->flags |= BLE_DEVICE_FLAG_CONNECTED;
    } else {
        ble->flags &= ~BLE_DEVICE_FLAG_CONNECTED;
    }
    device_mutex_unlock(ble);
}

bool device_is_registered(struct ble_device *ble)
{
    bool ret_value;
    device_mutex_lock(ble);
    ret_value = (ble->flags & BLE_DEVICE_FLAG_REGISTERED) != 0;
    device_mutex_unlock(ble);
    return ret_value;
}

void device_set_registered(struct ble_device *ble, bool is_registered)
{
    tr_debug("    device_set_registered device_id: '%s' registered: %d", ble->device_id, is_registered);
    device_mutex_lock(ble);
    if (is_registered) {
        ble->flags |= BLE_DEVICE_FLAG_REGISTERED;
    } else {
        ble->flags &= ~BLE_DEVICE_FLAG_REGISTERED;
    }
    device_mutex_unlock(ble);
}

pthread_mutex_t *devices_get_mutex()
{
    return &(global_devices.mutex);
}

ble_device_list_t *devices_get_list()
{
    return &(global_devices.devices);
}

/**
 * \brief Find the device by DBUS object path from the devices list.
 *
 * \param dbus_path The DBUS object path.
 * \return The device if found.\n
 *         NULL is returned if the device is not found.
 */
struct ble_device *devices_find_device_by_dbus_path(const char *dbus_path)
{
    struct ble_device *ble = NULL;

    ns_list_foreach(struct ble_device, dev, devices_get_list())
    {
        if (0 == strcmp(dev->dbus_path, dbus_path)) {
            ble = dev;
            break;
        }
    }

    tr_debug("< devices_find_device_by_dbus_path dbus_path: '%s' device: %p", dbus_path, ble);

    return ble;
}

/**
 * \brief Find the device by device id from the devices list.
 *
 * \param device_id The device identifier.
 * \return The device if found.\n
 *         NULL is returned if the device is not found.
 */
struct ble_device *devices_find_device_by_device_id(const char *device_id)
{
    struct ble_device *ble = NULL;

    ns_list_foreach(struct ble_device, dev, devices_get_list()) {
        if (0 == strcmp(dev->device_id, device_id)) {
            ble = dev;
            break;
        }
    }

    tr_debug("< devices_find_device_by_device_id device_id: '%s' device: %p", device_id, ble);

    return ble;
}

static void device_free_char(struct ble_gatt_char *chara)
{
#if 1 == EXPERIMENTAL_NOTIFY_CHARACTERISTIC_SUPPORT
    ble_characteristic_stop_notify_proxy(chara);
#endif // EXPERIMENTAL_NOTIFY_CHARACTERISTIC_SUPPORT
    free(chara->dbus_path);
    free(chara->value);
}

static void device_free_service(struct ble_gatt_service *service)
{
    int i;
    for (i = 0; i < service->chars_count; i++) {
        device_free_char(service->chars + i);
    }
    free(service->dbus_path);
    free(service->chars);
}

static void device_free_services(struct ble_device *ble)
{
    int i;
    for (i = 0; i < ble->attrs.services_count; i++) {
        device_free_service(ble->attrs.services + i);
    }
    free(ble->attrs.services);
}

void device_stop_retry_timer(struct ble_device *ble)
{
    if (0 != ble->retry_timer_source) {
        g_source_remove(ble->retry_timer_source);
        ble->retry_timer_source = 0;
    }
}

static void device_free(struct ble_device *ble)
{
    free(ble->dbus_path);
    device_stop_retry_timer(ble);
    if (ble->proxy != NULL) {
        tr_debug("deleting device proxy %p for device %p device_id: '%s'", ble->proxy, ble, ble->device_id);
        g_object_unref(ble->proxy);
    }
    device_free_services(ble);
    ble_services_free_translation_contexts(ble);
    free(ble->json_list);
    free(ble->device_id);
    free(ble);
}

void devices_del_device(struct ble_device *ble)
{
    tr_debug("> devices_del_device %p device_id: '%s'", ble, ble->device_id);

    //TODO: Be sure that device mutex is not in locked state.
    pthread_mutex_destroy(&ble->mutex);

    ns_list_remove(devices_get_list(), ble);
    device_free(ble);
}

static pt_status_t
devices_reboot_callback(const connection_id_t connection_id,
                        const char *device_id,
                        const uint16_t object_id,
                        const uint16_t instance_id,
                        const uint16_t resource_id,
                        const uint8_t operation,
                        const uint8_t *value,
                        const uint32_t value_size,
                        void *userdata)
{
    (void)connection_id;
    (void)device_id;
    (void)object_id;
    (void)instance_id;
    (void)resource_id;
    (void)operation;
    (void)value;
    (void)value_size;
    tr_info("Example /3 device reboot resource executed.");
    return PT_STATUS_SUCCESS;
}

bool
devices_create_pt_device(const char *device_id,
                         const char *manufacturer,
                         const char *model_number,
                         const char *serial_number,
                         const char *device_type)
{
    tr_debug("    devices_create_pt_device device_id: '%s'", device_id);
    return edge_create_device(device_id, manufacturer, model_number, serial_number, device_type, LIFETIME, devices_reboot_callback);
}

void devices_link_device(struct ble_device *entry, const char *device_id)
{
    assert(entry != NULL);
    entry->device_id = strdup(device_id);
    ns_list_add_to_end(&(global_devices.devices), entry);
}

int devices_init()
{
    //init our device list and mutex
    ns_list_init(&(global_devices.devices));
    pthread_mutexattr_t Attr;
    pthread_mutexattr_init(&Attr);
    pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&global_devices.mutex, &Attr);

    return 0;
}

static uint64_t seconds_since_epoch()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec);
}

uint64_t devices_duration_in_sec_since_last_connection(struct ble_device *ble_dev)
{
    return seconds_since_epoch() - ble_dev->last_connected_timestamp_secs;
}

void device_update_last_connected_timestamp(struct ble_device *ble_dev)
{
    ble_dev->last_connected_timestamp_secs = seconds_since_epoch();
}

struct ble_device *device_create(const char *addr)
{
    struct ble_device *ble;
    tr_debug("> device_create addr: %s", addr);
    ble = malloc(sizeof(struct ble_device));
    assert(NULL != ble);
    memset(ble, 0, sizeof(struct ble_device));
    strncpy(ble->attrs.addr, addr, sizeof ble->attrs.addr);
    device_update_last_connected_timestamp(ble);
    ns_list_init(&(ble->translations));

    //init our mutex
    pthread_mutexattr_t Attr;

    pthread_mutexattr_init(&Attr);
    pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&ble->mutex, &Attr);
    tr_debug("< device_create ble: %p device_id: '%s'", ble, ble->device_id);

    return ble;
}

/*  Returns the service structure of the ble device with the given uuid,
 *  creating and adding it if necessary
 */
static struct ble_gatt_service *get_service(struct ble_device *ble,
                                            const char *uuid)
{
    int svc;

    for (svc = 0; svc < ble->attrs.services_count; svc++ ) {
        if (0 == strncmp(ble->attrs.services[svc].uuid, uuid, sizeof ble->attrs.services[svc].uuid)) {
            return &(ble->attrs.services[svc]);
        }
    }

    svc = ble->attrs.services_count;
    ble->attrs.services_count++;
    // BUG: we lose the previous pointer if realloc fails, causing a memory leak
    ble->attrs.services = realloc(ble->attrs.services,
                                  ble->attrs.services_count * sizeof (struct ble_gatt_service));

    assert(NULL != ble->attrs.services);
    memset(&ble->attrs.services[ble->attrs.services_count - 1], 0, sizeof(struct ble_gatt_service));
    strncpy(ble->attrs.services[svc].uuid, uuid, FORMATTED_UUID_LEN);

    return &(ble->attrs.services[svc]);
}

static struct ble_gatt_char *add_char_to_service(struct ble_gatt_service *srvc,
                                                 const char *uuid,
                                                 const char *dbus_path,
                                                 int properties,
                                                 BLE_DATATYPE dtype,
                                                 size_t dsize,
                                                 uint16_t resource_id,
                                                 GDBusProxy *proxy)
{
    tr_debug("    add_char_to_service uuid: %s, dbus_path: %s, proxy: %p", uuid, dbus_path, proxy);
    int ch = srvc->chars_count;
    // BUG: we lose the previous pointer if realloc fails, causing a memory leak
    srvc->chars = realloc(srvc->chars, (ch + 1) * sizeof(struct ble_gatt_char));
    assert(NULL != srvc->chars);
    memset(&srvc->chars[ch], 0, sizeof(struct ble_gatt_char));
    srvc->chars_count++;
    srvc->chars[ch].properties = properties;
    strncpy(srvc->chars[ch].uuid, uuid, sizeof(srvc->chars[ch].uuid));
    if (dbus_path != NULL) {
        srvc->chars[ch].dbus_path = (char *)malloc(strlen(dbus_path) + 1);
        assert(srvc->chars[ch].dbus_path != NULL);
        strcpy(srvc->chars[ch].dbus_path, dbus_path);
    }
    srvc->chars[ch].value = (uint8_t *)malloc(dsize);
    assert(srvc->chars[ch].value != NULL);
    memset(srvc->chars[ch].value, 0, dsize);
    srvc->chars[ch].value_length = 0;
    srvc->chars[ch].value_size = dsize;
    srvc->chars[ch].dtype = dtype;
    srvc->chars[ch].proxy = proxy;
    /*Note: Need to insure resource id's are unque for a given service
      If no resource_id is specified firm map code this is done by setting
      resource id equal to the index int the characteristic array.*/
    if (resource_id != 0) {
        srvc->chars[ch].resource_id = resource_id;
    } else {
        srvc->chars[ch].resource_id = (uint16_t)ch;
    }

    return &(srvc->chars[ch]);
}

struct ble_gatt_char * device_add_gatt_characteristic(struct ble_device *ble,
                                                      const char *srvc_uuid,
                                                      const char *srvc_dbus_path,
                                                      const char *char_uuid,
                                                      const char *char_dbus_path,
                                                      int char_properties,
                                                      GDBusProxy *proxy)
{
    BLE_DATATYPE dtype;
    size_t dsize;
    uint16_t resource_id = 0;
    struct ble_gatt_service *srvc = get_service(ble, srvc_uuid);
    struct ble_gatt_char *ret;


    if (srvc && srvc->dbus_path == NULL) {
        srvc->dbus_path = strdup(srvc_dbus_path);
    }

    tr_debug("--> device_add_gatt_characteristic(%p, %s, %s, %s, %d)", ble, srvc_uuid, char_uuid, char_dbus_path, char_properties);
    map_uuid_to_datatype(char_uuid, &dtype, &dsize, &resource_id);
    ret = add_char_to_service(srvc, char_uuid, char_dbus_path, char_properties, dtype, dsize, resource_id, proxy);
    tr_debug("<-- device_add_gatt_characteristic");

    return ret;
}

/* converts BLE services/characteristics to PT LwM2M resources */
int device_add_resources_from_gatt(struct ble_device *ble)
{
    struct ble_attrs *attrs;
    struct ble_gatt_char *c;
    struct ble_gatt_service *s;

    attrs = &ble->attrs;
    tr_debug("--> device_add_resources_from_gatt device_id: '%s'", ble->device_id);
    tr_info("    adding LwM2M resources for device %s", attrs->addr);

    tr_info("    service count: %d", attrs->services_count);
    for (int instance = 0; instance < attrs->services_count; ++instance) {
        s = &attrs->services[instance];
        tr_info("    service UUID=%s, OID=/%d/%d",
                s->uuid, IPSO_OID_BLE_SERVICE, instance);
        tr_info("    char count: %d", s->chars_count);
        for (int j = 0; j < s->chars_count; ++j) {
            c = &s->chars[j];
            tr_info("        characteristic UUID=%s, OID=/%d/%d/%d",
                    c->uuid, IPSO_OID_BLE_SERVICE, instance, j);
            uint8_t ops = 0;
            if (c->properties & BLE_GATT_PROP_PERM_READ) {
                ops |= OPERATION_READ;
            }
            if (c->properties & BLE_GATT_PROP_PERM_WRITE) {
                ops |= OPERATION_WRITE;
            }
            tr_info("    mapping ble char %s into lwm2m resource /%d/%d/%d with RW properties %d",
                    c->dbus_path,
                    IPSO_OID_BLE_SERVICE,
                    instance,
                    c->resource_id,
                    ops);
            Lwm2mResourceType rtype;
            switch (c->dtype) {
            case BLE_BOOLEAN:
                rtype = LWM2M_BOOLEAN;
                break;
            case BLE_INTEGER:
                rtype = LWM2M_INTEGER;
                break;
            case BLE_FLOAT:
                rtype = LWM2M_FLOAT;
                break;
            case BLE_STRING:
                rtype = LWM2M_STRING;
                break;
            case BLE_STRUCT:
            default:
                rtype = LWM2M_OPAQUE;
                break;
            }

            if (!edge_add_resource(ble->device_id,
                                   IPSO_OID_BLE_SERVICE,
                                   instance,
                                   c->resource_id,
                                   rtype,
                                   ops,
                                   c->value,
                                   c->value_size)) {
                tr_err("    Failed to create resource /%d/%d for service UUID %s",
                       IPSO_OID_BLE_SERVICE,
                       instance,
                       s->uuid);
                continue;
            }
        }
    }

    tr_info("    adding introspection resource");
    assert(ble->json_list == NULL);
    ble->json_list = json_list_device_services(ble);
    // BUG: This somehow causes our json_list buffer to become corrupted
    // TODO: Investigate and re-enable the map.
    if (!edge_add_resource(ble->device_id,
                           IPSO_OID_BLE_INTROSPECT,
                           0,
                           0,
                           LWM2M_STRING,
                           OPERATION_READ,
                           (uint8_t *)ble->json_list,
                           strlen(ble->json_list) + 1)) {
        tr_err("    Failed to create introspection resource /%d/0/0", IPSO_OID_BLE_INTROSPECT);
    }

    tr_debug("<-- device_add_resources_from_gatt");
    return 0;
}

/* Translates known BLE services/characteristics to PT LwM2M representation */
int device_add_known_translations_from_gatt(struct ble_device *ble)
{
    struct ble_attrs *attrs;
    struct ble_gatt_char *c;
    struct ble_gatt_service *s;

    attrs = &ble->attrs;
    tr_debug("--> device_add_known_translations_from_gatt device_id: '%s'", ble->device_id);
    tr_info("    adding LwM2M resources for device %s", attrs->addr);

    for (int i = 0; i < attrs->services_count; ++i) {
        s = &attrs->services[i];

        // Check if local service translation is supported
        if (!ble_services_is_supported_service(s->uuid)) {
            continue;
        }

        // Construct local service translation
        ble_services_construct_service(ble, i);

        // Construct local characteristic translations for this service
        for (int j = 0; j < s->chars_count; ++j) {
            c = &s->chars[j];
            if (!ble_services_is_supported_characteristic(s->uuid, c->uuid)) {
                continue;
            }

            ble_services_construct_characteristic(ble, i, j);
        }
    }

    tr_debug("<-- device_add_known_translations_from_gatt");
    return 0;
}

static void set_local_characteristic_value(struct ble_device *ble,
                                           const int          svc,
                                           const int          ch,
                                           const uint8_t     *val,
                                           const size_t       sz)
{
    if ((ble == NULL) ||
        (svc >= ble->attrs.services_count) ||
        (ch >= ble->attrs.services[svc].chars_count)) {
        return;
    }
    struct ble_gatt_char *charact = &(ble->attrs.services[svc].chars[ch]);

    if (sz < charact->value_size) {
        memset(charact->value, 0, charact->value_size);
        memcpy(charact->value, val, sz);
    } else {
        memcpy(charact->value, val, charact->value_size);
    }
    charact->value_length = sz;

    // We need to inform edge pt of new value
    edge_set_resource_value(ble->device_id, IPSO_OID_BLE_SERVICE, svc, charact->resource_id, val, sz);
}

void device_update_characteristic_resource_value(struct ble_device *ble,
                                                 const int          svc,
                                                 const int          ch,
                                                 const uint8_t     *val,
                                                 const size_t       sz)
{
    if ((ble == NULL) ||
        (svc >= ble->attrs.services_count) ||
        (ch >= ble->attrs.services[svc].chars_count)) {
        return;
    }
    struct ble_gatt_char *charact = &(ble->attrs.services[svc].chars[ch]);

    edge_set_resource_value(ble->device_id, IPSO_OID_BLE_SERVICE, svc, charact->resource_id, val, sz);
}

int device_write_characteristic(struct ble_device *ble,
                                const uint16_t object_id,
                                const uint16_t instance_id,
                                const uint16_t resource_id,
                                const uint8_t *value,
                                const size_t value_size)
{
    if (ble != NULL) {
        if (object_id != IPSO_OID_BLE_SERVICE) {
            // Update reosurces under any translated objects
            struct translation_context *ctx = ble_services_find_translation_context(ble, object_id, instance_id, resource_id);
            if (ctx) {
                uint8_t *translated_value = NULL;
                size_t translated_value_size = 0;
                // Encode the lwm2m value representation into gatt characteristic representation
                ble_services_encode_characteristic_value(ble,
                                                         ctx,
                                                         ble->attrs.services[ctx->sv_idx].chars[ctx->ch_idx].value,
                                                         ble->attrs.services[ctx->sv_idx].chars[ctx->ch_idx].value_size,
                                                         value,
                                                         value_size,
                                                         &translated_value,
                                                         &translated_value_size);
                if (translated_value != NULL) {
                    // Write into gatt characteristic
                    int rc = ble_write_characteristic(ble->attrs.services[ctx->sv_idx].chars[ctx->ch_idx].dbus_path,
                                                      translated_value,
                                                      translated_value_size);
                    if (rc == 0) {
                        // Update resource value in the service object
                        set_local_characteristic_value(ble, ctx->sv_idx, ctx->ch_idx, value, value_size);
                    }
                    free(translated_value);

                    return rc;
                }
            }

        }
        else if (instance_id < ble->attrs.services_count) {
            // Update resource under service object

            int ccnt = ble->attrs.services[instance_id].chars_count;
            int char_idx = 0;

            while (char_idx < ccnt) {
                if (ble->attrs.services[instance_id].chars[char_idx].resource_id == resource_id) {
                    break;
                }
                char_idx++;
            }
            if (char_idx < ccnt) {
                int rc = ble_write_characteristic(ble->attrs.services[instance_id].chars[char_idx].dbus_path,
                                                  value, value_size);
                if (rc == 0) {
                    set_local_characteristic_value(ble, instance_id, char_idx, value, value_size);
                    // Update the translation object value if this is supported characteristic
                    if (ble_services_is_supported_characteristic(ble->attrs.services[instance_id].uuid,
                                                                 ble->attrs.services[instance_id].chars[char_idx].uuid)) {
                        ble_services_decode_and_write_characteristic_translation(ble,
                                                                                 instance_id,
                                                                                 char_idx,
                                                                                 value,
                                                                                 value_size);
                    }
                }

                return rc;
            }
        }
    }

    tr_warn("Instance %d, resource %d does not map to a known characteristic",
            instance_id, resource_id);
    return -1;
}

void device_write_values_to_pt(struct ble_device *dev)
{
    edge_write_values(dev->device_id);
}

void device_register_device(struct ble_device *dev)
{
    if (!device_is_registered(dev)) {
        edge_register_device(dev->device_id);
    }
}

char *json_list_device_services(struct ble_device *ble)
{
    int srvc = 0, ch = 0;

    if (ble != NULL && ble->attrs.services != NULL) {
        for (srvc = 0; srvc < ble->attrs.services_count; srvc++) {
            ch += ble->attrs.services[srvc].chars_count;
        }
    }
    int retsize = JSON_FORMATTED_DEV_LEN + JSON_FORMATTED_SRVC_LEN * srvc + JSON_FORMATTED_CHAR_LEN * ch;
    char *retval = malloc(retsize);
    if (retval == NULL) {
        tr_err("Could not allocate memory for JSON string");
        return NULL;
    }

    strncpy(retval, JSON_DEVICE_FMT, retsize);
    retval[retsize - 1] = 0; // strncpy does not terminate the string if it overflows
    for (srvc = 0; srvc < ble->attrs.services_count; srvc++) {
        snprintf(retval + strlen(retval), retsize - strlen(retval),
                 JSON_SRVC_FMT,
                 srvc > 0 ? "," : "",
                 ble->attrs.services[srvc].uuid,
                 IPSO_OID_BLE_SERVICE,
                 srvc);
        for (ch = 0; ch < ble->attrs.services[srvc].chars_count; ch++) {
            snprintf(retval + strlen(retval), retsize - strlen(retval),
                     JSON_CHAR_FMT,
                     ch > 0 ? "," : "",
                     ble->attrs.services[srvc].chars[ch].uuid,
                     IPSO_OID_BLE_SERVICE,
                     srvc,
                     ble->attrs.services[srvc].chars[ch].resource_id);
        }
        strncat(retval, JSON_ARRAY_END, retsize - strlen(retval) - 1);
    }
    strncat(retval, JSON_ARRAY_END, retsize - strlen(retval) - 1);
    tr_debug("Wrote %zu bytes into json", strlen(retval));

    return retval;
}
