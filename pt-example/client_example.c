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
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>

#include "byte-order/byte_order.h"
#include "client_example_clip.h"
#include "common/constants.h"
#include "common/integer_length.h"
#include "device-interface/thermal_zone.h"
#include "examples-common-2/client_config.h"
#include "examples-common-2/ipso_objects.h"
#include "pt-example/client_example.h"

#define TRACE_GROUP "clnt-example"
#include "mbed-trace/mbed_trace.h"
#include "common/edge_trace.h"

/* The protocol translator API include */
#include "pt-client-2/pt_api.h"

#define CPU_TEMPERATURE_DEVICE "cpu-temperature"

connection_id_t g_connection_id = PT_API_CONNECTION_ID_INVALID;
sem_t g_shutdown_handler_called;
pt_client_t *g_client = NULL;

/**
 * \defgroup EDGE_PT_CLIENT_EXAMPLE Protocol translator client example.
 * @{
 */

/**
 * \file client_example.c
 * \brief A usage example of the protocol translator API.
 *
 * This file shows the protocol translator API in use. It describes how to start the protocol
 * translator client and connect it to Edge.
 * It also shows how the callbacks are used to react to the responses to called remote
 * functions.
 *
 * Note that the protocol translator client is run in event loop, so blocking
 * in the callback handlers blocks the whole protocol translator client
 * from processing any further requests and responses. If there is a long
 * running operation for the responses in the callback handlers, you need
 * to move that work into thread.
 */

/**
 * \brief Flag for the protocol translator example run state
 *
 * 0 if the protocol translator main loop should terminate.\n
 * 1 if the protocol translator main loop should continue.
 */
volatile bool g_keep_running = true;

/**
 * \brief Flag for the protocol translator example connected
 *
 * false disconnected state
 * true connected state
 */
volatile bool g_connected = false;
pthread_cond_t g_connected_cond = PTHREAD_COND_INITIALIZER;

/**
 * \brief Flag for protocol translator thread state.
 *
 * true if the thread is not running.\n
 * false if the thread is running.
 */
volatile int g_protocol_translator_api_running = false;

/**
 * \brief The thread structure for protocol translator API
 */
pthread_t protocol_translator_api_thread;

/**
 * \brief Structure to pass the protocol translator initialization
 * data to the protocol translator API.
 */
typedef struct protocol_translator_api_start_ctx {
    pt_client_t *client;
    const char *name;
    const char *endpoint_postfix;
} protocol_translator_api_start_ctx_t;

pthread_mutex_t shutdown_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t shutdown_wait_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t state_data_mutex;

static void wait_until_connected()
{
    pthread_mutex_lock(&state_data_mutex);
    tr_info("Waiting until connected.");
    pthread_cond_wait(&g_connected_cond, &state_data_mutex);
    pthread_mutex_unlock(&state_data_mutex);
}

static bool is_connected()
{
    bool connected;
    pthread_mutex_lock(&state_data_mutex);
    connected = g_connected;
    pthread_mutex_unlock(&state_data_mutex);
    return connected;
}

static void set_connected()
{
    pthread_mutex_lock(&state_data_mutex);
    g_connected = true;
    pthread_cond_signal(&g_connected_cond);
    pthread_mutex_unlock(&state_data_mutex);
}

static void set_disconnected()
{
    pthread_mutex_lock(&state_data_mutex);
    g_connected = false;
    pthread_mutex_unlock(&state_data_mutex);
}

static bool get_keep_running()
{
    bool keep_running;
    pthread_mutex_lock(&state_data_mutex);
    keep_running = g_keep_running;
    pthread_mutex_unlock(&state_data_mutex);
    return keep_running;
}

static void set_keep_running(bool keep_running)
{
    pthread_mutex_lock(&state_data_mutex);
    g_keep_running = keep_running;
    pthread_mutex_unlock(&state_data_mutex);
}

static bool get_protocol_translator_api_running()
{
    bool protocol_translator_api_running;
    pthread_mutex_lock(&state_data_mutex);
    protocol_translator_api_running = g_protocol_translator_api_running;
    pthread_mutex_unlock(&state_data_mutex);
    return protocol_translator_api_running;
}

