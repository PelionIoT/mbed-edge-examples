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
#include <assert.h>
#include <string.h>

#include <pthread.h>
#include "mbed-trace/mbed_trace.h"
#include "pt_edge.h"
#include "pt_ble.h"
#include "devices.h"
#include "pt-client-2/pt_api.h"
#include "pt-client-2/pt_devices_api.h"
#include "pt-client-2/pt_device_object.h"

// ============================================================================
// Global Variables
// ============================================================================
pthread_t protocol_translator_api_thread;
connection_id_t g_connection_id = PT_API_CONNECTION_ID_INVALID;
pt_client_t *g_client = NULL;
bool unregistering_devices = false;
pthread_mutex_t pt_api_start_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pt_api_start_wait_cond = PTHREAD_COND_INITIALIZER;

void shutdown_and_cleanup();
static pt_status_t resource_callback_handler(const connection_id_t connection_id,
                                             const char *device_id,
                                             const uint16_t object_id,
                                             const uint16_t instance_id,
                                             const uint16_t resource_id,
                                             const uint8_t operation,
                                             const uint8_t *value,
                                             const uint32_t value_size,
                                             void *userdata);

#define TRACE_GROUP "pt-edge"

/*
 * Lwm2mResourceType:
 *  LWM2M_STRING,
 *  LWM2M_INTEGER,
 *  LWM2M_FLOAT,
 *  LWM2M_BOOLEAN,
 *  LWM2M_OPAQUE,
 *  LWM2M_TIME,
 *  LWM2M_OBJLINK
 */
bool
edge_add_resource(const char *device_id,
                  const uint16_t object_id,
                  const uint16_t instance_id,
                  const uint16_t resource_id,
                  const Lwm2mResourceType type,
                  const uint8_t operations,
                  const uint8_t *value,
                  const uint32_t value_size)
{
    pt_status_t status = PT_STATUS_SUCCESS;

    // Since value buffer ownership is transferred to the pt, let's copy it here
    uint8_t *buf = malloc(value_size);
    if (buf == NULL) {
        goto err;
    }

    memcpy(buf, value, value_size);
    status = pt_device_add_resource_with_callback(g_connection_id,
                                         device_id,
                                         object_id,
                                         instance_id,
                                         resource_id,
                                         type,
                                         operations,
                                         buf,
                                         value_size,
                                         free,
                                         resource_callback_handler);

    if (status != PT_STATUS_SUCCESS) {
        goto err;
    }

    return true;

err:
    tr_err("Could not create resource %s/%d/%d/%d ",
           device_id, object_id, instance_id, resource_id);
    return false;
}

void
edge_set_resource_value(const char *device_id,
                        const uint16_t object_id,
                        const uint16_t instance_id,
                        const uint16_t resource_id,
                        const uint8_t *value,
                        const uint32_t value_size)
{
    // pt_resource_set_value free's the value buffer, so copy the value to new buffer
    uint8_t *buf = malloc(value_size);
    if (buf) {
        memcpy(buf, value, value_size);
        pt_device_set_resource_value(g_connection_id,
                                     device_id,
                                     object_id,
                                     instance_id,
                                     resource_id,
                                     buf,
                                     value_size,
                                     free);
    }
}

static pt_status_t resource_callback_handler(const connection_id_t connection_id,
                                             const char *device_id,
                                             const uint16_t object_id,
                                             const uint16_t instance_id,
                                             const uint16_t resource_id,
                                             const uint8_t operation,
                                             const uint8_t *value,
                                             const uint32_t value_size,
                                             void *userdata)
{
    assert(device_id != NULL);
    struct ble_device *ble;

    if (global_keep_running == 0) {
        tr_info("Edge resource callback, ignoring because shutting down.");
        return PT_STATUS_ERROR;
    }

    pt_status_t status = PT_STATUS_ERROR;
    devices_mutex_lock();

    tr_info("Edge resource callback.");

    ble = devices_find_device_by_device_id(device_id);
    if (NULL == ble) {
        tr_warn("No match for device \"%s/%d/%d/%d\".",
                device_id, object_id, instance_id, resource_id);
        status = PT_STATUS_ERROR;
        goto out;
    }

    if (operation & OPERATION_WRITE) {
        tr_info("Attempting write to ble characteristic associated with %s/%d/%d/%d",
                device_id, object_id, instance_id, resource_id);
        if (device_write_characteristic(ble, object_id, instance_id, resource_id,
                                        value, value_size) == 0) {
            status = PT_STATUS_SUCCESS;
            goto out;
        }
    } else if (operation & OPERATION_EXECUTE) {
        tr_info("Executing resource \"%s/%d/%d/%d\".",
                device_id, object_id, instance_id, resource_id);
        /* TODO: Need to implement executable resources */
        status = PT_STATUS_SUCCESS;
        goto out;
    }

out:
    devices_mutex_unlock();
    return status;
}

