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
#include "pt_ble.h"
#include "pt_edge.h"
#include "devices.h"

#include <mbed-trace/mbed_trace.h>

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include "common/read_file.h"
#include "jansson.h"

#include "pt_ble_translations.h"

// ============================================================================
// Enums, Structs and Defines
// ============================================================================
#define BLUEZ_NAME "org.bluez"
#define ADAPTER_IFACE "org.bluez.Adapter1"
#define DEVICE_IFACE "org.bluez.Device1"
#define GATT_SERVICE_IFACE "org.bluez.GattService1"
#define GATT_CHARACTERISTIC_IFACE "org.bluez.GattCharacteristic1"
#define DBUS_PROPERTIES_IFACE "org.freedesktop.DBus.Properties"
#define POWERED_PROPERTY "Powered"
#define OBJ_PATH_GAS "/service000c/char000d"
#define OBJ_PATH_HUMIDITY "/service000f/char0010"
#define OBJ_PATH_TEMPERATURE "/service000f/char0014"
#define BLE_DEV "BLE"
#define BLE_DEVICE_ADDRESS_SIZE 18
#define BLE_DEVICE_NAME_MAX_LENGTH 33
#define MAX_PATH_LENGTH 256
#define MAX_VALUE_STRING_LENGHT 10
#define TRACE_GROUP "BLE"
#define BLE_VALUE_READ_INTERVAL 5000
#define BLE_RETRY_SLEEP_TIME_INITIAL_SECS 4
#define BLE_MAX_BACK_OFF_TIME_SECS 300 // After this the device gets unregistered
#define BLE_SLEEP_TIME_MULTIPLIER 2
#define MAX_CONNECTION_RETRY_TIME_SECONDS (3600 * 24)
#define BLE_MAX_CONNECTION_RETRIES 10000             // some upper limit just to prevent integer overflow
#define EXPERIMENTAL_ADVERTISEMENT_SUPPORT_ENABLED 0 // the feature is not ready yet.
#define BLUEZ_RECONNECT_RETRY_TIME_SECONDS 3         // Connection retry is tried in case the bluetooth daemon dies.
// ============================================================================
// Global Variables
// ============================================================================

typedef struct device_conf_entry {
    ns_list_link_t link;
    char *name;
    bool partial_match;
} device_conf_entry_t;

typedef NS_LIST_HEAD(device_conf_entry_t, link) device_conf_list_t;

static struct config {
    const char *postfix;
    const char *adapter;
    guint g_source_id_1;
    GMainLoop *g_loop;
    GDBusConnection *connection;
    char bluez_hci_path[64];
    device_conf_list_t *white_list_entries;
    int service_based_discovery;
} g_config;

// ============================================================================
// Static functions
// ============================================================================
static void ble_discover_characteristics(struct ble_device *ble_dev);
static void ble_proxy_connect(GDBusProxy *devProxy);
static void ble_start_reconnection_timer_or_unregister_device(struct ble_device *ble_dev);
static void device_conf_list_free(device_conf_list_t *list);

// ============================================================================
// Code
// ============================================================================


/**
 * \brief Set up the signal handlers through GLib for catching signals from OS.
 * This signal handler setup catches SIGTERM and SIGINT for shutting down
 * the protocol translator client gracefully through an event handler in glib.
 * In debug mode also SIGUSR2 is setup for shutting down for debugging purposes.
 */
bool pt_ble_setup_signals(void)
{
    if (g_unix_signal_add(SIGTERM, pt_ble_graceful_shutdown, (gpointer)SIGTERM) == 0) {
        return false;
    }
    if (g_unix_signal_add(SIGINT, pt_ble_graceful_shutdown, (gpointer)SIGINT) == 0) {
        return false;
    }
#ifdef DEBUG
    tr_info("Setting support for SIGUSR2");
    if (g_unix_signal_add(SIGUSR2, pt_ble_graceful_shutdown, (gpointer)SIGUSR2) == 0) {
        return false;
    }
#endif
    return true;
}


static bool ble_device_is_connected(GDBusProxy *proxy)
{
    GVariant *connected;
    bool ret = false;

    connected = g_dbus_proxy_get_cached_property(proxy, "Connected");
    if (connected != NULL) {
        assert(g_variant_is_of_type(connected, G_VARIANT_TYPE_BOOLEAN));
        ret = g_variant_get_boolean(connected);
        g_variant_unref(connected);
    }

    return ret;
}

static bool ble_services_are_resolved(GDBusProxy *proxy)
{
    GVariant *resolved;
    bool ret = false;

    resolved = g_dbus_proxy_get_cached_property(proxy, "ServicesResolved");
    if (resolved != NULL) {
        assert(g_variant_is_of_type(resolved, G_VARIANT_TYPE_BOOLEAN));
        ret = g_variant_get_boolean(resolved);
        g_variant_unref(resolved);
    }

    return ret;
}

static void print_proxy_properties(GDBusProxy *proxy)
{
    gchar **property_names;
    guint n;

    tr_info("    properties:\n");
    property_names = g_dbus_proxy_get_cached_property_names(proxy);
    for (n = 0; property_names != NULL && property_names[n] != NULL; n++) {
        const gchar *key = property_names[n];
        GVariant *value;
        gchar *value_str;
        value = g_dbus_proxy_get_cached_property(proxy, key);
        value_str = g_variant_print(value, TRUE);
        tr_info ("      %s -> %s", key, value_str);
        g_variant_unref(value);
        g_free(value_str);
    }
    g_strfreev(property_names);
}

static void ble_debug_print_char(struct ble_gatt_char *chara)
{
    tr_debug("            properties = %d", chara->properties);
    tr_debug("            uuid = %s", chara->uuid);
    (void)chara;
}

static void ble_debug_print_service(struct ble_gatt_service *service)
{
    tr_debug("        chars_count = %d", service->chars_count);
    tr_debug("        chars = %p", service->chars);
    for (int i = 0; i < service->chars_count; i++) {
        ble_debug_print_char(service->chars + i);
    }
    tr_debug("        uuid = %s", service->uuid);
}

static void ble_debug_print_device(struct ble_device *ble_dev)
{
    tr_debug("--> ble_debug_print_device");
    tr_debug("    flags = %x", ble_dev->flags);
    tr_debug("    device_id = %p", ble_dev->device_id);
    tr_debug("    proxy = %p", ble_dev->proxy);
    tr_debug("    attrs.services_count = %d", ble_dev->attrs.services_count);
    tr_debug("    attrs.services = %p", ble_dev->attrs.services);
    for (int i = 0; i < ble_dev->attrs.services_count; i++) {
        ble_debug_print_service(ble_dev->attrs.services + i);
    }
    tr_debug("    attrs.addr = %s", ble_dev->attrs.addr);
    tr_debug("<-- ble_debug_print_device");
}

GVariant* ble_get_property(const char *dbus_path, const char *dbus_interface, const char *property_name)
{
    GError *gerr = NULL;
    GDBusProxy *proxy = g_dbus_proxy_new_sync(g_config.connection,
                                              G_DBUS_PROXY_FLAGS_NONE,
                                              NULL,
                                              BLUEZ_NAME,
                                              dbus_path,
                                              DBUS_PROPERTIES_IFACE,
                                              NULL,
                                              &gerr);

    if (NULL == proxy || !G_IS_DBUS_PROXY(proxy)) {
        tr_debug("Could not get proxy for reading property, error was %s (code %d)", gerr->message, gerr->code);
        g_clear_error(&gerr);
        return NULL;
    }
    g_clear_error(&gerr);

    GVariant *params = g_variant_new("(ss)", dbus_interface, property_name);
    GVariant *result = g_dbus_proxy_call_sync(proxy,
                                              "Get",
                                              params,
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &gerr);

    GVariant *ret = NULL;
    if (result == NULL) {
        tr_debug("Error when reading property, %s (code %d)", gerr->message, gerr->code);
        g_clear_error(&gerr);
    } else {

        // result should be tuple with first item being the actual property value
        g_variant_get(result, "(v)", &ret);

        // Get reference to the variant so that we can unref the result tuple
        ret = g_variant_ref(ret);

        g_variant_unref(result);
    }
    g_object_unref(proxy);

    return ret;
}

bool ble_set_property(const char *dbus_path, const char *dbus_interface, const char *property_name, GVariant *value)
{
    bool ret = false;
    GError *gerr = NULL;
    GDBusProxy *proxy = g_dbus_proxy_new_sync(g_config.connection,
                                              G_DBUS_PROXY_FLAGS_NONE,
                                              NULL,
                                              BLUEZ_NAME,
                                              dbus_path,
                                              DBUS_PROPERTIES_IFACE,
                                              NULL,
                                              &gerr);

    if (NULL == proxy || !G_IS_DBUS_PROXY(proxy)) {
        tr_debug("Could not get proxy for writing property, error was %s (code %d)", gerr->message, gerr->code);
        g_variant_unref(value);
        g_clear_error(&gerr);
        return ret;
    }
    g_clear_error(&gerr);

    GVariant *params = g_variant_new("(ssv)", dbus_interface, property_name, value);
    GVariant *result = g_dbus_proxy_call_sync(proxy,
                                              "Set",
                                              params,
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &gerr);

    if (result == NULL) {
        tr_debug("Error when writing property, %s (code %d)", gerr->message, gerr->code);
        g_clear_error(&gerr);
    }
    else {
        ret = true;
        g_variant_unref(result);
    }

    g_object_unref(proxy);

    return ret;
}

/**
 * \brief check if device object proxy
 *
 * This function checks if we got the device interface
 *
 * \params a proxy to check
 *
 */
