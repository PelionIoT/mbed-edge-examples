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
#ifndef MEPT_DEVICES_H
#define MEPT_DEVICES_H

#include "compat.h"

#include <pt-client-2/pt_api.h>

#include <gio/gio.h>
#include <ns_list.h>

#define BLE_ADDRESS_MAX_LENGTH 20
#define BLE_DEVICE_FLAG_REGISTERED (1 << 0)
#define BLE_DEVICE_FLAG_CONNECTED  (1 << 1)

/* BLE GATT R/W permissions */
#define BLE_GATT_PROP_PERM_READ    (1 << 0)
#define BLE_GATT_PROP_PERM_WRITE   (1 << 1)
#define BLE_GATT_PROP_PERM_NOTIFY  (1 << 2)

/* BLE GATT Encryption flags */
#define BLE_GATT_PROP_ENC_NONE     (1 << 2)
#define BLE_GATT_PROP_ENC_UNAUTH   (1 << 3)
#define BLE_GATT_PROP_ENC_AUTH     (1 << 4)

/* BLE GATT Authentication flags */
#define BLE_GATT_PROP_AUTH_NONE    (1 << 5)
#define BLE_GATT_PROP_AUTH_REQD    (1 << 6)


#ifdef MEPT_BLE_ADD_FAKE_DEVICES
#define FAKE_SRVC1_UUID "01020304-0506-0708-090a-0b0c0d0e0f10"
#define FAKE_CHAR1_UUID "11121314-1516-1718-191a-1b1c1d1e1f10"
#define FAKE_SRVC2_UUID "01020304-0506-0708-090a-0b0c0d0e0f11"
#define FAKE_CHAR2_UUID "11121314-1516-1718-191a-1b1c1d1e1f20"
#endif

typedef enum ble_device_type_ {
    BLE_DEVICE_UNKNOWN,
    BLE_DEVICE_PERSISTENT_GATT_SERVER, // Device with GATT server and persistent connection
    BLE_DEVICE_GAP_ADVERTISEMENT_ONLY  // Device with GAP advertisement only
} ble_device_type;

typedef enum ble_datatype {
    BLE_BOOLEAN = 1,
    BLE_INTEGER = 2,
    BLE_FLOAT   = 3,
    BLE_STRING  = 4,
    BLE_STRUCT  = 5
} BLE_DATATYPE;

struct ble_gatt_char {
    int properties; /* BLE_GATT_PROP_* */
    uint16_t handle;
    char uuid[FORMATTED_UUID_LEN + 1];
    char *dbus_path;
    BLE_DATATYPE dtype;
    uint16_t resource_id;
    GDBusProxy *proxy;
    uint8_t *value;
    size_t value_size; //allocated size for value
    size_t value_length; //actual length of store data (<= value_size)
};

struct ble_gatt_service {
    int chars_count;
    char *dbus_path;
    struct ble_gatt_char *chars;
    char uuid[FORMATTED_UUID_LEN + 1];
};

struct ble_attrs {
    int services_count;
    struct ble_gatt_service *services;
    char addr[BLE_ADDRESS_MAX_LENGTH + 1];
};

struct translation_context {
    uint16_t object_id;
    uint16_t object_instance_id;
    uint16_t resource_id;
    uint32_t characteristic_extra_flags;
    int ch_idx;
    int sv_idx;
    ns_list_link_t link;
};

typedef NS_LIST_HEAD(struct translation_context, link) translation_context_list_t;

struct ble_device {
    pthread_mutex_t mutex;
    ns_list_link_t link;
    // Used to remove the context when we cannot connect for a long time.
    uint64_t last_connected_timestamp_secs;
    // When the device is connected, we register it.
    // If the device gets disconnected, we try to reconnect to it and
    // it stays registered until max backoff time is reached.
    int flags;
    char *device_id;
    GDBusProxy *proxy;
    struct ble_attrs attrs;
    char *dbus_path;
    char *json_list;
    ble_device_type device_type;
    translation_context_list_t translations;
    guint retry_timer_source;
    int connection_retries;
    bool services_resolved;
};

typedef NS_LIST_HEAD(struct ble_device, link) ble_device_list_t;

struct mept_devices {
    ble_device_list_t devices;
    pthread_mutex_t mutex;
};

int devices_init();

void devices_link_device(struct ble_device *entry, const char *device_id);