typedef struct {
    char *device_id;
    bool delete_context;
} unregistered_message_t;

void pt_edge_del_device(struct ble_device *ble)
{
    assert(NULL != ble);
    devices_del_device(ble);
    if (unregistering_devices) {
        // If unregistering all devices, check if this was last one and inform ble eventloop
        // handler if every device has been unregistered
        if (ns_list_count(devices_get_list()) == 0) {
            g_idle_add(pt_ble_g_main_quit_loop, NULL);
        }
    }
}

static gboolean device_unregistered(gpointer unregistered_message_p)
{
    unregistered_message_t *unregistered_message = (unregistered_message_t *) unregistered_message_p;
    char *device_id = unregistered_message->device_id;
    bool delete_context = unregistered_message->delete_context;
    assert(device_id != NULL);

    devices_mutex_lock();

    // Remove the device and clean up any allocated data
    struct ble_device *ble = devices_find_device_by_device_id((char *) device_id);
    free(device_id);
    free(unregistered_message);
    if (NULL != ble) {
        if (delete_context) {
            pt_edge_del_device(ble);
        } else {
            device_set_registered(ble, false);
        }
    }

    devices_mutex_unlock();

    return FALSE;
}

/**
 * \brief Device unregistration success callback handler.
 * In this callback you can react to successful endpoint device unregistration.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The ID of the connection to Edge.
 * \param device_id The device ID from context from the `pt_unregister_device()` call.
 * \param userdata Pointer to `unregistered_message_t` structure.
 */
static void device_unregistration_success(const connection_id_t connection_id, const char* device_id, void *userdata)
{
    tr_info("Device unregistration successful for %s", device_id);
    g_idle_add(device_unregistered, userdata);
}

/**
 * \brief Device unregistration failure callback handler.
 * In this callback you can react to failed endpoint device unregistration.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The ID of the connection to Edge.
 * \param device_id The device ID from context from the `pt_unregister_device()` call.
 * \param userdata Pointer to `unregistered_message_t` structure.
 */
static void device_unregistration_failure(const connection_id_t connection_id, const char* device_id, void *userdata)
{
    tr_info("Device unregistration failed for %s", device_id);
    g_idle_add(device_unregistered, userdata);
}

/**
 * \brief Unregisters the test device
 */
void unregister_devices()
{
    if (unregistering_devices == false) {
        tr_info("Unregistering all devices");
        unregistering_devices = true;
        ble_device_list_t *list;
        list = devices_get_list();
        if (ns_list_count(list) == 0) {
            // No devices to unregister, so we can close immediately
            g_idle_add(pt_ble_g_main_quit_loop, NULL);
        }
        else {
            // Unregister each device, after the last device unregistration has finished
            // we can close
            ns_list_foreach_safe(struct ble_device, dev, list) {
                ble_remove_device(dev);
            }

            // All non-registered devices will be removed by edge_unregister_device, so
            // check if the list is already empty at this point, in which case we can
            // initiate the glib event loop shutdown
            if (ns_list_count(list) == 0) {
                g_idle_add(pt_ble_g_main_quit_loop, NULL);
            }
        }
    }
}

/**
 * \brief Success callback for device write values operation in Pelion Edge.
 * With this callback, you can react to a successful write of device values to Pelion Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * to a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The connection ID from context from the `pt_devices_update()` call.
 * \param userdata The user-supplied context from the `pt_devices_update()` call.
 */
static void device_write_values_success_handler(connection_id_t connection_id, const char *device_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_info("Object structure update finished successfully.");
}