static bool ble_is_device_interface_proxy(GDBusProxy *proxy)
{
    bool ret = false;
    const gchar *interface_name;

    assert(G_IS_DBUS_PROXY(proxy));
    interface_name = g_dbus_proxy_get_interface_name(proxy);
    if (interface_name != NULL) {
        ret = (strcmp(interface_name, DEVICE_IFACE) == 0);
    } else {
        tr_err("Failed to get proxy interface name");
    }
    return ret;
}

// Copies the BT address of a device proxy to buf, up to size bytes
static int ble_device_proxy_get_address(char *buf, size_t size, GDBusProxy *proxy)
{
    GVariant *bt_address_property = NULL;
    const gchar *bt_address;
    int ret = 0;

    /* query the BLE address */
    bt_address_property = g_dbus_proxy_get_cached_property(proxy, "Address");
    if (bt_address_property == NULL) {
        tr_err("BLE device has no Address property.");
        ret = -1;
        goto out;
    }

    bt_address = g_variant_get_string(bt_address_property, NULL);
    strncpy(buf, bt_address, size);
    buf[size - 1] = 0;

out:
    if (bt_address_property != NULL) {
        g_variant_unref(bt_address_property);
    }

    return ret;
}

static struct ble_device *ble_find_device_from_address(const gchar *bt_address)
{
    struct ble_device *ble_dev = NULL;
    char ble_device_id[BLE_DEVICE_NAME_MAX_LENGTH] = {0};
    devices_make_device_id(ble_device_id, sizeof(ble_device_id), BLE_DEV, bt_address, g_config.postfix);
    ble_dev = devices_find_device_by_device_id(ble_device_id);
    return ble_dev;
}

// Tries to find a device based on a proxy.  Can return NULL.
static struct ble_device *ble_find_device_from_proxy(GDBusProxy *proxy)
{
    gchar bt_address[BLE_DEVICE_ADDRESS_SIZE] = {0};
    struct ble_device *ble_dev = NULL;

    assert(proxy != NULL);
    assert(G_IS_DBUS_PROXY(proxy));

    if (!ble_is_device_interface_proxy(proxy)) {
        goto out;
    }

    if (ble_device_proxy_get_address(bt_address, sizeof bt_address, proxy) != 0) {
        goto out;
    }
    ble_dev = ble_find_device_from_address(bt_address);

out:
    return ble_dev;
}

static void ble_remove_device_done(GObject *source_object,
                                   GAsyncResult *res,
                                   gpointer user_data)
{
    GDBusProxy *proxy;
    GVariant *ret;
    GError *err = NULL;
    struct ble_device *ble_dev = NULL;

    tr_debug("--> ble_remove_device_done");

    assert(source_object != NULL);
    proxy = (GDBusProxy *)source_object;
    assert(G_IS_DBUS_PROXY(proxy));

    ret = g_dbus_proxy_call_finish(proxy, res, &err);
    if (err == NULL) {
        assert(ret != NULL);
        g_variant_unref(ret);
    } else {
        assert(ret == NULL);
        tr_debug("    Failed to remove device: %s, %d.", err->message, err->code);
    }

    devices_mutex_lock();

    ble_dev = devices_find_device_by_device_id(user_data);
    if (ble_dev != NULL) {
        bool call_succeeded = edge_unregister_device(ble_dev, true /* remove_device_context */);
        if (!call_succeeded) {
            pt_edge_del_device(ble_dev);
        }
    }

    devices_mutex_unlock();

    free(user_data);

    tr_debug("<-- ble_remove_device_done");
}

void ble_remove_device(struct ble_device *ble_dev)
{
    GDBusProxy *proxy;
    GError *err = NULL;
    assert(ble_dev != NULL);
    device_stop_retry_timer(ble_dev);

    // Method is on the adapter interface.
    assert(g_config.connection != NULL);
    proxy = g_dbus_proxy_new_sync(g_config.connection,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          NULL,
                                          BLUEZ_NAME,
                                          g_config.bluez_hci_path,
                                          ADAPTER_IFACE,
                                          NULL,
                                          &err);
    if (!G_IS_DBUS_PROXY(proxy)) {
        tr_err("Adapter %s interface not available on dbus: %s", ADAPTER_IFACE, err->message);
        g_clear_error(&err);
        return;
    }

    g_dbus_proxy_call(proxy,
                      "RemoveDevice",
                      g_variant_new("(o)", ble_dev->dbus_path),
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      ble_remove_device_done,
                      strdup(ble_dev->device_id));

}

static void ble_on_connect(GDBusProxy *proxy)
{
    struct ble_device *ble_dev = NULL;

    assert(proxy != NULL);
    assert(G_IS_DBUS_PROXY(proxy));
    assert(ble_is_device_interface_proxy(proxy));

    devices_mutex_lock();
    ble_dev = ble_find_device_from_proxy(proxy);
    if (ble_dev == NULL) {
        tr_warn("Connected a device that we don't know about?");
        goto out;
    }

    tr_info("BLE device %s connected!", ble_dev->attrs.addr);
    device_set_connected(ble_dev, true);
out:
    devices_mutex_unlock();
    return;
}

static void ble_on_disconnect(GDBusProxy *proxy)
{
    struct ble_device *ble_dev = NULL;

    assert(proxy != NULL);
    assert(G_IS_DBUS_PROXY(proxy));
    assert(ble_is_device_interface_proxy(proxy));

    devices_mutex_lock();
    ble_dev = ble_find_device_from_proxy(proxy);
    if (ble_dev == NULL) {
        tr_warn("Disconnected a device that we don't know about?");
        goto out;
    }

    tr_info("BLE device %s disconnected!", ble_dev->attrs.addr);
    device_set_connected(ble_dev, false);
    // Try to reconnect a few times.
    ble_start_reconnection_timer_or_unregister_device(ble_dev);
out:
    devices_mutex_unlock();
    return;
}


static void ble_on_services_resolved(GDBusProxy *proxy)
{
    struct ble_device *ble_dev;

    if (edge_is_connected()) {

        devices_mutex_lock();

        assert(proxy != NULL);
        assert(G_IS_DBUS_PROXY(proxy));
        assert(ble_is_device_interface_proxy(proxy));

        ble_dev = ble_find_device_from_proxy(proxy);
        if ((ble_dev != NULL)) {
            if (!ble_dev->services_resolved) {
                tr_info("Discovering BLE properties");
                ble_discover_characteristics(ble_dev);
                ble_debug_print_device(ble_dev);

                /* convert BLE properties to PT resources */
                device_add_resources_from_gatt(ble_dev);
                device_add_known_translations_from_gatt(ble_dev);

                // TODO: We register this late because the resources must
                // all be present at registration time.  Try to figure out why.
                ble_dev->services_resolved = true;
            }
            device_register_device(ble_dev);
        } else {
            tr_warn("Resolved services on a device that we don't know about?");
        }

        devices_mutex_unlock();
    }
}

#if 1 == EXPERIMENTAL_ADVERTISEMENT_SUPPORT_ENABLED
static void ble_handle_advertisement(GDBusProxy *proxy, GVariant *advertisement_data)
{
    devices_mutex_lock();

    struct ble_device *ble = ble_find_device_from_proxy(proxy);
    if (edge_is_connected() && !edge_device_exists(ble->device_id)) {
        /* create the Edge PT device context */
        if (!devices_create_pt_device(ble->device_id,              /* device ID */
                                      "ARM",                       /* manufacturer */
                                      "mept-ble",                  /* model number */
                                      ble->attrs.addr,             /* serial number */
                                      "mept-ble-advertisement")) { /* device type */
            tr_err("Failed to create pt device context");
            devices_del_device(ble);
            goto out;
        }
        else {
            tr_debug("Created GAP advertising device %s", ble->device_id);
        }
    }

    if (edge_device_exists(ble->device_id)) {
        if (!device_is_registered(ble)) {
            device_register_device(ble);
        }

        // TODO: Parse advertisement data and update resources
    }

out:
    devices_mutex_unlock();
}
#endif

static void ble_properties_changed(GDBusProxy *proxy, GVariant *changed_properties, GStrv invalidated_properties, gpointer user_data)
{
    GVariantIter iter;
    const gchar *key;
    GVariant *value;
    gchar *s;

    assert(changed_properties != NULL);
    assert(invalidated_properties != NULL);
    (void)user_data;
    (void)invalidated_properties;

    g_variant_iter_init(&iter, changed_properties);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
        s = g_variant_print(value, TRUE);
        if (strncmp(key, "Connected", 10) == 0) {
            assert(g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN));
            if (g_variant_get_boolean(value)) {
                tr_debug("    %s: Connected -> 1", g_dbus_proxy_get_object_path(proxy));
                ble_on_connect(proxy);
            } else {
                tr_debug("    %s: Connected -> 0", g_dbus_proxy_get_object_path(proxy));
                ble_on_disconnect(proxy);
            }
        }
        if (strncmp(key, "ServicesResolved", 17) == 0) {
            assert(g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN));
            if (g_variant_get_boolean(value)) {
                tr_debug("    %s: ServicesResolved -> 1", g_dbus_proxy_get_object_path(proxy));
                ble_on_services_resolved(proxy);
            } else {
                tr_debug("    %s: ServicesResolved -> 0", g_dbus_proxy_get_object_path(proxy));
            }
        }
#if 1 == EXPERIMENTAL_ADVERTISEMENT_SUPPORT_ENABLED
        // Experimental advertisement support
        if (strncmp(key, "ServiceData", sizeof("ServiceData")) == 0) {
            char *servicedata = g_variant_print(value, TRUE);
            tr_debug("    %s: ServiceData -> %s", g_dbus_proxy_get_object_path(proxy), servicedata);
            g_free(servicedata);

            ble_handle_advertisement(proxy, value);
        }