void devices_free();

pthread_mutex_t *devices_get_mutex();

#if MUTEX_DEBUG_ENABLE == 1

// Mutex debug macros with additional tracing
#define device_mutex_lock(ble)                                          \
    {                                                                   \
        tr_debug("Lock device %s @ %s:%s:%d", ble->device_id, __func__, __FILE__, __LINE__); \
        pthread_mutex_lock(&ble->mutex);                        \
    }

#define device_mutex_unlock(ble)                                          \
    {                                                                   \
        tr_debug("Unlock device %s @ %s:%s:%d", ble->device_id, __func__, __FILE__, __LINE__); \
        pthread_mutex_unlock(&ble->mutex);                      \
    }


#define devices_mutex_lock() \
    { \
        tr_debug("Lock devices @ %s:%s:%d", __func__, __FILE__, __LINE__); \
        pthread_mutex_lock(devices_get_mutex()); \
    }

#define devices_mutex_unlock() \
    { \
        tr_debug("Unlock devices @ %s:%s:%d", __func__, __FILE__, __LINE__); \
        pthread_mutex_unlock(devices_get_mutex()); \
    }
#else

#define device_mutex_lock(ble)   pthread_mutex_lock(&ble->mutex)
#define device_mutex_unlock(ble) pthread_mutex_unlock(&ble->mutex)

#define devices_mutex_lock()     pthread_mutex_lock(devices_get_mutex())
#define devices_mutex_unlock()   pthread_mutex_unlock(devices_get_mutex())

#endif // MUTEX_DEBUG_ENABLE

ble_device_list_t *devices_get_list();

struct ble_device *devices_find_device_by_dbus_path(const char *dbus_path);
struct ble_device *devices_find_device_by_device_id(const char *device_id);

/* free */
void devices_del_device(struct ble_device *);

size_t devices_make_device_id(char *out_buf,
                              size_t out_size,
                              const char *prefix,
                              const char *ble_id,
                              const char *postfix);

uint64_t devices_duration_in_sec_since_last_connection(struct ble_device *ble_dev);
void device_update_last_connected_timestamp(struct ble_device *ble_dev);

bool devices_create_pt_device(const char *device_id,
                              const char *manufacturer,
                              const char *model_number,
                              const char *serial_number,
                              const char *device_type);

void device_stop_retry_timer(struct ble_device *);

/* Returns true if registered successfully with Edge and false otherwise. */
bool device_is_registered(struct ble_device *);

/* Sets the registered state of the device
 *
 * is_registered: true  = has successfully registered with Edge
 *                false = has not registered with Edge
 */
void device_set_registered(struct ble_device *, bool is_registered);

/* Returns true if BLE device is currently connected. Otherwise it returns false. */
bool device_is_connected(struct ble_device *ble);

/* Used to set current connected state of the device */
void device_set_connected(struct ble_device *ble, bool is_connected);

struct ble_device *device_create(const char *addr);

struct ble_gatt_char * device_add_gatt_characteristic(struct ble_device *ble,
                                                      const char *srvc_uuid,
                                                      const char *srvc_dbus_path,
                                                      const char *char_uuid,
                                                      const char *char_dbus_path,
                                                      int char_properties,
                                                      GDBusProxy *proxy);

int device_add_resources_from_gatt(struct ble_device *ble);

int device_add_known_translations_from_gatt(struct ble_device *ble);

int device_write_characteristic(struct ble_device *ble,
                                const uint16_t object_id,
                                const uint16_t instance_id,
                                const uint16_t resource_id,
                                const uint8_t *value,
                                const size_t value_size);

void device_update_ble_characteristics(struct ble_device *ble);
void device_update_characteristic_resource_value(struct ble_device *ble,
                                                 const int          svc,
                                                 const int          ch,
                                                 const uint8_t     *val,
                                                 const size_t       sz);
void device_write_values_to_pt(struct ble_device *ble);
void device_register_device(struct ble_device *dev);

/* return a malloc-ed string containing the json representation of a device's services and characteristics
 * according to the documentation in https://github.com/ARMmbed/ble-lwm2m-translation/blob/master/README.md
 * It is up to the caller to free() the string when no longer needed.
 */
char *json_list_device_services(struct ble_device *ble);

#endif /* MEPT_DEVICES_H */