/**
 * \brief Failure callback for device write values operation in Pelion Edge.
 * With this callback, you can react to a failed write of device values to Pelion Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * to a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The connection ID from context from the `pt_devices_update()` call.
 * \param userdata The user-supplied context from the `pt_devices_update()` call.
 */
static void device_write_values_failure_handler(connection_id_t connection_id, const char *device_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_err("Object structure update failed.");
}


static gboolean device_registered(gpointer device_id)
{
    assert(device_id != NULL);
    devices_mutex_lock();
    struct ble_device *ble = devices_find_device_by_device_id(device_id);
    if (ble != NULL) {
        device_set_registered(ble, true);
    }
    else {
        tr_error("Received registration event for unknown device id: %s",
                 (char*)device_id);
    }
    free(device_id);
    devices_mutex_unlock();
    return G_SOURCE_REMOVE;
}

/**
 * \brief Device registration success callback handler.
 * With this callback, you can react to a successful endpoint device
 * registration.
 *
 * The callback runs on the same thread as the event loop of the protocol
 * translator client.
 * If the related functionality of the callback runs a long process, you need
 * to move it a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The ID of the connection to Edge.
 * \param device_id The device ID from context from pt_register_device()
 * \param userdata The user-supplied context from pt_register_device()
 */
static void device_registration_success(const connection_id_t connection_id, const char* device_id, void *userdata)
{
    (void)userdata;
    assert(userdata == NULL);

    tr_info("Device registration successful for '%s', customer code",
            device_id);
    g_idle_add(device_registered, (gpointer)strdup(device_id));
}

/**
 * \brief Device registration failure callback handler.
 * With this callback, you can react to a failed endpoint device registration.
 *
 * The callback runs on the same thread as the event loop of the protocol
 * translator client.
 * If the related functionality of the callback runs a long process, you must
 * move it to a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The ID of the connection to Edge.
 * \param device_id The device ID provided to pt_register_device().
 * \param userdata The user-supplied context provided to pt_register_device().
 */
static void device_registration_failure(const connection_id_t connection_id, const char* device_id, void *userdata)
{
    (void)device_id;
    (void)userdata;
    tr_info("Device registration failed for device '%s'", device_id);
    pthread_cond_signal(&pt_api_start_wait_cond);
    pthread_mutex_unlock(&pt_api_start_wait_mutex);
}

/**
 * \brief Protocol translator registration success callback handler.
 * With this callback, you can react to a successful protocol translator registration to Mbed Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * to a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param userdata The user-supplied context from the `pt_register_protocol_translator()` call.
 */
static void protocol_translator_registration_success(void *userdata)
{
    // Note: PT API mutex (devices_get_mutex()) is locked by the caller
    (void)userdata;

    tr_info("PT registration successful");
    pthread_cond_signal(&pt_api_start_wait_cond);
    pthread_mutex_unlock(&pt_api_start_wait_mutex);

    devices_mutex_lock();
    ns_list_foreach_safe(struct ble_device, dev, devices_get_list()) {
        edge_register_device(dev->device_id);
    }
    devices_mutex_unlock();
}

/**
 * \brief Protocol translator registration failure callback handler.
 * With this callback, you can react to a failed protocol translator registration to Mbed Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param userdata The user-supplied context from the `pt_register_protocol_translator()` call.
 */
static void protocol_translator_registration_failure(void *userdata)
{
    (void)userdata;

    tr_info("PT registration failure, customer code");
    global_keep_running = 0;
    g_idle_add(pt_ble_graceful_shutdown, NULL);
}