#endif
        g_variant_unref(value);
        g_free(s);
    }
}

static void ble_create_device_context(GDBusProxy *proxy, ble_device_type device_type)
{
    char ble_device_id[BLE_DEVICE_NAME_MAX_LENGTH] = {0};
    char bt_address[BLE_DEVICE_ADDRESS_SIZE] = {0};
    struct ble_device *ble_dev = NULL;

    if (ble_device_proxy_get_address(bt_address, sizeof bt_address, proxy) != 0) {
        return;
    }
    tr_info("----> ble_create_device_context %s.", bt_address);

    devices_make_device_id(ble_device_id, sizeof(ble_device_id), BLE_DEV, bt_address, g_config.postfix);
    tr_debug("    ble_device_id = %s", ble_device_id);

    /* see if we already know about this device */
    devices_mutex_lock();
    ble_dev = devices_find_device_by_device_id(ble_device_id);
    devices_mutex_unlock();
    if (NULL != ble_dev) {
        tr_debug("    Device id %s already tracked.", ble_device_id);
        return;
    }
    tr_debug("    Device is new.");

    /* create our ble device context */
    ble_dev = device_create(bt_address);
    if (NULL == ble_dev) {
        tr_err("Failed to allocate ble device context.");
        return;
    }
    tr_debug("    Device context created.");

    /* store the proxy handle */
    ble_dev->proxy = proxy;
    ble_dev->dbus_path = strdup(g_dbus_proxy_get_object_path(proxy));

    /* set type */
    ble_dev->device_type = device_type;

    /* set up the device attributes */
    tr_debug("    address = %s", ble_dev->attrs.addr);

    /* add the device to our global list */
    devices_mutex_lock();
    devices_link_device(ble_dev, ble_device_id);
    devices_mutex_unlock();
    tr_info("<---- ble_create_device_context device: %p device_id: '%s' bt_address: %s.", ble_dev, ble_dev->device_id, bt_address);
}

// Used to count the timeout in milliseconds for the retry with count starting from 1
static uint32_t ble_back_off_time_in_ms(int32_t retry_index)
{
    assert(retry_index > 0);
    int32_t sleep_time = BLE_RETRY_SLEEP_TIME_INITIAL_SECS;

    while (retry_index > 1 && (sleep_time < BLE_MAX_BACK_OFF_TIME_SECS)) {
        sleep_time = sleep_time + sleep_time * BLE_SLEEP_TIME_MULTIPLIER;
        retry_index--;
    }

    return sleep_time * 1000;
}

static gboolean ble_retry_connect(gpointer data)
{
    struct ble_device *ble_dev = (struct ble_device *) data;
    tr_debug("--> ble_retry_connect device_id: '%s' proxy: %p", ble_dev->device_id, ble_dev->proxy);
    ble_proxy_connect(ble_dev->proxy);
    ble_dev->retry_timer_source = 0;
    tr_debug("<-- ble_retry_connect");
    return G_SOURCE_REMOVE;
}

static void ble_start_reconnection_timer_or_unregister_device(struct ble_device *ble_dev)
{
    if (0 == ble_dev->retry_timer_source) {
        bool remove_device_context = false;
        if (ble_dev->connection_retries < BLE_MAX_CONNECTION_RETRIES) {
            ble_dev->connection_retries += 1;
        }
        tr_debug("--> ble_start_reconnection_timer_or_unregister_device device id: '%s' retry index: %d",
                 ble_dev->device_id,
                 ble_dev->connection_retries);

        uint32_t retry_time_out_in_ms = ble_back_off_time_in_ms(ble_dev->connection_retries);
        uint64_t duration_since_connection_seconds = devices_duration_in_sec_since_last_connection(ble_dev);

        if (retry_time_out_in_ms > (BLE_MAX_BACK_OFF_TIME_SECS * 1000)) {
            remove_device_context = (duration_since_connection_seconds >= MAX_CONNECTION_RETRY_TIME_SECONDS);
            if (duration_since_connection_seconds >= BLE_MAX_BACK_OFF_TIME_SECS) {
                // Unregister the device, because it's not reachable.
                tr_err("    Unregistering device: '%s' due to maximum retry time in seconds: %d",
                       ble_dev->device_id,
                       BLE_MAX_BACK_OFF_TIME_SECS);
                bool call_succeeded = edge_unregister_device(ble_dev, remove_device_context);
                if (!call_succeeded && remove_device_context) {
                    pt_edge_del_device(ble_dev);
                }
            }
        }
        if (!remove_device_context) {
            tr_info("    retrying in %d ms.", retry_time_out_in_ms);
            ble_dev->retry_timer_source = g_timeout_add_full(G_PRIORITY_HIGH,
                                                             retry_time_out_in_ms,
                                                             ble_retry_connect,
                                                             ble_dev,
                                                             NULL);
        }
        tr_debug("<-- ble_start_reconnection_timer_or_unregister_device");
    } else {
        tr_debug("ble_start_reconnection_timer_or_unregister_device (timer already running) device id: '%s' retry "
                 "index: %d",
                 ble_dev->device_id,
                 ble_dev->connection_retries);
    }
}

static void ble_connect_done(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GDBusProxy *proxy = (GDBusProxy *)source_object;
    GVariant *ret = NULL;
    GError *err = NULL;
    struct ble_device *ble_dev = NULL;
    char *device_id = NULL;

    assert(proxy != NULL);
    (void)user_data;
    assert(user_data == NULL);

    ret = g_dbus_proxy_call_finish(proxy, res, &err);
    devices_mutex_lock();
    ble_dev = ble_find_device_from_proxy(proxy);
    if (ble_dev) {
        device_id = ble_dev->device_id;
    }
    if (err != NULL) {
        assert(ret == NULL);
        const gchar *path = g_dbus_proxy_get_object_path(proxy);
        tr_warn("--> ble_connect_done error: device proxy: %p device: %p device id: '%s'    Failed to connect to %s: "
                "%s, %d.",
                proxy,
                ble_dev,
                device_id,
                path,
                err->message,
                err->code);
        if (ble_dev) {
            ble_start_reconnection_timer_or_unregister_device(ble_dev);
        }
        goto out;
    }

    if (ble_dev == NULL) {
        tr_debug("--> ble_connect_done no device: device proxy: %p", proxy);
        goto out;
    }
    tr_debug("--> ble_connect_done success: device proxy: %p device: %p device id: '%s'", proxy, ble_dev, device_id);

    // The connection succeeded. Reset the reconnection counter.
    device_update_last_connected_timestamp(ble_dev);
    ble_dev->connection_retries = 0;

    if (edge_is_connected()) {
        /* create the Edge PT device context */
        if (!devices_create_pt_device(device_id,           /* device ID */
                                      "ARM",               /* manufacturer */
                                      "mept-ble",          /* model number */
                                      ble_dev->attrs.addr, /* serial number */
                                      "mept-ble")) {       /* device type */
            tr_err("Failed to create pt device context");
            devices_del_device(ble_dev);
            goto out;
        }
    }
    else {
        // If protocol translator is not yet connected we have to wait until it's connected
        // before we can create the device in edge...
        tr_debug("    Edge not connected, waiting...");
    }

    // If we're previously connected to this, then services are already resolved.
    // Otherwise, they'll resolve soon and we'll handle it then.
    if (edge_is_connected() && ble_services_are_resolved(proxy)) {
        tr_debug("    Device services have already resolved, processing now. device_id: '%s'", device_id);
        ble_on_services_resolved(proxy);
    } else {
        tr_debug("    Device services are not yet resolved, waiting... device_id: %s", device_id);
    }

out:
    tr_debug("<-- ble_connect_done device: %p, device_id: '%s'", ble_dev, device_id);
    devices_mutex_unlock();
    if (ret != NULL) {
        g_variant_unref(ret);
    }
}

/**
 * \brief reconnects BLE device
 * This function reconnects , it has 3 tries till give up
 *
 * \param devProxy, node to reconnect
 *
 */
static void ble_proxy_connect(GDBusProxy *devProxy)
{
    tr_debug("--> ble_proxy_connect %p", devProxy);
    tr_info("    Connecting to device %s", g_dbus_proxy_get_object_path(devProxy));
    g_dbus_proxy_call(devProxy,
                      "Connect",
                      NULL,
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      ble_connect_done,
                      NULL);
    tr_debug("<-- ble_proxy_connect %p", devProxy);
}


/**
 * \brief connects to BLE device by path
 * This function connects and check for valid device.
 * Creates BLE node (GDBusProxy) , it would be set to device as UserData.
 * Later we will use this node associated with device to read and write
 * data to it.
 *
 * \param addr, DBUS path to BLE device
 *
 */
static GDBusProxy *ble_create_device_proxy(const char *addr)
{
    GError *err;
    GDBusProxy *devProxy;

    tr_debug("--> ble_create_device_proxy: addr: %s", addr);
    err = NULL;

    assert(g_config.connection != NULL);
    devProxy = g_dbus_proxy_new_sync(g_config.connection,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             NULL,
                                             BLUEZ_NAME,
                                             addr,
                                             DEVICE_IFACE,
                                             NULL,
                                             &err);
    if (!G_IS_DBUS_PROXY(devProxy)) {
        tr_err("    Device %s, not available: %s", addr, err->message);
        g_clear_error(&err);
        return NULL;
    }

    gulong prop_handler = g_signal_connect(devProxy, "g-properties-changed", G_CALLBACK(ble_properties_changed), NULL);
    if (prop_handler == 0) {
        tr_err("    Could not setup g-properties-changed signal! addr: %s", addr);
        g_object_unref(devProxy);
        return NULL;
    }

    // print_proxy_properties(devProxy);
    (void)print_proxy_properties;

    tr_debug("<-- ble_create_device_proxy addr: %s proxy: %p", addr, devProxy);
    return devProxy;
}