static void set_protocol_translator_api_running(bool protocol_translator_api_running)
{
    pthread_mutex_lock(&state_data_mutex);
    g_protocol_translator_api_running = protocol_translator_api_running;
    pthread_mutex_unlock(&state_data_mutex);
}

/**
 * \brief Waits for the protocol translator API thread to stop.
 *        The thread needs to be joined in order to avoid a leak.
 */
static void wait_for_protocol_translator_api_thread() {
    void *result;
    tr_debug("Waiting for protocol translator api thread to stop.");
    pthread_join(protocol_translator_api_thread, &result);
    set_protocol_translator_api_running(false);
}

static void pt_devices_unregistration_common(connection_id_t connection_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    pthread_cond_signal(&shutdown_wait_cond);
    tr_debug("pt_devices_unregistration_common - shutting down the PT-client");
    pt_client_shutdown(g_client);
    client_config_free();
    set_keep_running(0); // Close main thread
}

static void devices_unregistration_success(connection_id_t connection_id, void *userdata)
{
    tr_info("Devices unregistration succeeded");
    pt_devices_unregistration_common(connection_id, userdata);
}

static void devices_unregistration_failure(connection_id_t connection_id, void *userdata)
{
    tr_err("Devices unregistration failed");
    pt_devices_unregistration_common(connection_id, userdata);
}

/**
 * \brief Global device list of known devices for this protocol translator.
 */
static void shutdown_and_cleanup()
{
    tr_info("Unregistering all devices");
    pthread_mutex_lock(&shutdown_wait_mutex);
    pt_status_t status = pt_devices_unregister_devices(g_connection_id,
                                                       devices_unregistration_success,
                                                       devices_unregistration_failure,
                                                       NULL);
    if (PT_STATUS_SUCCESS == status) {
        pthread_cond_wait(&shutdown_wait_cond, &shutdown_wait_mutex);
    } else {
        tr_warn("pt_devices_unregister_devices returned %d - shutting down immediately!", status);
        pt_client_shutdown(g_client);
    }
    pthread_mutex_unlock(&shutdown_wait_mutex);
}

/**
 * \brief The client example shutdown handler.
 *
 * \param signum The signal number that initiated the shutdown handler.
 */
static void shutdown_handler(int signum)
{
    sem_post(&g_shutdown_handler_called);
}

static bool is_shutdown_handler_called()
{
    int called;
    sem_getvalue(&g_shutdown_handler_called, &called);
    return called == 1;
}

/**
 * \brief Set up the signal handler for catching signals from OS.
 * This example signal handler setup catches SIGTERM and SIGINT for shutting down
 * the protocol translator client gracefully.
 */
bool setup_signals(void)
{
    struct sigaction sa = { .sa_handler = shutdown_handler, };
    struct sigaction sa_pipe = { .sa_handler = SIG_IGN, };
    int ret_val;

    if (sigemptyset(&sa.sa_mask) != 0) {
        return false;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return false;
    }
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return false;
    }
    ret_val = sigaction(SIGPIPE, &sa_pipe, NULL);
    if (ret_val != 0) {
        tr_warn("setup_signals: sigaction with SIGPIPE returned error=(%d) errno=(%d) strerror=(%s)",
                ret_val,
                errno,
                strerror(errno));
    }
    // This is helpful for debugging. It allows to test shutdown by executing `kill -12 <pt-example-PID>`
    tr_info("Setting support for SIGUSR2");
    if (sigaction(SIGUSR2, &sa, NULL) != 0) {
        return false;
    }
    return true;
}

static void devices_registration_success_cb(connection_id_t connection_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_info("Devices registration succeeded.");
    set_connected();
}

static void devices_registration_failure_cb(connection_id_t connection_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_err("Devices registration failed.");
    set_connected();
}