/**
 * \brief The implementation of the `pt_connection_ready_cb` function prototype from `pt-client-2/pt_client_api.h`.
 *
 * With this callback, you can react to the protocol translator being ready for passing a
 * message with the Mbed Edge Core.
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The ID of the connection which is ready.
 * \param name The name of the protocol translator.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
static void connection_ready_handler(connection_id_t connection_id, const char *name, void *userdata)
{
    (void) userdata;
    (void) name;
    tr_info("PT connection ready");
    g_connection_id = connection_id;
    g_idle_add(pt_ble_pt_ready, NULL);
}

/**
 * \brief The implementation of the `pt_disconnected_cb` function prototype from `pt-client/pt_api.h`.
 *
 * With this callback, you can react to the protocol translator being disconnected from
 * the Mbed Edge Core.
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The ID of the connection which is ready.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
static void disconnected_handler(connection_id_t connection_id, void *userdata)
{
    (void) userdata;
    (void) connection_id;
    tr_info("Protocol translator got disconnected.");
    g_connection_id = PT_API_CONNECTION_ID_INVALID;
}

/**
 * \brief Implementation of the `pt_connection_shutdown_cb` function prototype
 * for shutting down the client application.
 *
 * The callback to be called when the protocol translator client is shutting down. This
 * lets the client application know when the pt-client is shutting down
 * \param connection The connection of the using application.
 * \param userdata The original userdata from the application.
 *
 * \param connection_id The ID of the connection which is closing down.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
static void shutdown_cb_handler(connection_id_t connection_id, void *userdata)
{
    (void)connection_id;
    (void)userdata;

    tr_info("Shutting down pt client application, customer code");
    // The connection went away. Main thread can quit now.
    if (global_keep_running == 0) {
        tr_warn("Already shutting down.");
        return;
    }
    global_keep_running = 0; // Close main thread
    g_idle_add(pt_ble_graceful_shutdown, NULL);
}

/**
 * \brief Handles certificate renewal notification.
 *        This callback will be called to notify the status when a certificate renewal completes.
 * \param name The name of the certificate.
 * \param initiator 0 - device initiated the renewal \n
 *                  1 - cloud initiated the renewal
 * \param status Status of the certificate renewal.
 *               0 - for success. \n
 *               Non-zero if error happened. See error codes in `ce_status_e` in
 *                   `certificate-enrollment-client/ce_defs.h`.
 * \param description Description of the status in string form for human readability.
 * \param userdata. The Userdata which was passed to `pt_client_start`.
 */
static void certificate_renewal_notification_handler(const connection_id_t connection_id,
                                                     const char *name,
                                                     int32_t initiator,
                                                     int32_t status,
                                                     const char *description,
                                                     void *userdata)
{
    (void) connection_id;
    tr_info("Certificate renewal notification - name: '%s' initiator: %d status: %d description: '%s'",
            name,
            initiator,
            status,
            description);
}


/**
 * \brief Handles device certificate renewal requests.
 *        This callback will be called to request a device to perform certificate enrollment.
 * \param device_id The device which should perform the enrollment.
 * \param name The name of the certificate.
 * \param userdata. The Userdata which was passed to `pt_client_start`.
 */
pt_status_t device_certificate_renew_request_handler(const connection_id_t connection_id,
                                                     const char *device_id,
                                                     const char *name,
                                                     void *userdata)
{
    (void) connection_id;
    tr_info("Certificate renewal request  - device: '%s' certificate: '%s'",
            device_id, name);
    // Not implemented in this example, so we return error
    return PT_STATUS_ERROR;
}

/**
 * \brief A function to start the protocol translator API.
 *
 * This function is the protocol translator threads main entry point.
 *
 * \param ctx The context object must containt `protocol_translator_api_start_ctx_t` structure
 * to pass the initialization data to protocol translator start function.
 */
static void *protocol_translator_api_start_func(void *ctx)
{
    assert(ctx != NULL);
    const protocol_translator_api_start_ctx_t *pt_start_ctx = ctx;
    protocol_translator_callbacks_t pt_cbs;

    tr_info("starting PT thread");
    tr_info("PT thread id is %lx", pthread_self());

    pt_api_init();

    pt_cbs.connection_ready_cb = connection_ready_handler;
    pt_cbs.disconnected_cb = disconnected_handler;
    pt_cbs.connection_shutdown_cb = shutdown_cb_handler;
    pt_cbs.certificate_renewal_notifier_cb = certificate_renewal_notification_handler;
    pt_cbs.device_certificate_renew_request_cb = device_certificate_renew_request_handler;

    pthread_mutex_lock(&pt_api_start_wait_mutex);
    g_client = pt_client_create(pt_start_ctx->socket_path,
                                &pt_cbs);

    if (g_client == NULL) {
        tr_error("Could not create protocol translator client!");
        global_keep_running = 0;
        return NULL;
    }

    if (pt_client_start(g_client,
                        protocol_translator_registration_success,
                        protocol_translator_registration_failure,
                        pt_start_ctx->name,
                        NULL) != 0) {
        global_keep_running = 0;
    }
    return NULL;
}