/**
 * \brief check if device interface
 *
 * This function checks if we got the device interface
 *
 * \params a object to check
 *
 */
static bool ble_is_device(GDBusObject *object)
{
    GDBusInterface *interface;
    bool ret = false;

    assert(G_IS_OBJECT(object));
    interface = g_dbus_object_get_interface(object, DEVICE_IFACE);
    if (interface != NULL) {
       ret = true;
        g_object_unref(interface);
    }
    return ret;
}

/**
 * \brief Check whether given device advertises or has resolved supported GATT services.
 *
 * \return Return true if device has supported services, false otherwise.
 */
static bool ble_identify_device_services(GDBusProxy *device_proxy)
{
    assert(G_IS_DBUS_PROXY(device_proxy));

    GVariant *uuid_list = g_dbus_proxy_get_cached_property(device_proxy, "UUIDs");
    if (uuid_list == NULL) {
        // No advertised or resolved service UUIDs for device
        return false;
    }

    // Iterate the UUIDs
    bool success = false;
    GVariantIter iter;
    g_variant_iter_init(&iter, uuid_list);
    GVariant *uuid;
    while ((uuid = g_variant_iter_next_value(&iter))) {
        if (g_variant_type_equal(g_variant_get_type(uuid), G_VARIANT_TYPE_STRING)) {
            if (ble_services_is_supported_service(g_variant_get_string(uuid, NULL))) {
                // Device has services we support
                success = true;
                g_variant_unref(uuid);
                break;
            }
        }
        g_variant_unref(uuid);
    }

    return success;
}

/**
 * \brief Determine if device is supported based on device whitelist configuration.
 *
 * \return Return ble_device_type enum, should return BLE_DEVICE_UNKNOWN if device is
 *         not identified return one of the other values depending on the type of
 *         identified device.
 */
static ble_device_type ble_identify_device_using_whitelist(GDBusProxy *device_proxy)
{
    tr_debug("ble_identify_device_using_whitelist device_proxy: %p", device_proxy);
    assert(G_IS_DBUS_PROXY(device_proxy));

    GVariant *name_prop;
    GVariant *address_prop = NULL;
    ble_device_type ret = BLE_DEVICE_UNKNOWN;

    name_prop = g_dbus_proxy_get_cached_property(device_proxy, "Name");
    if (name_prop && g_variant_type_equal(g_variant_get_type(name_prop), G_VARIANT_TYPE_STRING)) {
        const char *name = g_variant_get_string(name_prop, NULL);
        tr_debug("Trying to identify device with name '%s'", name);
        if (g_config.white_list_entries) {
            ns_list_foreach_safe(device_conf_entry_t, entry, g_config.white_list_entries)
            {
                if (entry->partial_match) {
                    if (strstr(name, entry->name)) {
                        ret = BLE_DEVICE_PERSISTENT_GATT_SERVER;
                        goto out;
                    }
                } else {
                    // Full match needed
                    if (0 == strcmp(entry->name, name)) {
                        ret = BLE_DEVICE_PERSISTENT_GATT_SERVER;
                        goto out;
                    }
                }
            }
        }
    }
/* TODO / FIXME: whitelist needs to support configuring experimental advertisement only device */
#if 1 == EXPERIMENTAL_ADVERTISEMENT_SUPPORT_ENABLED
    // Experimental advertisement only device support
    address_prop = g_dbus_proxy_get_cached_property(device_proxy, "Address");
    if (address_prop && g_variant_type_equal(g_variant_get_type(address_prop), G_VARIANT_TYPE_STRING)) {
        const char *address = g_variant_get_string(address_prop, NULL);
        tr_debug("Comparing address %s", address);
        if (strstr(address, "AC:23:3F:23:C7:E3")) {
            ret = BLE_DEVICE_GAP_ADVERTISEMENT_ONLY;
            goto out;
        }
    }
#endif

out:
    if (name_prop) {
        g_variant_unref(name_prop);
    }
    if (address_prop) {
        g_variant_unref(address_prop);
    }

    return ret;
}

/**
 * \brief check if device should be connected to
 *
 * This function checks if device should be connected to
 *
 * \params a object to check
 *
 */
static ble_device_type ble_identify_device_type(const char *path)
{
    GDBusProxy *devProxy;
    ble_device_type device_type = BLE_DEVICE_UNKNOWN;
    GError *err = NULL;

    assert(g_config.connection != NULL);

    tr_info("--> ble_identify_device_type path: %s", path);

    devProxy = g_dbus_proxy_new_sync(g_config.connection,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     NULL,
                                     BLUEZ_NAME,
                                     path,
                                     DEVICE_IFACE,
                                     NULL,
                                     &err);

    if (!G_IS_DBUS_PROXY(devProxy)) {
        tr_err("    Device %s, not available: %s", path, err->message);
        g_clear_error(&err);
        return BLE_DEVICE_UNKNOWN;
    }

    if (g_config.service_based_discovery) {
        if (ble_identify_device_services(devProxy)) {
            tr_info("    identified supported services");
            device_type = BLE_DEVICE_PERSISTENT_GATT_SERVER;
            goto out;
        }
    }
    device_type = ble_identify_device_using_whitelist(devProxy);
    if (device_type == BLE_DEVICE_UNKNOWN) {
        tr_info("    device not identified");
    } else {
        tr_info("    identified custom device (type %d)", device_type);
    }
out:
    g_object_unref(devProxy);
    tr_info("<-- ble_identify_device_type");
    return device_type;
}


static void get_device_address_from_characteristic_path(const char *path, char *char_device_address)
{
    /* dbus paths for characteristics are formatted
       /org/bluez/hciX/dev_XX_XX_XX_XX_XX_XX/serviceXXXX/charXXXX
       Here we exploit that format to pull the device address directly form the path
     */
    char *dev = strstr(path, "dev_");
    if (NULL == dev) {
        tr_err("Malformed characteristic path - %s\r\n", path);
    } else {
        strncpy(char_device_address, dev + 4, 17);
        char_device_address[2] =
            char_device_address[5] =
            char_device_address[8] =
            char_device_address[11] =
            char_device_address[14] = ':';
        char_device_address[17] = 0;
    }
}

static int translate_ble_flags(gchar *flags_str)
{
    int flags = 0;

    if (strstr(flags_str, "read") != NULL) {
        flags |= BLE_GATT_PROP_PERM_READ;
    }
    if (strstr(flags_str, "write") != NULL) {
        flags |= BLE_GATT_PROP_PERM_WRITE;
    }
    if (strstr(flags_str, "notify") != NULL) {
        flags |= BLE_GATT_PROP_PERM_NOTIFY;
    }
    /*TODO: BLE exposes other flags besides read/write (e.g. 'indicate')
      Need to map those properties to LWM2M properties*/

    return flags;
}

static int object_on_adapter(const char *objpath, const char *adapter)
{
    char adapter_path[32];
    snprintf(adapter_path, sizeof(adapter_path), "/org/bluez/%s/", adapter);
    return (0 == strncmp(objpath, adapter_path, strlen(adapter_path)));
}

#if 1 == EXPERIMENTAL_NOTIFY_CHARACTERISTIC_SUPPORT
static void ble_characteristic_properties_changed(GDBusProxy *proxy,
                                                  GVariant *changed_properties,
                                                  GStrv invalidated_properties,
                                                  gpointer user_data)
{
    tr_debug("Characteristic properties changed proxy: %p", proxy);
    tr_debug("Characteristic: %s", g_dbus_proxy_get_object_path(proxy));
    // TODO: Update translated resource values for this characteristic
}

static void ble_start_notify_done(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GError *err = NULL;
    GDBusProxy *proxy = (GDBusProxy *)source_object;
    GVariant *ret;

    assert(user_data == NULL);
    assert(proxy != NULL);
    assert(G_IS_DBUS_PROXY(proxy));
    (void)user_data;
    tr_debug("--> ble_start_notify_done proxy: %p", proxy);

    ret = g_dbus_proxy_call_finish(proxy, res, &err);
    if (err == NULL) {
        gchar *ret_string = g_variant_print(ret, TRUE);
        if (ret_string != NULL) {
            tr_debug("    ble_start_notify_done: StartNotify returned %s", ret_string);
            g_free(ret_string);
        }
        if (ret != NULL) {
            g_variant_unref(ret);
        }
    } else {
        assert(ret == NULL);
    }
    tr_debug("<-- ble_start_notify_done");
}

void ble_characteristic_stop_notify_proxy(const struct ble_gatt_char *ch)
{
    assert(ch != NULL);
    assert(G_IS_DBUS_PROXY(ch->proxy));

    // Configure properties changed signal for getting value notifications from notify characteristics
    if (ch->properties & BLE_GATT_PROP_PERM_NOTIFY) {
        tr_debug("Start notify proxy for path %s", ch->dbus_path);

        GError *err = NULL;
        // Start the notify operations
        g_dbus_proxy_call_sync(ch->proxy,
                               "StopNotify",
                               NULL,
                               G_DBUS_CALL_FLAGS_NONE,
                               -1,
                               NULL,
                               &err);
        g_object_unref(ch->proxy);
    }
}