/**
 * \brief The implementation of the `pt_connection_ready_cb` function prototype from `pt-client/pt_api.h`.
 *
 * With this callback, you can react to the protocol translator being ready for passing a
 * message with the Edge Core.
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The id of the connection which is ready.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
void connection_ready_handler(connection_id_t connection_id, const char *name, void *userdata)
{
    tr_info("Connection with ID %d is ready for '%s', customer code", connection_id, name);
    protocol_translator_api_start_ctx_t *ctx = (protocol_translator_api_start_ctx_t *) userdata;
    g_connection_id = connection_id;
    client_config_create_devices(g_connection_id, ctx->endpoint_postfix);
}
/**
 * \brief Protocol translator registration success callback handler.
 * With this callback, you can react to a successful protocol translator registration to Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * to a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param userdata The user-supplied context from the `pt_register_protocol_translator()` call.
 */
void protocol_translator_registration_success(void *userdata)
{
    (void) userdata;
    tr_info("PT registration successful, customer code");
    set_protocol_translator_api_running(true);
    /* Register already existing devices from device list */
    pt_devices_register_devices(g_connection_id,
                                devices_registration_success_cb,
                                devices_registration_failure_cb,
                                userdata);
}

/**
 * \brief Protocol translator registration failure callback handler.
 * With this callback, you can react to a failed protocol translator registration to Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param userdata The user-supplied context from the `pt_register_protocol_translator()` call.
 */
void protocol_translator_registration_failure(void *userdata)
{
    (void) userdata;
    tr_info("PT registration failure, customer code");
    set_keep_running(false);
    shutdown_and_cleanup();
}

/**
 * \brief The implementation of the `pt_disconnected_cb` function prototype from `pt-client-2/pt_api.h`.
 *
 * With this callback, you can react to the protocol translator being disconnected from
 * the Edge Core.
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection_id The ID of the connection which is disconnected.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
void disconnected_handler(connection_id_t connection_id, void *userdata)
{
    (void) userdata;
    (void) connection_id;
    tr_info("Protocol translator got disconnected.");
    set_disconnected();
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
void certificate_renewal_notification_handler(const connection_id_t connection_id,
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
 * \brief Implementation of the `pt_connection_shutdown_cb` function prototype
 * for shutting down the client application.
 *
 * The callback to be called when the protocol translator client is shutting down. This
 * lets the client application know when the pt-client is shutting down
 *
 * \param connection_id The ID of the connection which is closing down.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
void shutdown_cb_handler(connection_id_t connection_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_info("Shutting down pt client application, customer code");
    // The connection went away. Main thread can quit now.
    if (false == get_keep_running()) {
        tr_warn("Already shutting down.");
        return;
    }
    set_keep_running(false); // Close main thread
}

/**
 * \brief A function to start the protocol translator API.
 *
 * This function is the protocol translator threads main entry point.
 *
 * \param ctx The context object must containt `protocol_translator_api_start_ctx_t` structure
 * to pass the initialization data to protocol translator start function.
 */
void *protocol_translator_api_start_func(void *ctx)
{
    const protocol_translator_api_start_ctx_t *pt_start_ctx = (protocol_translator_api_start_ctx_t*) ctx;

    if (0 != pt_client_start(pt_start_ctx->client,
                             protocol_translator_registration_success,
                             protocol_translator_registration_failure,
                             pt_start_ctx->name,
                             ctx)) {
        set_keep_running(false); // Close main thread
    }
    return NULL;
}

/**
 * \brief Function to create the protocol translator thread.
 *
 * \param ctx The context to pass initialization data to protocol translator API.
 */
void start_protocol_translator_api(protocol_translator_api_start_ctx_t *ctx)
{
    pthread_create(&protocol_translator_api_thread, NULL,
                   &protocol_translator_api_start_func, ctx);
}

static void *malloc_and_memcpy(void *src, size_t size)
{
    void *ret = malloc(size);
    if (ret != NULL) {
        memcpy(ret, src, size);
    }
    return ret;
}

/**
 * \brief Update the given temperature to device object
 *
 * This function updates the given temperature to the device object.
 * Internally this function is responsible of changing the given float
 * byte-order to network byte-order. On little endian architecture the
 * byte-order is changed and on big endian architecture the order is unchanged.
 *
 * \param *device The pointer to device object to update
 * \param temperature The temperature in host network byte order.
 */