/**
 * \brief Function to create the protocol translator thread.
 *
 * \param ctx The context to pass initialization data to protocol
 *            translator API.
 */
void
start_protocol_translator_api(protocol_translator_api_start_ctx_t *ctx)
{
    pthread_create(&protocol_translator_api_thread,
                   NULL,
                   &protocol_translator_api_start_func,
                   ctx);
}

/**
 * \brief Function to shutdown the protocol translator API.
 */
void stop_protocol_translator_api()
{
    pthread_mutex_lock(&pt_api_start_wait_mutex);
    if (NULL == g_client) {
        pthread_cond_wait(&pt_api_start_wait_cond, &pt_api_start_wait_mutex);
    }
    pthread_mutex_unlock(&pt_api_start_wait_mutex);
    pt_client_shutdown(g_client);
}

/**
 * \brief Function to stop and clean the protocol translator thread.
 */
void stop_protocol_translator_api_thread()
{
    void *result;

    tr_debug("Waiting for protocol translator api thread to stop.");
    pthread_join(protocol_translator_api_thread, &result);
}

void edge_write_values(const char *device_id)
{
    pt_device_write_values(g_connection_id, device_id, device_write_values_success_handler, device_write_values_failure_handler, NULL);
}

bool edge_create_device(const char *device_id,
                        const char *manufacturer,
                        const char *model_number,
                        const char *serial_number,
                        const char *device_type,
                        uint32_t lifetime,
                        pt_resource_callback reboot_callback)
{
    pt_status_t status = pt_device_create(g_connection_id, device_id, lifetime, QUEUE);
    if (status != PT_STATUS_SUCCESS) {
        if (status == PT_STATUS_ITEM_EXISTS) {
            tr_debug("Device %s already exists", device_id);
            return true;
        }
        tr_err("Could not allocate device structure. (status %d)", status);
        return false;
    }

    ptdo_device_object_data_t device_object_data;
    device_object_data.manufacturer = manufacturer;
    device_object_data.model_number = model_number;
    device_object_data.serial_number = serial_number;
    device_object_data.firmware_version = "N/A";
    device_object_data.hardware_version = "N/A";
    device_object_data.software_version = "N/A";
    device_object_data.device_type = device_type;
    device_object_data.reboot_callback = reboot_callback;
    device_object_data.factory_reset_callback = NULL;
    device_object_data.reset_error_code_callback = NULL;

    ptdo_initialize_device_object(g_connection_id, device_id, &device_object_data);
    return true;
}

void edge_register_device(const char *device_id)
{
    tr_info("registering device: %s", device_id);
    pt_status_t status = pt_device_register(g_connection_id,
                                            device_id,
                                            device_registration_success,
                                            device_registration_failure,
                                            NULL);
    if (PT_STATUS_SUCCESS != status) {
        tr_err("failed to register device: '%s' status: %d", device_id, status);
    }
}

// Returns true if the call succeeded and false otherwise.
bool edge_unregister_device(struct ble_device *dev, bool remove_device_context)
{
    tr_info("Unregistering device: '%s'", dev->device_id);
    if (pt_device_exists(g_connection_id, dev->device_id)) {
        pt_status_t status;
        unregistered_message_t *unregistered_message = calloc(1, sizeof(unregistered_message_t));
        unregistered_message->device_id = strdup(dev->device_id);
        unregistered_message->delete_context = remove_device_context;

        status = pt_device_unregister(g_connection_id,
                                      dev->device_id,
                                      device_unregistration_success,
                                      device_unregistration_failure,
                                      unregistered_message /* userdata */);
        tr_debug("status was %d", status);
        if (PT_STATUS_SUCCESS == status) {
            return true;
        } else {
            free(unregistered_message->device_id);
            free(unregistered_message);
        }
    } else {
        tr_warn("    Device: '%s' doesn't exist", dev->device_id);
    }
    return false;
}

connection_id_t edge_get_connection_id()
{
    return g_connection_id;
}

bool edge_is_connected()
{
    return (g_connection_id != PT_API_CONNECTION_ID_INVALID);
}

bool edge_device_exists(const char *device_id)
{
    return pt_device_exists(g_connection_id, device_id);
}