static void ble_characteristic_start_notify_proxy(const struct ble_gatt_char *ch)
{
    assert(ch != NULL);
    assert(G_IS_DBUS_PROXY(ch->proxy));

    // Configure properties changed signal for getting value notifications from notify characteristics
    if (ch->properties & BLE_GATT_PROP_PERM_NOTIFY) {
        tr_debug("Start notify proxy for dbus_path: %s", ch->dbus_path);

        (void)g_signal_connect(ch->proxy,
                               "g-properties-changed",
                               G_CALLBACK(ble_characteristic_properties_changed),
                               NULL);

        // Start the notify operations
        g_dbus_proxy_call(ch->proxy,
                          "StartNotify",
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          ble_start_notify_done,
                          NULL);
    }
}
#endif // EXPERIMENTAL_NOTIFY_CHARACTERISTIC_SUPPORT

static void process_characteristic_object(GDBusObject *obj, struct ble_device *ble_dev, const char *adapter)
{

    GError *err = NULL;
    const gchar *path = NULL;
    gchar service_path[4096];
    GDBusProxy *proxy;
    GDBusProxy *service_proxy = NULL;
    char *quoted;

    int char_flags = 0;
    char char_uuid[FORMATTED_UUID_LEN + 1];
    char srvc_uuid[FORMATTED_UUID_LEN + 1];
    bool has_uuid = false;

    path = g_dbus_object_get_object_path(obj);
    if ((NULL != path) && object_on_adapter(path, adapter)) {

        assert(g_config.connection != NULL);
        proxy = g_dbus_proxy_new_sync(g_config.connection,
                                              G_DBUS_CALL_FLAGS_NONE,
                                              NULL,
                                              BLUEZ_NAME,
                                              path,
                                              GATT_CHARACTERISTIC_IFACE,
                                              NULL,
                                              &err);
        if (!G_IS_DBUS_PROXY(proxy)) {
            tr_err("Characteristic at dbus path %s, not available: %s", path, err->message);
            g_clear_error(&err);
        } else {
            gchar **property_names;
            guint n;
            const gchar *key;
            GVariant *value;
            gchar *value_str;
            char char_device_address[BLE_DEVICE_NAME_MAX_LENGTH];

            get_device_address_from_characteristic_path(path, char_device_address);
            if (0 == strcmp(char_device_address, ble_dev->attrs.addr)) {
                tr_info("adding characteristic at path [%s] to ble device [%s]",
                        path, ble_dev->attrs.addr);
                property_names = g_dbus_proxy_get_cached_property_names(proxy);
                for (n = 0; property_names != NULL && property_names[n] != NULL; n++) {
                    key = property_names[n];
                    value = g_dbus_proxy_get_cached_property(proxy, key);
                    value_str = g_variant_print(value, TRUE);
                    if (0 == strcmp(key, "Flags")) {
                        char_flags = translate_ble_flags(value_str);
                    } else if (0 == strcmp(key, "UUID")) {
                        quoted = strtok(value_str, "'");
                        strncpy(char_uuid, quoted, sizeof char_uuid);
                        char_uuid[FORMATTED_UUID_LEN] = 0;
                        has_uuid = true;
                    } else if (0 == strcmp(key, "Service")) {
                        quoted = strtok(value_str, "'");
                        quoted = strtok(NULL, "'");
                        strncpy(service_path, quoted, sizeof service_path);
                        service_path[4095] = 0;

                    } else {
                        tr_debug("property of ble characteristic [%s -> %s] will not be part of LWM2M resource",
                                 key, value_str);
                    }

                    g_variant_unref(value);
                    g_free(value_str);
                }
                g_strfreev(property_names);


                /* In the event we cannot extract the service uuid, we will store this characteristic
                   locally under a default service with uuid ffffffff-ffff-ffff-ffff-ffffffffffff*/
                strncpy(srvc_uuid, "ffffffff-ffff-ffff-ffff-ffffffffffff", sizeof srvc_uuid);
                assert(g_config.connection != NULL);
                service_proxy = g_dbus_proxy_new_sync(g_config.connection,
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              NULL,
                                                              BLUEZ_NAME,
                                                              service_path,
                                                              GATT_SERVICE_IFACE,
                                                              NULL,
                                                              &err);
                if (!G_IS_DBUS_PROXY(service_proxy)) {
                    tr_err("Unable to create proxy for serice %s: %s", service_path, err->message);
                    g_clear_error(&err);
                } else {
                    property_names = g_dbus_proxy_get_cached_property_names(service_proxy);
                    for (n = 0; property_names != NULL && property_names[n] != NULL; n++) {
                        key = property_names[n];
                        value = g_dbus_proxy_get_cached_property(service_proxy, key);
                        value_str = g_variant_print(value, TRUE);
                        if (0 == strcmp(key, "UUID")) {
                            quoted = strtok(value_str, "'");
                            strncpy(srvc_uuid, quoted, sizeof srvc_uuid);
                            srvc_uuid[FORMATTED_UUID_LEN] = 0;
                        } else {
                            tr_debug("service property  %s -> %s", key, value_str);
                        }
                        g_variant_unref(value);
                        g_free(value_str);
                    }
                    g_strfreev(property_names);
                    g_object_unref(service_proxy);
                }
            } /* else {
                tr_debug("characteristic [%s] belongs to device [%s] - not device [%s]",
                        path, char_device_address, ble_dev->attrs.addr);
            } */
        }

        if (has_uuid) {
            struct ble_gatt_char *ch;

            ch = device_add_gatt_characteristic(ble_dev,
                                                srvc_uuid,
                                                service_path,
                                                char_uuid,
                                                path,
                                                char_flags,
                                                proxy);

            if (ch == NULL) {
                tr_error("Failed to add gatt characteristic for %s, out of memory?", path);
            }
#if 1 == EXPERIMENTAL_NOTIFY_CHARACTERISTIC_SUPPORT
            else {
                ble_characteristic_start_notify_proxy(ch);
            }
#endif // EXPERIMENTAL_NOTIFY_CHARACTERISTIC_SUPPORT
        }
    }
}

static bool ble_is_characteristic(GDBusObject *maybe_characteristic)
{
    GDBusInterface *interface;
    bool ret = false;

    interface = g_dbus_object_get_interface(
            (GDBusObject *)maybe_characteristic,
            GATT_CHARACTERISTIC_IFACE);
    ret = (interface != NULL);
    if (interface != NULL) {
        g_object_unref(interface);
    }

    return ret;
}

static void ble_handle_characteristic(gpointer data, gpointer user_data)
{
    GDBusObject *object = data;
    struct ble_device *ble_dev = user_data;

    assert(object != NULL);
    assert(ble_dev != NULL);
    assert(G_IS_OBJECT(object));

    // TODO: Filter devices based on service/char UUID.
    if (ble_is_characteristic(object)) {
        process_characteristic_object(
                object,
                ble_dev,
                g_config.adapter);
    }
}

static void ble_discover_characteristics(struct ble_device *ble_dev)
{
    GError *err;
    GList *objects = NULL;

    tr_debug("--> BLE discover characteristics device_id: '%s'", ble_dev->device_id);
    err = NULL;
    assert(g_config.connection != NULL);
    GDBusObjectManager *bluez_manager = g_dbus_object_manager_client_new_sync(
                g_config.connection,
                G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                BLUEZ_NAME,
                "/",
                NULL,
                NULL,
                NULL,
                NULL,
                &err);
    if (bluez_manager == NULL) {
        tr_error("Couldn't get object manager to discover characteristics!");
        tr_error("%d %s", err->code, err->message);
        goto out;
    }

    objects = g_dbus_object_manager_get_objects(bluez_manager);
    if (NULL == objects) {
        tr_err("Manager did not give us objects!");
        tr_error("%d %s", err->code, err->message);
        goto out;
    }

    g_list_foreach(objects, ble_handle_characteristic, ble_dev);

out:
    if (err != NULL) {
        g_clear_error(&err);
    }
    if (objects != NULL) {
        g_list_free_full(objects, g_object_unref);
    }
    if (bluez_manager != NULL) {
        g_object_unref(bluez_manager);
    }

    tr_debug("<-- ble_discover_characteristics");
}

static void ble_new_device(const char *dbus_path)
{
    tr_info("Discovered device dbus_path: '%s'\n", dbus_path);

    if (global_keep_running) {
        ble_device_type device_type;
        device_type = ble_identify_device_type(dbus_path);

        if (device_type != BLE_DEVICE_UNKNOWN) {
            // Create proxy for all device types that are known
            GDBusProxy *proxy = ble_create_device_proxy(dbus_path);
            ble_create_device_context(proxy, device_type);
            if (device_type == BLE_DEVICE_PERSISTENT_GATT_SERVER) {
                tr_info("    device type is persistent GATT server");
                ble_proxy_connect(proxy);
            }
#if 1 == EXPERIMENTAL_ADVERTISEMENT_SUPPORT_ENABLED
            else if (device_type == BLE_DEVICE_GAP_ADVERTISEMENT_ONLY) {
                tr_info("    device type is GAP advertisement only");
            }
#endif
        }
    } else {
        tr_debug("   ignoring new device, because shutdown is in progress.");
    }
}

// Helper function for g_list_foreach
static void ble_handle_known_device(gpointer data, gpointer user_data)
{
    GDBusObject *object = data;
    assert(user_data == NULL);
    assert(object != NULL);
    assert(G_IS_OBJECT(object));

    (void)user_data;
    if (ble_is_device(object)) {
        const char *path = g_dbus_object_get_object_path(object);
        if (object_on_adapter(path, g_config.adapter)) {
            ble_new_device(path);
        } else {
            tr_debug("Ignoring %s due to not being on adapter.", path);
        }
    }
}