void update_temperature_to_device(const char *device_id, float temperature)
{
    tr_info("Updating temperature to device: %f", temperature);

    float current;
    uint8_t *value_buffer;
    uint32_t value_len;
    pt_status_t status = pt_device_get_resource_value(g_connection_id, device_id, TEMPERATURE_SENSOR, 0, SENSOR_VALUE,
                                                      &value_buffer, &value_len);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Current temperature sensor resource value get failed.");
        return;
    }

    convert_value_to_host_order_float(value_buffer, &current);

    /* If value changed update it */
    if (current != temperature) {
        float *nw_temperature = malloc(sizeof(float));
        convert_float_value_to_network_byte_order(temperature, (uint8_t *) nw_temperature);
        /* The value is float, do not change the value_size, original size applies */
        pt_device_set_resource_value(g_connection_id,
                                     device_id,
                                     TEMPERATURE_SENSOR,
                                     0,
                                     SENSOR_VALUE,
                                     (uint8_t *) nw_temperature,
                                     sizeof(float),
                                     free);

        /* Find the min and max resources and update those accordingly
         * Brute force checks, if values are resetted the value changed check
         * for the current value would possibly skip setting the min and max.
         */
        bool min_exists = pt_device_resource_exists(g_connection_id,
                                                    device_id,
                                                    TEMPERATURE_SENSOR,
                                                    0,
                                                    MIN_MEASURED_VALUE);

        if (min_exists) {
            float min_value;
            status = pt_device_get_resource_value(g_connection_id, device_id, TEMPERATURE_SENSOR, 0, MIN_MEASURED_VALUE,
                                                  &value_buffer, &value_len);

            if (status != PT_STATUS_SUCCESS) {
                tr_err("Temperature sensor min resource value get failed.");
                return;
            }

            convert_value_to_host_order_float(value_buffer, &min_value);
            if (temperature < min_value) {
                uint8_t *new_min_value = malloc_and_memcpy(nw_temperature, sizeof(float));
                if (new_min_value) {
                    pt_device_set_resource_value(g_connection_id,
                                                 device_id,
                                                 TEMPERATURE_SENSOR,
                                                 0,
                                                 MIN_MEASURED_VALUE,
                                                 new_min_value,
                                                 sizeof(float),
                                                 free);
                } else {
                    tr_err("Memory allocation failed when allocating new min value");
                }
            }
        }

        bool max_exists = pt_device_resource_exists(g_connection_id,
                                                    device_id,
                                                    TEMPERATURE_SENSOR,
                                                    0,
                                                    MAX_MEASURED_VALUE);
        if (max_exists) {
            float max_value;
            status = pt_device_get_resource_value(g_connection_id, device_id, TEMPERATURE_SENSOR, 0, MAX_MEASURED_VALUE,
                                                  &value_buffer, &value_len);

            if (status != PT_STATUS_SUCCESS) {
                tr_err("Temperature sensor max resource value get failed.");
                return;
            }

            convert_value_to_host_order_float(value_buffer, &max_value);
            if (temperature > max_value) {
                uint8_t *new_max_value = malloc_and_memcpy(nw_temperature, sizeof(float));
                if (new_max_value) {
                    pt_device_set_resource_value(g_connection_id,
                                                 device_id,
                                                 TEMPERATURE_SENSOR,
                                                 0,
                                                 MAX_MEASURED_VALUE,
                                                 new_max_value,
                                                 sizeof(float),
                                                 free);
                } else {
                    tr_err("Memory allocation failed when allocating new max value");
                }
            }
        }
    }
}

void update_object_structure_success_handler(connection_id_t connection_id, void *ctx)
{
    (void) connection_id;
    (void) ctx;
    tr_info("Object structure update finished successfully.");
}

void update_object_structure_failure_handler(connection_id_t connection_id, void *ctx)
{
    (void) connection_id;
    (void) ctx;
    tr_err("Object structure update failed.");
}

void device_register_success_handler(connection_id_t connection_id, const char *device_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_info("Device \"%s\" registered.", device_id);
}

void device_register_failure_handler(connection_id_t connection_id, const char *device_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_info("Device \"%s\" registration failed.", device_id);
}