/**
 * \brief Try to connect to devices that BlueZ already knows about.
 */
static int ble_connect_to_known_devices(GDBusObjectManager *bluez_manager)
{
    GError *err;
    GList *objects;

    tr_debug("--> ble_connect_to_known_devices");

    err = NULL;

    objects = g_dbus_object_manager_get_objects(bluez_manager);
    if (NULL == objects) {
        tr_err("Manager did not give us objects!");
        goto out;
    }

    tr_debug("    Calling connect on each known device.");
    g_list_foreach(objects, ble_handle_known_device, NULL);

out:
    if (objects != NULL) {
        g_list_free_full(objects, g_object_unref);
    }
    if (err != NULL) {
        g_clear_error(&err);
    }
    tr_debug("<-- ble_connect_to_known_devices");
    return 0;
}

// BlueZ finished starting discovery.  This function is only for displaying this info.
void ble_startdiscovery_done(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *err = NULL;
    GDBusProxy *proxy = (GDBusProxy *)source_object;
    GVariant *ret;

    assert(user_data == NULL);
    assert(proxy != NULL);
    assert(G_IS_DBUS_PROXY(proxy));
    (void)user_data;
    tr_debug("--> ble_startdiscovery_done");

    ret = g_dbus_proxy_call_finish(proxy, res, &err);
    if (err == NULL) {
        gchar *ret_string = g_variant_print(ret, TRUE);
        if (ret_string != NULL) {
            tr_debug("    ble_start_discovery_done: StartDiscovery returned %s", ret_string);
            g_free(ret_string);
        }
        if (ret != NULL) {
            g_variant_unref(ret);
        }
    } else {
        tr_err("    ble_start_discovery_done: StartDiscovery failed!");
        assert(ret == NULL);
    }
    tr_debug("<-- ble_startdiscovery_done");
}

/**
 * \brief scan for BLE devices
 *
 * This function scanning for BLE devices, calls Bluez Adapter interface
 * functions
 * Have to call SetDiscoveryFilter before Discovering
 *
 * \param bluez_manager, pointer to GDBusObjectManager
 *
 */
static int ble_discover(GDBusObjectManager *bluez_manager)
{
    // FIXME: StopDiscovery is never called. Calling it could free memory leaks!
    int ret = 0;
    GError *err = NULL;
    GVariant *proxy_call;

    tr_debug("--> ble_discover");

    GDBusProxy *proxy = (GDBusProxy *)g_dbus_object_manager_get_interface(
                            bluez_manager,
                            g_config.bluez_hci_path,
                            ADAPTER_IFACE);
    if (!G_IS_DBUS_PROXY(proxy)) {
        tr_err("Error: Get device proxy ADAPTER_IFACE failed");
        ret = 1;
        goto out;
    }


    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

    // If service based discovery is enabled, we discover devices based
    // on advertised services. So build the service filter and add it to the
    // discovery filter.
    if (g_config.service_based_discovery) {
        GVariant *services_filter = ble_services_get_service_uuid_filter();
        g_variant_builder_add(&builder,
                              "{sv}",
                              "UUIDs",
                              services_filter);
    }

    g_variant_builder_add(&builder,
                          "{sv}",
                          "Transport",
                          g_variant_new_string("le"));
    GVariant *const filter = g_variant_builder_end(&builder);

    /* pack the filter params into a tuple */
    GVariant *const filter_args = g_variant_new("(@a{sv})", filter);

    tr_info("    ble_discover: SetDiscoveryFilter");
    proxy_call = g_dbus_proxy_call_sync(proxy,
                                        "SetDiscoveryFilter",
                                        filter_args,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &err);
    if (NULL == proxy_call) {
        tr_err("Failed to set discovery filter: %s, %d",
               err->message, err->code);
        ret = err->code;
        goto out;
    }
    tr_debug("    ble_discover: returned from SetDiscoveryFilter");
    g_variant_unref(proxy_call);

    tr_debug("    ble_discover: StartDiscovery");
    g_dbus_proxy_call(proxy,
                      "StartDiscovery",
                      NULL,
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      ble_startdiscovery_done,
                      NULL);
out:
    if (err != NULL) {
        g_clear_error(&err);
    }
    if (proxy != NULL) {
        g_object_unref(proxy);
    }
    tr_debug("<-- ble_discover");
    return ret;
}

static void ble_on_object_added(GDBusObjectManager *manager, GDBusObject *object, gpointer user_data)
{
    (void)user_data;
    (void)manager;
    if (ble_is_device(object)) {
        const char *path = g_dbus_object_get_object_path(object);
        tr_info("ble_on_object_added path: '%s'", path);
        ble_new_device(path);
    }
}

static void ble_on_object_removed(GDBusObjectManager *manager, GDBusObject *object, gpointer user_data)
{
    gchar *owner;
    (void)user_data;
    (void)object;
    owner = g_dbus_object_manager_client_get_name_owner(G_DBUS_OBJECT_MANAGER_CLIENT(manager));
    const gchar *object_path = g_dbus_object_get_object_path(object);
    tr_info("Removed object at %s (owner %s)", object_path, owner);
    struct ble_device *ble_dev = NULL;

    devices_mutex_lock();
    // Check if the Bluez hci interface was removed.
    // This happens for example, if the bluetooth daemon crashes.
    if (!strcmp(g_config.bluez_hci_path, object_path)) {
        tr_debug("Restarting g_main loop");
        g_main_loop_quit(g_config.g_loop);
    } else {
        ble_dev = devices_find_device_by_dbus_path(object_path);
        if (ble_dev == NULL) {
            tr_debug("    Can't find device for %s", object_path);
        } else {
            bool call_succeeded = edge_unregister_device(ble_dev, true /* remove_context */);
            if (!call_succeeded) {
                pt_edge_del_device(ble_dev);
            }
        }
    }
    devices_mutex_unlock();
    g_free(owner);
}

// Read all characteristics for a connected device.
void ble_read_all_characteristics_for_device(struct ble_device *ble)
{
    int srvc, ch;
    tr_debug("---> ble_read_all_characteristics_for_device device_id: '%s'", ble->device_id);
    if (!ble_device_is_connected(ble->proxy)) {
        tr_debug("    Trying to read a disconnected device.");
        goto exit_label;
    }

    for (srvc = 0; srvc < ble->attrs.services_count; srvc++) {
        struct ble_gatt_service *gattservice = &(ble->attrs.services[srvc]);
        for (ch = 0; ch < gattservice->chars_count; ch++) {
            struct ble_gatt_char *gattchar = &(gattservice->chars[ch]);
            if ((gattchar->properties & BLE_GATT_PROP_PERM_READ) == 0) {
                tr_debug("    Skipping characteristic %s", gattchar->dbus_path);
            }
            else {
                ble_read_characteristic_async(gattchar->dbus_path, ble->device_id, srvc, ch);
            }
        }
    }
exit_label:
    tr_debug("<--- ble_read_all_characteristics_for_device device_id: '%s'", ble->device_id);
}

static gboolean ble_read_everything(gpointer data)
{
    (void)data;

    if (!global_keep_running) {
        tr_debug("Main thread is shutting down, return without doing anything");
        return G_SOURCE_REMOVE;
    }

    tr_debug("Reading all the things.");

    //lock the list before the loop
    devices_mutex_lock();

    ns_list_foreach_safe(struct ble_device, ble, devices_get_list()) {
        if (device_is_registered(ble) && device_is_connected(ble)) {
            // TODO: handle lwm2m subscription -> GATT notify
            ble_read_all_characteristics_for_device(ble);
            device_write_values_to_pt(ble);
        } else {
            tr_info("device '%s' is registered: %d and connected: %d",
                    ble->attrs.addr,
                    device_is_registered(ble),
                    device_is_connected(ble));
        }
    }

    //unlock the list now that we are done
    devices_mutex_unlock();

    return G_SOURCE_CONTINUE;
}

static void ble_clear_device_cache(GDBusObjectManager *object_manager)
{
    GDBusProxy *proxy = NULL;
    GList *objects = NULL;
    GError *err = NULL;
    GList *iterator = NULL;
    GDBusObject *device = NULL;
    GVariant *result = NULL;
    const char *device_path = NULL;

    tr_debug("--> ble_clear_device_cache");

    assert(g_config.connection != NULL);
    assert(object_manager != NULL);

    proxy = g_dbus_proxy_new_sync(g_config.connection,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  BLUEZ_NAME,
                                  g_config.bluez_hci_path,
                                  ADAPTER_IFACE,
                                  NULL,
                                  &err);
    if (!G_IS_DBUS_PROXY(proxy)) {
        tr_err("Adapter %s interface not available on dbus: %s", ADAPTER_IFACE, err->message);
        g_clear_error(&err);
        goto out;
    }

    objects = g_dbus_object_manager_get_objects(object_manager);

    if (!objects) {
        g_object_unref(proxy);
        goto out;
    }

    for (iterator = objects; iterator != NULL; iterator = iterator->next) {
        device = iterator->data;
        device_path = g_dbus_object_get_object_path(device);
        if (device_path && ble_is_device(device)) {
            tr_info("Removing device at %s", device_path);
            result = g_dbus_proxy_call_sync(proxy,
                                            "RemoveDevice",
                                            g_variant_new("(o)", device_path),
                                            G_DBUS_CALL_FLAGS_NONE,
                                            -1,
                                            NULL,
                                            &err);
            if (result) {
                g_variant_unref(result);
            }
            else {
                tr_err("Failed to remove device with error: %s (%d)", err->message, err->code);
                g_clear_error(&err);
            }
        }
    }

    g_list_free_full(objects, g_object_unref);
    g_object_unref(proxy);
out:
    tr_debug("<-- ble_clear_device_cache");

}

gboolean pt_ble_pt_ready(gpointer data)
{
    (void)data;
    tr_debug("--> pt_ble_pt_ready");

    devices_mutex_lock();
    // Create and register all devices that are already known about
    ns_list_foreach_safe(struct ble_device, ble, devices_get_list()) {
        if (!edge_device_exists(ble->device_id)) {
            /* create the Edge PT device context */
            if (!devices_create_pt_device(ble->device_id,          /* device ID */
                                          "ARM",                   /* manufacturer */
                                          "mept-ble",              /* model number */
                                          ble->attrs.addr,         /* serial number */
                                          "mept-ble")) {           /* device type */
                tr_err("Failed to create pt device context");
            }
            else {
                // Device created, resolve services if they are already resolved
                if (edge_is_connected() && ble_services_are_resolved(ble->proxy)) {
                    tr_debug("    Device services have already resolved, processing now.");
                    ble_on_services_resolved(ble->proxy);
                }
            }
        }
    }
    devices_mutex_unlock();

    tr_debug("<-- pt_ble_pt_ready");

    return FALSE;
}

gboolean pt_ble_g_main_quit_loop(gpointer data)
{
    (void)data;
    tr_debug("--> pt_ble_g_main_quit_loop");
    assert(g_config.g_loop != NULL);

    if (!global_keep_running) {
        tr_debug("Quitting g_main loop");
        g_main_loop_quit(g_config.g_loop);
    }

    tr_debug("<-- pt_ble_g_main_quit_loop");

    return FALSE;
}

gboolean pt_ble_graceful_shutdown(gpointer data)
{
    (void)data;
    tr_debug("--> pt_ble_graceful_shutdown");

    // global_keep_running can be reset by pt-client thread which indicates that socket connection
    // with edge core is shutting down.
    if(!global_keep_running) {
        g_idle_add(pt_ble_g_main_quit_loop, NULL);
    } else {
        global_keep_running = 0;
    }

    // Inform protocol translator that we are now shutting down and all devices should be
    // unregistered.
    devices_mutex_lock();

    tr_info("unregister_devices");
    unregister_devices();

    devices_mutex_unlock();

    tr_debug("<-- pt_ble_graceful_shutdown");

    return G_SOURCE_REMOVE;
}

static int ble_connect_to_dbus(const char *address)
{
    int ret = 0;
    GError *err = NULL;

    if (address == NULL) {
        tr_debug("    connecting to system bus");
        g_config.connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    } else {
        tr_debug("    connecting to %s", address);
        g_config.connection = g_dbus_connection_new_for_address_sync(address,
                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION |
                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                NULL,
                NULL,
                &err);
    }

    if (g_config.connection == NULL) {
        assert(err != NULL);
        tr_err("Error: couldn't establish dbus connection! %d: %s",
                err->code,
                err->message);
        ret = 1;
        goto out;
    } else {
        assert(err == NULL);
        tr_info("Connected to D-Bus at %s",
                g_dbus_connection_get_unique_name(g_config.connection));
    }

out:
    if (err != NULL) {
        g_clear_error(&err);
    }
    return ret;
}

gboolean ble_adapter_is_powered()
{
    gboolean powered = false;
    GVariant *prop = ble_get_property(g_config.bluez_hci_path, ADAPTER_IFACE, POWERED_PROPERTY);
    if (prop && g_variant_is_of_type(prop, G_VARIANT_TYPE_BOOLEAN)) {
        powered = g_variant_get_boolean(prop);
        g_variant_unref(prop);
    }
    return powered;
}

gboolean ble_adapter_set_powered(gboolean powered)
{
    bool ret = false;
    GVariant *value = g_variant_new("b", powered);
    if (ble_set_property(g_config.bluez_hci_path, ADAPTER_IFACE, POWERED_PROPERTY, value)) {
        ret = true;
    }
    return ret;
}

device_conf_list_t *device_conf_list_read(const char *file_path)
{
    device_conf_list_t *devices_list = NULL;
    json_t *json = NULL;
    uint8_t *data = NULL;
    if (file_path) {
        size_t bytes_read = 0;

        if (0 == edge_read_file(file_path, &data, &bytes_read)) {
            json_error_t error;
            json_t *json = json_loads((const char *) data, 0, &error);
            if (json != NULL) {
                json_t *devices = json_object_get(json, "whitelisted-devices");
                if (NULL == devices) {
                    tr_err("Cannot find 'whitelisted-devices' in '%s'", file_path);
                    goto exit_label;
                }
                size_t index;
                json_t *entry;
                devices_list = calloc(1, sizeof(device_conf_list_t));
                ns_list_init(devices_list);

                json_array_foreach(devices, index, entry)
                {
                    json_t *json_name = json_object_get(entry, "name");
                    if (!json_name) {
                        tr_err("Cannot find name-value pair for 'name' in device entry");
                        goto error_exit;
                    }
                    if (!json_is_string(json_name)) {
                        tr_err("Value for key 'name' is not a string");
                        goto error_exit;
                    }
                    bool partial = true;
                    json_t *partial_json = json_object_get(entry, "partial-match");
                    if (partial_json) {
                        if (json_is_integer(partial_json)) {
                            partial = (bool) json_integer_value(partial_json);
                        } else {
                            tr_err("Value for 'partial-match' is not integer");
                            goto error_exit;
                        }
                    }
                    device_conf_entry_t *device_conf_entry = calloc(1, sizeof(device_conf_entry_t));
                    device_conf_entry->partial_match = partial;
                    device_conf_entry->name = strdup(json_string_value(json_name));
                    ns_list_add_to_end(devices_list, device_conf_entry);
                }
            } else {
                tr_err("Jansson cannot parse '%s' error: '%s' on line: %d", file_path, error.text, error.line);
            }
        } else {
            tr_err("Cannot read the file '%s'", file_path);
        }
    }
    goto exit_label;
error_exit:
    device_conf_list_free(devices_list);
    devices_list = NULL;

exit_label:
    json_decref(json);
    free(data);
    return devices_list;
}

static void device_conf_list_free(device_conf_list_t *list)
{
    if (list) {
        ns_list_foreach_safe(struct device_conf_entry, dev, list)
        {
            free(dev->name);
            ns_list_remove(list, dev);
        }
        free(list);
    }
}

/**
 * \brief updates connected BLE devices in loop
 *
 * This function goes in a loop throw registered BLE devices.
 * Read all requiered characteristic and updates devices.
 * \return 0 if there's no errors. Otherwise return non-zero.
 */
int ble_start(const char *postfix,
              const char *adapter,
              const char *address,
              int clear_device_cache,
              const char *extended_discovery_file_path,
              int service_based_discovery)
{
    int ret_val = 0;
    if (extended_discovery_file_path) {
        g_config.white_list_entries = device_conf_list_read(extended_discovery_file_path);
        if (NULL == g_config.white_list_entries) {
            tr_err("Couldn't read whitelist even though the extended discovery file is specified!");
            ret_val = 1;
            goto out;
        }
    }
    bool retry;
    do {
        tr_debug("--> ble_start extended_discovery_file_path: %s", extended_discovery_file_path);
        GError *err = NULL;
        GDBusObjectManager *bluez_manager = NULL;
        gulong object_added_signal, object_removed_signal;

        g_config.postfix = postfix;
        g_config.adapter = adapter;
        g_config.service_based_discovery = service_based_discovery;
        snprintf(g_config.bluez_hci_path, sizeof g_config.bluez_hci_path, "/org/bluez/%s", adapter);

        if (ble_connect_to_dbus(address)) {
            // Error message already printed.
            ret_val = 2;
            goto out;
        }

        g_config.g_loop = g_main_loop_new(NULL, FALSE);
        if (g_config.g_loop == NULL) {
            tr_err("Error: couldn't allocate main loop");
            ret_val = 3;
            goto out;
        }

        tr_info("creating GDBus Bluez interface");
        err = NULL;
        assert(g_config.connection != NULL);
        bluez_manager = g_dbus_object_manager_client_new_sync(g_config.connection,
                                                              G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                              BLUEZ_NAME,
                                                              "/",
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              NULL,
                                                              &err);
        if (NULL == bluez_manager) {
            assert(err != NULL);
            tr_err("Error: Is Bluez running? %d %s", err->code, err->message);
            g_clear_error(&err);
            goto out;
        }
        tr_info("created GDBus Bluez interface");

        if (!ble_adapter_is_powered()) {
            tr_info("powering on BlueZ adapter");
            if (ble_adapter_set_powered(true)) {
                tr_info("BlueZ adapter powered on");
            } else {
                tr_error("could not power on adapter!");
                goto out;
            }
        }

        /* Before we discover, we must check if there are existing devices. */
        if (clear_device_cache) {
            ble_clear_device_cache(bluez_manager);
        }
        else {
            ble_connect_to_known_devices(bluez_manager);
        }

        object_added_signal = g_signal_connect(bluez_manager, "object-added", G_CALLBACK(ble_on_object_added), NULL);
        object_removed_signal = g_signal_connect(bluez_manager,
                                                 "object-removed",
                                                 G_CALLBACK(ble_on_object_removed),
                                                 NULL);

        if (ble_discover(bluez_manager) != 0) {
            tr_err("ble_discover Error: Is Bluez running?");
            goto out;
        }

        // Schedule periodic reads of all devices.
        g_config.g_source_id_1 = g_timeout_add_full(G_PRIORITY_HIGH,
                                                    BLE_VALUE_READ_INTERVAL,
                                                    ble_read_everything,
                                                    NULL,
                                                    NULL);

        g_main_loop_run(g_config.g_loop);

        g_source_remove(g_config.g_source_id_1);

        g_signal_handler_disconnect(bluez_manager, object_added_signal);
        g_signal_handler_disconnect(bluez_manager, object_removed_signal);
    out:
        if (g_config.g_loop != NULL) {
            g_main_loop_unref(g_config.g_loop);
        }
        if (bluez_manager != NULL) {
            g_object_unref(bluez_manager);
        }
        if (g_config.connection != NULL) {
            g_object_unref(g_config.connection);
        }
        retry = (ret_val == 0 && global_keep_running);
        if (retry) {
            tr_info("Retry connecting to bluez in %d seconds...", BLUEZ_RECONNECT_RETRY_TIME_SECONDS);
            sleep(BLUEZ_RECONNECT_RETRY_TIME_SECONDS);
        }
    } while (retry);
    device_conf_list_free(g_config.white_list_entries);
    g_config.white_list_entries = NULL;
    tr_debug("<-- ble_start");
    return ret_val;
}