void main_loop(DocoptArgs *args)
{
    wait_until_connected();

    char *cpu_temperature_device_id = malloc(strlen(CPU_TEMPERATURE_DEVICE) + strlen(args->endpoint_postfix) + 1);
    if (cpu_temperature_device_id) {
        sprintf(cpu_temperature_device_id, "%s%s", CPU_TEMPERATURE_DEVICE, args->endpoint_postfix);
        client_config_create_cpu_temperature_device(g_connection_id, cpu_temperature_device_id);
        pt_device_register(g_connection_id,
                           cpu_temperature_device_id,
                           device_register_success_handler,
                           device_register_failure_handler, NULL);

        while (get_keep_running()) {
            if (is_shutdown_handler_called()) {
                tr_info("Interrupt was received! Shutting down.");
                break;
            }

            if (is_connected()) {
                if (pt_device_exists(g_connection_id, cpu_temperature_device_id) &&
                    get_protocol_translator_api_running()) {
                    float temperature = tzone_read_cpu_temperature();
                    update_temperature_to_device(cpu_temperature_device_id, temperature);
                    pt_devices_update(g_connection_id,
                                      update_object_structure_success_handler,
                                      update_object_structure_failure_handler,
                                      NULL);
                }

            } else {
                tr_debug("main_loop: currently in disconnected state. Not writing any values!");
            }
            sleep(5);
        }
    }
    free(cpu_temperature_device_id);
}

#ifndef BUILD_TYPE_TEST
/**
 * \brief Main entry point to the example application.
 *
 * Mandatory arguments:
 * \li Protocol translator name
 *
 * Optional arguments:
 * \li Endpoint postfix to indicate the running pt-example in device names.
 * \li Edge domain socket path if default path is not used.
 *
 * Starts the protocol translator client and registers the connection ready callback handler.
 *
 * \param argc The number of the command line arguments.
 * \param argv The array of the command line arguments.
 */
int main(int argc, char **argv)
{
    DocoptArgs args = docopt(argc, argv, /* help */ 1, /* version */ "0.1");
    edge_trace_init(args.color_log);
    int32_t result = pthread_mutex_init(&shutdown_wait_mutex, NULL);
    assert(0 == result);
    result = pthread_mutex_init(&state_data_mutex, NULL);
    assert(0 == result);

    sem_init(&g_shutdown_handler_called, 0, 0);

    if (!args.protocol_translator_name) {
        fprintf(stderr, "The --protocol-translator-name parameter is mandatory. Please see --help\n");
        return 1;
    }
    pt_api_init();
    protocol_translator_callbacks_t pt_cbs = {0};
    pt_cbs.connection_ready_cb = connection_ready_handler;
    pt_cbs.disconnected_cb = disconnected_handler;
    pt_cbs.connection_shutdown_cb = shutdown_cb_handler;
    pt_cbs.certificate_renewal_notifier_cb = certificate_renewal_notification_handler;
    pt_cbs.device_certificate_renew_request_cb = device_certificate_renew_request_handler;

    g_client = pt_client_create(args.edge_domain_socket,
                                &pt_cbs);

    /* Setup signal handler to catch SIGINT for shutdown */
    if (!setup_signals()) {
        tr_err("Failed to setup signals.");
        return 1;
    }

    protocol_translator_api_start_ctx_t *ctx = malloc(sizeof(protocol_translator_api_start_ctx_t));
    if (ctx == NULL) {
        tr_err("Could not allocate program context");
        return 1;
    }

    ctx->client = g_client;
    ctx->name = args.protocol_translator_name;
    ctx->endpoint_postfix = args.endpoint_postfix;

    start_protocol_translator_api(ctx);

    main_loop(&args);

    shutdown_and_cleanup();

    tr_info("Main thread waiting for protocol translator api to stop.");
    // Note: to avoid a leak, we should join the created thread from the same thread it was created from.
    wait_for_protocol_translator_api_thread();
    free(ctx);
    pt_client_free(g_client);
    pthread_mutex_destroy(&shutdown_wait_mutex);
    pthread_mutex_destroy(&state_data_mutex);
    edge_trace_destroy();
}
#endif

/**
 * @}
 * close EDGE_PT_CLIENT_EXAMPLE Doxygen group definition
 */