/**
 *  /brief Parses the dbus read result variant to a data-array
 */

static void parse_result_variant(GVariant *ret, uint8_t *data, size_t *size) {
    GVariantIter *iter;
    size_t maxsize = *size;
    size_t datasize = 0;
    g_variant_get(ret, "(ay)", &iter);
    while ((datasize < maxsize) && g_variant_iter_loop(iter, "y", &data[datasize++]));
    *size = datasize;
    g_variant_iter_free (iter);
}

/** Reads value from the ble characteristic specified by the
 *  dbus path.
 *
 *  characteristic_path
 *       input: the dbus path for the characteristic
 *  data
 *       data buffer provided for return data
 *  *size
 *       input: the maximum size of the buffer
 *       output: actual size of written data (<= input)
 *
 */

int ble_read_characteristic(const char *characteristic_path,
                            uint8_t    *data,
                            size_t     *size)
{
    GVariantBuilder build_opt;
    GVariant *ret;
    GDBusProxy *charProxy;
    GError *err = NULL;
    int rc = 0;

    if (NULL == characteristic_path) {
        tr_err("Null dbus path in ble_read_characteristic");
        return -1;
    }
    if ((NULL == data) || (0 == *size)) {
        tr_err("Invalid buffer %p size %zu for ble_read_characteristic", data, *size);
    }

    assert(g_config.connection != NULL);
    charProxy = g_dbus_proxy_new_sync(g_config.connection,
                                              G_DBUS_PROXY_FLAGS_NONE,
                                              NULL,
                                              BLUEZ_NAME,
                                              characteristic_path,
                                              GATT_CHARACTERISTIC_IFACE,
                                              NULL,
                                              &err);
    if (!G_IS_DBUS_PROXY(charProxy)) {
        tr_err("Get characteristic proxy failed: %s (%d)", err->message, err->code);
        rc = err->code;
        g_clear_error(&err);
        return rc;
    }

    g_variant_builder_init(&build_opt, G_VARIANT_TYPE("a{sv}"));

    ret = g_dbus_proxy_call_sync(charProxy,
                                 "ReadValue",
                                 g_variant_new("(a{sv})", &build_opt),
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 &err);
    if (ret == NULL) {
        tr_err("Failed to read %s: %s (%d)", characteristic_path, err->message, err->code);
        rc = err->code;
        g_clear_error(&err);
    } else {
        parse_result_variant(ret, data, size);
        g_variant_unref(ret);
    }

    g_object_unref(charProxy);

    return rc;
}

/**
 * /brief Callback called by the asynchronous dbus read operation
 */
void ble_read_characteristic_callback(GObject *source_object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
    struct async_read_userdata *read_userdata = (struct async_read_userdata*) user_data;

    GError *error = NULL;
    GVariant *ret = g_dbus_proxy_call_finish(G_DBUS_PROXY(source_object), res, &error);
    int ch = read_userdata->ch;
    int srvc = read_userdata->srvc;

    if (ret != NULL) {
        devices_mutex_lock();
        struct ble_device *ble = devices_find_device_by_device_id(read_userdata->device_id);
        device_mutex_lock(ble);
        struct ble_gatt_service *gattservice = &(ble->attrs.services[srvc]);
        struct ble_gatt_char *gattchar = &(gattservice->chars[ch]);

        size_t size = gattchar->value_size;
        uint8_t *data = gattchar->value;

        parse_result_variant(ret, data, &size);
        gattchar->value_length = size;
        g_variant_unref(ret);

        if (ble_services_is_supported_characteristic(gattservice->uuid, gattchar->uuid)) {
            ble_services_decode_and_write_characteristic_translation(ble, srvc, ch, gattchar->value, size);
        }

        // TODO: endian conversions for other size integers and floats
        if (gattchar->dtype == BLE_INTEGER) {
            switch (gattchar->value_size) {
            case 2:
            {
                uint16_t *u16 = (uint16_t *)gattchar->value;
                uint16_t host = *u16;
                *u16 = htons(host);
            }
            break;
            case 4:
            {
                uint32_t *u32 = (uint32_t *)gattchar->value;
                uint32_t host = *u32;
                *u32 = htonl(host);
            }
            break;
            case 8:
            default:
                break;
            }
        }

        // Inform Edge PT of resource value change
        device_update_characteristic_resource_value(ble, srvc, ch, gattchar->value, gattchar->value_length);
        device_mutex_unlock(ble);
        devices_mutex_unlock();
        tr_debug("    Updated value for characteristic %s", gattchar->dbus_path);
    }
    free(read_userdata->device_id);
    free(read_userdata);
}

/** Reads asynchronously value from the ble characteristic specified by the
 *  dbus path.
 *
 *  characteristic_path
 *       input: the dbus path for the characteristic
 *  device_id
 *       input: the id of the device containing the characteristic
 *  srvc
 *       input: index of the service in the device's service list
 *  ch
 *       input: index of the characteristic in the device's characteristic list
 */

int ble_read_characteristic_async(char *characteristic_path,
                                  char *device_id,
                                  int srvc,
                                  int ch)
{
    GVariantBuilder build_opt;
    GDBusProxy *charProxy;
    GError *err = NULL;
    int rc = 0;

    if (characteristic_path == NULL) {
        tr_err("Characteristic path for asynchronous read is null");
        rc = -1;
        return rc;
    }
    if (device_id == NULL) {
        tr_err("Device id for asynchronous read is null");
        rc = -2;
        return rc;
    }

    assert(g_config.connection != NULL);
    charProxy = g_dbus_proxy_new_sync(g_config.connection,
                                              G_DBUS_PROXY_FLAGS_NONE,
                                              NULL,
                                              BLUEZ_NAME,
                                              characteristic_path,
                                              GATT_CHARACTERISTIC_IFACE,
                                              NULL,
                                              &err);
    if (!G_IS_DBUS_PROXY(charProxy)) {
        tr_err("Get characteristic proxy failed: %s (%d)", err->message, err->code);
        rc = err->code;
        g_clear_error(&err);
        return rc;
    }

    g_variant_builder_init(&build_opt, G_VARIANT_TYPE("a{sv}"));

    struct async_read_userdata *read_userdata = calloc(1, sizeof(struct async_read_userdata));
    read_userdata->device_id = strdup(device_id);
    read_userdata->srvc = srvc;
    read_userdata->ch = ch;

    g_dbus_proxy_call(charProxy,
                            "ReadValue",
                            g_variant_new("(a{sv})", &build_opt),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            (GAsyncReadyCallback) ble_read_characteristic_callback,
                            read_userdata);

    g_object_unref(charProxy);
    return 0;
}

int ble_write_characteristic(const char    *characteristic_path,
                             const uint8_t *data,
                             size_t         size)
{
    GVariant *ret = NULL;
    GDBusProxy *charProxy = NULL;
    GError *err = NULL;
    int rc = 0;

    assert(g_config.connection != NULL);
    charProxy = g_dbus_proxy_new_sync(g_config.connection,
                                      G_DBUS_PROXY_FLAGS_NONE,
                                      NULL,
                                      BLUEZ_NAME,
                                      characteristic_path,
                                      GATT_CHARACTERISTIC_IFACE,
                                      NULL,
                                      &err);

    if (!G_IS_DBUS_PROXY(charProxy)) {
        tr_err("Get characteristic proxy failed: %s (%d)", err->message, err->code);
        rc = err->code;
        g_clear_error(&err);
        return rc;
    }

    GVariant *data_v = g_variant_new_from_data(G_VARIANT_TYPE ("ay"), data, size, TRUE, NULL, NULL);
    if (data_v == NULL) {
        tr_err("Failed to allocate new variant type");
        rc = err->code;
        g_clear_error(&err);
        g_object_unref(charProxy);
        return rc;
    }

    // We pass NULL as last parameter to indicate empty options dictionary
    GVariant *write_value_argument = g_variant_new("(@aya{sv})", data_v, NULL);

    ret = g_dbus_proxy_call_sync(charProxy,
                                 "WriteValue",
                                 write_value_argument,
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 &err);
    if (ret == FALSE) {
        tr_err("Failed write to %s: %s (%d)", characteristic_path, err->message, err->code);
        rc = err->code;
        g_clear_error(&err);
    } else {
        tr_info("Successfully wrote %zu bytes to BLE characteristic %s", size, characteristic_path);
    }

    g_variant_unref(ret);
    g_object_unref(charProxy);

    return rc;
}
