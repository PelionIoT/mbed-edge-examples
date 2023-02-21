/*
 * ----------------------------------------------------------------------------
 * Copyright 2018 ARM Ltd.
 * Copyright 2023 (c) Izuma Networks
 * ----------------------------------------------------------------------------
 */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>

#include "common/constants.h"
#include "common/integer_length.h"
#include "pt-client/pt_api.h"
#include "pt-client/client.h"
#include "mbed-trace/mbed_trace.h"
#include "examples-common/client_config.h"
#include "stress-tester/stress_tester.h"
#include "examples-common/ipso_objects.h"
#include "device-interface/thermal_zone.h"
#include "byte-order/byte_order.h"
#include "common/integer_length.h"
#define TRACE_GROUP "tester"
#include "stress_tester_clip.h"
#include <errno.h>
#include "common/edge_trace.h"
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "ns_list.h"
#include "unistd.h"
#include "time.h"

/**
 * \defgroup EDGE_PT_STRESS_TESTER Protocol translator stress tester.
 * @{
 */

/**
 * \file stress_tester.c
 * \brief Tests the thread safety of protocol translator API.
 *
 */

#define RANDOM_DEVICE_PREFIX "rand"
#define LIFETIME 86400
#define MAX_DEVICE_ID_LEN 50
struct stress_tester_s;
struct pt_api_thread_s;

/**
 * \brief Holds data for the test thread.
 *        Each test thread will use 1 protocol API connection.
 */
typedef struct {
    pthread_t thread;
    int32_t test_thread_index;
    pt_device_list_t *_devices;
    struct pt_api_thread_s *api_data;
    pthread_mutex_t device_list_mutex;
} test_thread_t;

typedef struct {
    bool registered;
} device_userdata_t;

/**
 * \brief Structure to pass the protocol translator initialization
 * data to the protocol translator API.
 */
typedef struct protocol_translator_api_start_ctx {
    const char *socket_path;
    char *name;
    struct pt_api_thread_s *api_thread;
} protocol_translator_api_start_ctx_t;

/**
 *  \brief Holds data for the Protocol API thread.
 *         This thread will run libevent loop.
 *         Each PT API thread will have their own connection.
 */
typedef struct pt_api_thread_s {
    pthread_mutex_t connection_mutex;
    int32_t pt_index;
    pthread_t thread;
    connection_t *connection;
    struct protocol_translator_api_start_ctx *start_ctx;
    struct stress_tester_s *tester;

    bool protocol_translator_api_running;
    bool connected;
    bool keep_running;
} pt_api_thread_t;

typedef struct {

} device_registration_parameter_t;

/**
 * \brief Main structure for the C-API stress tester program.
 */
typedef struct stress_tester_s {
    DocoptArgs *args;
    int32_t number_of_threads;
    int32_t number_of_protocol_translators;
    int32_t max_number_of_devices;
    int32_t min_number_of_devices;
    int32_t test_duration_seconds;
    int32_t sleep_time_ms;
    test_thread_t *test_threads;
    pt_api_thread_t *api_threads; // Each protocol translator needs its own API thread.
    pthread_t *shutdown_thread;
    bool test_threads_exited;
    bool parallel_connection_lock;
    struct timespec start_time;
} stress_tester_t;

// Defines the random actions which the test threads will execute
typedef enum {
    ACTION_REGISTER_DEVICE,
    ACTION_UNREGISTER_DEVICE,
    ACTION_SET_RANDOM_VALUE,
    ACTION_LAST // only marks list termination
} test_action_e;

stress_tester_t g_tester;

volatile int shutdown_initiated = 0;

static void unregister_devices(test_thread_t *test_data);

static void sleep_ms(int64_t milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}
static connection_t *api_data_conditionally_lock_connection(pt_api_thread_t *api_data)
{
    if (api_data->tester->parallel_connection_lock) {
        pthread_mutex_lock(&api_data->connection_mutex);
    }
    return api_data->connection;
}

static void api_data_conditionally_unlock_connection(pt_api_thread_t *api_data)
{
    if (api_data->tester->parallel_connection_lock) {
        pthread_mutex_unlock(&api_data->connection_mutex);
    }
}

static void test_data_conditionally_lock_device_list(test_thread_t *test_data)
{
    pthread_mutex_lock(&test_data->device_list_mutex);
}

static void test_data_conditionally_unlock_device_list(test_thread_t *test_data)
{
    pthread_mutex_unlock(&test_data->device_list_mutex);
}

static void wait_for_test_threads(stress_tester_t *tester)
{
    int32_t index;
    void *result;
    tr_debug("Waiting for test threads to stop.");
    for (index = 0; index < tester->number_of_threads; index++) {
        test_thread_t *test_data = &tester->test_threads[index];
        pthread_join(test_data->thread, &result);
        tr_debug("Joined test thread %d", index);
    }
    tr_info("All test threads have exited.");
    tester->test_threads_exited = true;
}

/**
 * \brief Waits for the protocol translator API threads to stop.
 *        The thread needs to be joined in order to avoid a leak.
 */
static void wait_for_protocol_translator_api_threads(stress_tester_t *tester)
{
    void *result;
    int32_t index;
    tr_debug("Waiting for protocol translator api thread to stop.");
    for (index = 0; index < tester->number_of_protocol_translators; index++) {
        pt_api_thread_t *api_data = &tester->api_threads[index];
        pthread_join(api_data->thread, &result);
        api_data->protocol_translator_api_running = false;
        pthread_mutex_destroy(&api_data->connection_mutex);
    }
    tr_debug("All protocol translator threads have finished.");
}

/**
 * \brief Global device list of known devices for this protocol translator.
 */

/**
 * \brief Find the device by device id from the devices list.
 *
 * \param device_id The device identifier.
 * \return The device if found.\n
 *         NULL is returned if the device is not found.
 */
pt_device_t *find_device(test_thread_t *test_data, const char *device_id)
{
    ns_list_foreach(pt_device_entry_t, cur, test_data->_devices)
    {
        if (strlen(cur->device->device_id) == strlen(device_id) &&
            strncmp(cur->device->device_id, device_id, strlen(cur->device->device_id)) == 0) {
            return cur->device;
        }
    }
    return NULL;
}

static void unregister_device_entry_common(test_thread_t *test_data, pt_device_entry_t *cur)
{
    pt_api_thread_t *api_data = test_data->api_data;
    pt_device_list_t *_devices = test_data->_devices;
    pt_status_t status = PT_STATUS_SUCCESS;
    if (api_data_conditionally_lock_connection(api_data)) {
        status = pt_unregister_device(api_data->connection,
                                      cur->device,
                                      device_unregistration_success,
                                      device_unregistration_failure,
                                      /* userdata */ test_data);
    }
    api_data_conditionally_unlock_connection(api_data);

    if (PT_STATUS_SUCCESS != status) {
        /* Error happened, remove device forcefully */
        tr_err("Error in unregistering '%s'", cur->device->device_id);
        pt_device_free(cur->device);
        ns_list_remove(_devices, cur);
        free(cur);
    }
}

/**
 * \brief Unregisters the test device
 */
static void unregister_devices(test_thread_t *test_data)
{
    tr_info("Unregistering all devices for Test thread #%d", test_data->test_thread_index);
    test_data_conditionally_lock_device_list(test_data);
    ns_list_foreach_safe(pt_device_entry_t, cur, test_data->_devices)
    {
        unregister_device_entry_common(test_data, cur);
    }
    test_data_conditionally_unlock_device_list(test_data);
}

static void shutdown_and_cleanup(stress_tester_t *tester)
{
    int32_t index;
    tr_info("shutdown_and_cleanup called - waiting for test threads!");

    // Wait for the test threads to exit so we can unregister all devices robustly.
    while (!tester->test_threads_exited) {
        sleep_ms(5);
    }
    tr_info("All test threads have exited");
    for (index = 0; index < tester->number_of_protocol_translators; index++) {
        pt_api_thread_t *api_data = &tester->api_threads[index];

        tr_info("Shutting down connection!");
        if (api_data_conditionally_lock_connection(api_data)) {
            pt_client_shutdown(api_data->connection);
        }
        api_data_conditionally_unlock_connection(api_data);
        api_data->keep_running = false;
    }
}

void *shutdown_thread_func(void *arg)
{
    stress_tester_t *tester = arg;
    shutdown_and_cleanup(tester);

    return NULL;
}

/**
 * \brief The client example shutdown handler.
 *
 * \param signum The signal number that initiated the shutdown handler.
 */
void shutdown_handler(int signum)
{
    tr_info("Shutdown handler when interrupt %d is received, customer code", signum);
    if (!shutdown_initiated) {
        shutdown_initiated = 1;
        if (!g_tester.shutdown_thread) {
            g_tester.shutdown_thread = calloc(1, sizeof(pthread_t));
            // The interrupt handler will be executed in the main thread.
            // The shutdown needs to be run in a separate thread so that the main thread can join the test threads.
            pthread_create(g_tester.shutdown_thread, NULL, &shutdown_thread_func, &g_tester);
        }
    }
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
    tr_info("Setting support for SIGUSR2");
    if (sigaction(SIGUSR2, &sa, NULL) != 0) {
        return false;
    }
    return true;
}

/**
 * \brief Value write remote procedure failed callback handler.
 * With this callback, you can react to a failed value write to Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * to a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param device_id The device id from context from the `pt_write_value()` call.
 * \param userdata The user-supplied context from the `pt_write_value()` call.
 */
void write_value_failure(const char* device_id, void *userdata)
{
    (void) userdata;
    tr_err("Write value failure for device %s, customer code", device_id);
}

/**
 * \brief Value write remote procedure success callback handler.
 * With this callback, you can react to a successful value write to Edge.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * to a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param device_id The device id from context from the `pt_write_value()` call.
 * \param userdata The user-supplied context from the `pt_write_value()` call.
 */
void write_value_success(const char* device_id, void *userdata)
{
    pt_api_thread_t *api_data = (pt_api_thread_t *) userdata;
    tr_info("Write value success for device %s, customer code in Protocol Translator #%d",
            device_id,
            api_data->pt_index);
}

/**
 * \brief Used keep track of the devices that have been registered
 * \param api_data API thread data.
 * \param device_id The device_id which will be used.
 * \param value true if the device should be added to the list of registered devices.
 *              false if device should be removed from the list of registered devices.
 */
static void test_data_set_device_registered(test_thread_t *test_data, char *device_id, bool value)
{
    pt_device_t *device = find_device(test_data, device_id);
    pt_device_userdata_t *userdata = device->userdata;
    device_userdata_t *data = userdata->data;
    data->registered = value;
}

/**
 * \brief Used to check if device has been registered
 * \param test_data test thread data.
 * \param device_id The device_id which will be used.
 * \return true The device is registered
 *         false The device is not registered
 */
static bool test_data_is_device_registered(test_thread_t *test_data, char *device_id)
{
    pt_device_t *device = find_device(test_data, device_id);
    pt_device_userdata_t *userdata = device->userdata;
    device_userdata_t *data = userdata->data;
    return data->registered;
}

/**
 * \brief Counts how many devices have been registered.
 * \param test_data test thread data.
 */
static uint32_t test_data_count_registered_devices(test_thread_t *test_data)
{
    uint32_t count = 0;
    ns_list_foreach(pt_device_entry_t, cur, test_data->_devices)
    {
        pt_device_t *device = cur->device;
        pt_device_userdata_t *userdata = device->userdata;
        device_userdata_t *data = userdata->data;
        if (data->registered) {
            count++;
        }
    }
    return count;
}

/**
 * \brief Device registration success callback handler.
 * With this callback, you can react to a successful endpoint device registration.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param device_id The device id from context from the `pt_register_device()` call.
 * \param userdata The user-supplied context from the `pt_register_device()` call.
 */
void device_registration_success(const char* device_id, void *userdata)
{
    test_thread_t *test_data = (test_thread_t *) userdata;
    tr_info("Device registration successful for %s in Test thread with index %d",
            device_id,
            test_data->test_thread_index);
    test_data_conditionally_lock_device_list(test_data);
    test_data_set_device_registered(test_data, (char *) device_id, true);
    test_data_conditionally_unlock_device_list(test_data);
}

/**
 * \brief Device registration failure callback handler.
 * With this callback, you can react to a failed endpoint device registration.
 *
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you must move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param device_id The device id from context from the `pt_register_device()` call.
 * \param userdata The user-supplied context from the `pt_register_device()` call.
 */
void device_registration_failure(const char* device_id, void *userdata)
{
    test_thread_t *test_data = (test_thread_t *) userdata;
    tr_info("Device registration failure for '%s' in test thread #%d", device_id, test_data->test_thread_index);
    test_data->api_data->keep_running = false;
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
 * \param device_id The device id from context from the `pt_unregister_device()` call.
 * \param userdata The user supplied context from the `pt_unregister_device()` call.
 */
void device_unregistration_success(const char* device_id, void *userdata)
{
    test_thread_t *test_data = userdata;
    tr_info("Device unregistration successful for '%s', customer code", device_id);
    /* Remove the device from device list and free the allocated memory */
    test_data_conditionally_lock_device_list(test_data);
    test_data_set_device_registered(test_data, (char *) device_id, false);
    ns_list_foreach_safe(pt_device_entry_t, cur, test_data->_devices)
    {
        if (strlen(device_id) == strlen(cur->device->device_id) &&
            strncmp(device_id, cur->device->device_id, strlen(device_id)) == 0) {
            pt_device_free(cur->device);
            ns_list_remove(test_data->_devices, cur);
            free(cur);
            break;
        }
    }
    test_data_conditionally_unlock_device_list(test_data);
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
 * \param device_id The device id from context from the `pt_unregister_device()` call.
 * \param userdata The user supplied context from the `pt_unregister_device()` call.
 */
void device_unregistration_failure(const char* device_id, void *userdata)
{
    tr_err("Device unregistration failure for '%s', customer code", device_id);
}

void protocol_translator_registration_success(void *userdata)
{
    (void) userdata;
    tr_info("PT registration successful, customer code");
    pt_api_thread_t *api_data = (pt_api_thread_t *) userdata;
    api_data->protocol_translator_api_running = true;
}

void protocol_translator_registration_failure(void *userdata)
{
    tr_err("PT registration failure, customer code");
    pt_api_thread_t *api_data = (pt_api_thread_t *) userdata;
    api_data->keep_running = false;
    shutdown_and_cleanup(api_data->tester);
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
 * \param connection The connection which is ready.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
void connection_ready_handler(connection_t *connection, void *userdata)
{
    /* Initiate protocol translator registration */
    pt_api_thread_t *api_data = (pt_api_thread_t *) userdata;
    tr_info("Connection #%d is ready", api_data->pt_index);
    pt_status_t status = PT_STATUS_SUCCESS;
    api_data_conditionally_lock_connection(api_data);
    status = pt_register_protocol_translator(connection,
                                             protocol_translator_registration_success,
                                             protocol_translator_registration_failure,
                                             userdata);
    api_data_conditionally_unlock_connection(api_data);
    if (status != PT_STATUS_SUCCESS) {
        shutdown_and_cleanup(api_data->tester);
    }
    api_data->connected = true;
}

/**
 * \brief The implementation of the `pt_disconnected_cb` function prototype from `pt-client/pt_api.h`.
 *
 * With this callback, you can react to the protocol translator being disconnected from
 * the Edge Core.
 * The callback runs on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback runs a long process, you need to move it to
 * a worker thread. If the process runs directly in the callback, it
 * blocks the event loop and thus, blocks the protocol translator.
 *
 * \param connection The connection which is disconnected.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
void disconnected_handler(connection_t *connection, void *userdata)
{
    (void) userdata;
    (void) connection;
    pt_api_thread_t *api_data = userdata;
    tr_info("Protocol translator got disconnected.");
    api_data->connected = false;
}

/**
 * \brief Implementation of `pt_received_write_handle` function prototype handler for
 * write messages received from the Edge Core.
 *
 * The callback is run on the same thread as the event loop of the protocol translator client.
 * If the related functionality of the callback does some long processing the processing
 * must be moved to worker thread. If the processing is run directly in the callback it
 * will block the event loop and therefore it will block the whole protocol translator.
 *
 * \param connection The connection that this write originates from.
 * \param device_id The device id receiving the write.
 * \param object_id The object id of the object receiving the write.
 * \param instance_id The object instance id of the object instance receiving the write.
 * \param resource_id The resource id of the resource receiving the write.
 * \param operation The operation on the resource. See `constants.h` for the defined values.
 * \param value The argument byte buffer of the write operation.
 * \param value_size The size of the value argument.
 * \param userdata* Received userdata from write message.
 *
 * \return Returns 0 on success and non-zero on failure.
 */
int received_write_handler(connection_t* connection,
                           const char *device_id, const uint16_t object_id,
                           const uint16_t instance_id, const uint16_t resource_id,
                           const unsigned int operation,
                           const uint8_t *value, const uint32_t value_size,
                           void* userdata)
{
    tr_info("Edge write to protocol translator.");
    pt_api_thread_t *api_data = userdata;
    stress_tester_t *tester = api_data->tester;
    int32_t index;

    for (index = 0; index < tester->number_of_threads; index++) {
        test_thread_t *test_data = &tester->test_threads[index];

        test_data_conditionally_lock_device_list(test_data);
        pt_device_t *device = find_device(test_data, device_id);
        if (device) {
            pt_object_t *object = pt_device_find_object(device, object_id);
            pt_object_instance_t *instance = pt_object_find_object_instance(object, instance_id);
            const pt_resource_t *resource = pt_object_instance_find_resource(instance, resource_id);

            if (!device || !object || !instance || !resource) {
                tr_warn("No match for device \"%s/%d/%d/%d\" on write action.",
                        device_id,
                        object_id,
                        instance_id,
                        resource_id);
                return 1;
            }

            /* Check if resource supports operation */
            if (!(resource->operations & operation)) {
                tr_warn("Operation %d tried on resource \"%s/%d/%d/%d\" which does not support it.",
                        operation,
                        device_id,
                        object_id,
                        instance_id,
                        resource_id);
                return 1;
            }

            if ((operation & OPERATION_WRITE) && resource->callback) {
                tr_info("Writing new value to \"%s/%d/%d/%d\".", device_id, object_id, instance_id, resource_id);
                /*
                 * The callback must validate the value size to be acceptable.
                 * For example, if resource value type is float, the value_size must be acceptable
                 * for the float field. And if the resource value type is string the
                 * callback may have to reallocate the reserved memory for the new value.
                 */
                resource->callback(resource, value, value_size, NULL);
            } else if ((operation & OPERATION_EXECUTE) && resource->callback) {
                resource->callback(resource, value, value_size, NULL);
                /*
                 * Update the reset min and max to Edge Core. The Edge Core cannot know the
                 * resetted values unless written back.
                 */
                if (api_data_conditionally_lock_connection(api_data)) {
                    pt_write_value(connection,
                                   device,
                                   device->objects,
                                   write_value_success,
                                   write_value_failure,
                                   (void *) device_id);
                }
                api_data_conditionally_unlock_connection(api_data);
            }
        }
        test_data_conditionally_unlock_device_list(test_data);
    }
    return 0;
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
 * \param connection The connection which is closing down.
 * \param userdata The user supplied context from the `pt_register_protocol_translator()` call.
 */
void shutdown_cb_handler(connection_t **connection, void *userdata)
{
    pt_api_thread_t *api_data = (pt_api_thread_t *) userdata;
    tr_info("Shutting down tester application, customer code");
    api_data->keep_running = false;
}

/**
 * \brief A function to start the protocol translator API.
 *
 * This function is the protocol translator threads main entry point.
 *
 * \param ctx The context object must contain `protocol_translator_api_start_ctx_t` structure
 * to pass the initialization data to protocol translator start function.
 */
void *protocol_translator_api_start_func(void *ctx)
{
    const protocol_translator_api_start_ctx_t *pt_start_ctx = (protocol_translator_api_start_ctx_t *) ctx;

    protocol_translator_callbacks_t pt_cbs;
    pt_cbs.connection_ready_cb = (pt_connection_ready_cb) connection_ready_handler;
    pt_cbs.disconnected_cb = (pt_disconnected_cb) disconnected_handler;
    pt_cbs.received_write_cb = (pt_received_write_handler) received_write_handler;
    pt_cbs.connection_shutdown_cb = (pt_connection_shutdown_cb) shutdown_cb_handler;
    pt_api_thread_t *api_data = pt_start_ctx->api_thread;
    // Don't lock connection mutex here, because this function starts the eventloop.
    if (0 != pt_client_start(pt_start_ctx->socket_path,
                             pt_start_ctx->name,
                             &pt_cbs,
                             api_data /* userdata */,
                             &api_data->connection)) {
        api_data->keep_running = false;
    }
    api_data_conditionally_lock_connection(api_data);
    if (api_data->connection) {
        free(api_data->connection);
        api_data->connection = NULL;
        tr_debug("Freed and nullified the connection in Protocol API #%d", api_data->pt_index);
    } else {
        tr_err("protocol_translator_api_start_func: why was the connection already NULL?");
    }

    api_data_conditionally_unlock_connection(api_data);
    return NULL;
}

/**
 * \brief Function to create the protocol translator thread.
 *
 * \param ctx The context to pass initialization data to protocol translator API.
 */
void start_protocol_translator_api(protocol_translator_api_start_ctx_t *ctx, pthread_t *thread)
{
    pthread_create(thread, NULL, &protocol_translator_api_start_func, ctx);
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
void update_temperature_to_device(pt_device_t *device, float temperature)
{
    tr_info("Updating temperature to device: %f", temperature);
    pt_object_t *object = pt_device_find_object(device, TEMPERATURE_SENSOR);
    pt_object_instance_t *instance = pt_object_find_object_instance(object, 0);
    pt_resource_t *resource = pt_object_instance_find_resource(instance, SENSOR_VALUE);

    if (!object || !instance || !resource) {
        tr_err("Could not find the cpu temperature resource.");
        return;
    }

    float current;
    convert_value_to_host_order_float(resource->value, &current);

    /* If value changed update it */
    if (current != temperature) {
        current = temperature; // current value is now the temperature passed in.
        float nw_temperature;
        convert_float_value_to_network_byte_order(temperature,
                                                  (uint8_t*) &nw_temperature);
        /* The value is float, do not change the value_size, original size applies */
        memcpy(resource->value, &nw_temperature, resource->value_size);
    }

    /* Find the min and max resources and update those accordingly
     * Brute force checks, if values are resetted the value changed check
     * for the current value would possibly skip setting the min and max.
     */
    pt_resource_t *min = pt_object_instance_find_resource(instance, MIN_MEASURED_VALUE);
    if (min) {
        float min_value;
        convert_value_to_host_order_float(min->value, &min_value);
        if (current < min_value) {
            memcpy(min->value, resource->value, resource->value_size);
        }
    }

    pt_resource_t *max = pt_object_instance_find_resource(instance, MAX_MEASURED_VALUE);
    if (max) {
        float max_value;
        convert_value_to_host_order_float(max->value, &max_value);
        if (current > max_value) {
        memcpy(max->value, resource->value, resource->value_size);
        }
    }
}

static struct protocol_translator_api_start_ctx *create_start_ctx(int32_t index, pt_api_thread_t *api_data)
{
    struct protocol_translator_api_start_ctx *ctx = calloc(1, sizeof(struct protocol_translator_api_start_ctx));
    stress_tester_t *tester = api_data->tester;
    ctx->api_thread = api_data;
    ctx->name = calloc(1,
                       strlen(tester->args->protocol_translator_name) + edge_int_length(index) + 1 /* - */ +
                               1 /* NULL terminator */);
    sprintf(ctx->name, "%s-%d", tester->args->protocol_translator_name, index);
    ctx->socket_path = tester->args->edge_domain_socket;
    return ctx;
}

static void create_pt_api_threads(stress_tester_t *tester)
{
    int32_t index;
    tester->api_threads = (pt_api_thread_t *) calloc(tester->number_of_protocol_translators, sizeof(pt_api_thread_t));
    for (index = 0; index < tester->number_of_protocol_translators; index++) {
        pt_api_thread_t *api_data = &tester->api_threads[index];
        pthread_mutex_init(&api_data->connection_mutex, NULL);
        api_data->pt_index = index;
        api_data->keep_running = true;
        api_data->tester = tester;
        api_data->start_ctx = create_start_ctx(index, api_data);

        start_protocol_translator_api(api_data->start_ctx, &api_data->thread);
    }
}

static char *get_random_device_id(int32_t thread_index)
{
    int32_t num = rand();
    char *device_id = calloc(1,
                             strlen(RANDOM_DEVICE_PREFIX) + 1 /* '-' */ + edge_int_length(thread_index) +
                                     edge_int_length(num) + 1 /* '-' */ + 1 /* NUL terminator */);
    sprintf(device_id, "%s-%d-%d", RANDOM_DEVICE_PREFIX, thread_index, num);
    return device_id;
}

static void register_random_device(test_thread_t *test_data)
{
    char *device_id;
    pt_api_thread_t *api_data = test_data->api_data;
    int32_t device_count = ns_list_count(test_data->_devices);
    if (device_count >= test_data->api_data->tester->max_number_of_devices) {
        tr_err("Cannot register new device because maximum number of devices is %d",
               test_data->api_data->tester->max_number_of_devices);
        return;
    }

    do {
        device_id = get_random_device_id(test_data->test_thread_index);
        if (!find_device(test_data, device_id)) {
            // found non-existing device
            break;
        }
        free(device_id);
    } while (true);
    tr_info("Registering device %s", device_id);
    device_userdata_t *data = (device_userdata_t *) calloc(1, sizeof(device_userdata_t));
    pt_device_userdata_t *userdata = pt_api_create_device_userdata(data, free);
    pt_device_t *device = client_config_create_device_with_userdata(device_id, "", userdata);
    free(device_id);
    ipso_create_thermometer(device, 0, 24, false, NULL);
    pt_device_entry_t *device_entry = malloc(sizeof(pt_device_entry_t));
    device_entry->device = device;
    ns_list_add_to_end(test_data->_devices, device_entry);
    if (api_data_conditionally_lock_connection(api_data)) {
        pt_register_device(test_data->api_data->connection,
                           device,
                           device_registration_success,
                           device_registration_failure,
                           test_data);
    }
    api_data_conditionally_unlock_connection(api_data);
}

static void unregister_random_device(test_thread_t *test_data)
{
    tr_info("Unregister random device");
    pt_api_thread_t *api_data = test_data->api_data;
    int32_t device_count = ns_list_count(test_data->_devices);
    if (device_count <= api_data->tester->min_number_of_devices) {
        tr_err("Cannot remove device because minimum number of devices is %d",
               api_data->tester->min_number_of_devices);
        return;
    }

    if (device_count > 0) {
        int32_t chosen_index = rand() / (RAND_MAX / device_count + 1);
        int32_t index = 0;
        ns_list_foreach_safe(pt_device_entry_t, entry, test_data->_devices)
        {
            if (index == chosen_index) {
                unregister_device_entry_common(test_data, entry);
                break;
            }
            index++;
        }
    } else {
        tr_err("No devices to unregister!");
    }
}

static void set_random_value_for_device(test_thread_t *test_data, pt_device_t *device)
{
    pt_api_thread_t *api_data = test_data->api_data;
    if (test_data_is_device_registered(test_data, device->device_id)) {
        float temperature = rand() / (1.0 * RAND_MAX) * 135 - 35;
        update_temperature_to_device(device, temperature);
        if (api_data_conditionally_lock_connection(api_data)) {
            pt_write_value(api_data->connection,
                           device,
                           device->objects,
                           write_value_success,
                           write_value_failure,
                           api_data);
        }
        api_data_conditionally_unlock_connection(api_data);
    }
}

static void set_random_value(test_thread_t *test_data)
{
    tr_info("Set random value");
    int32_t device_count = ns_list_count(test_data->_devices);
    if (device_count > 0) {
        int32_t chosen_index = rand() / (RAND_MAX / device_count + 1);
        int32_t index = 0;
        ns_list_foreach_safe(pt_device_entry_t, entry, test_data->_devices)
        {
            if (index == chosen_index) {
                set_random_value_for_device(test_data, entry->device);
            }
            index++;
        }
    }
}

static void run_test_action(test_thread_t *test_data)
{
    test_action_e action = rand() / (RAND_MAX / ACTION_LAST + 1);
    pt_api_thread_t *api_data = test_data->api_data;

    test_data_conditionally_lock_device_list(test_data);
    if (api_data->connected && api_data->protocol_translator_api_running) {
        switch (action) {
            case ACTION_REGISTER_DEVICE: {
                register_random_device(test_data);
            } break;

            case ACTION_UNREGISTER_DEVICE: {
                unregister_random_device(test_data);
            } break;

            case ACTION_SET_RANDOM_VALUE: {
                set_random_value(test_data);
            } break;

            default:
                tr_info("No test implementation for action %d", action);
                break;
        }
    } else {
        tr_warn("No connection or not registered yet. Cannot do tests.");
    }
    test_data_conditionally_unlock_device_list(test_data);
}

static void *test_thread_func(void *arg) {
    test_thread_t *test_data = arg;
    pt_api_thread_t *api_data = test_data->api_data;
    bool done = false;

    while (!done) {

        if (api_data->keep_running && !shutdown_initiated) {
            if (api_data->connected) {
                run_test_action(test_data);
            } else {
                tr_debug("test thread: currently in disconnected state. Not writing any values!");
            }
        } else {
            done = true;
        }
        if (!done) {
            sleep_ms(api_data->tester->sleep_time_ms);
        }
    }
    unregister_devices(test_data);
    done = false;
    while (!done) {
        test_data_conditionally_lock_device_list(test_data);
        int32_t device_count = ns_list_count(test_data->_devices);
        int32_t registered_devices_count = test_data_count_registered_devices(test_data);
        tr_info("Waiting for the %d devices to be unregistered in thread #%d - number of registered devices is %d",
                device_count,
                test_data->test_thread_index,
                registered_devices_count);
        if (device_count == 0 && registered_devices_count == 0) {
            done = true;
        }
        test_data_conditionally_unlock_device_list(test_data);
        if (!done) {
            sleep_ms(200);
        }
    }
    free(test_data->_devices);
    test_data->_devices = NULL;
    pthread_mutex_destroy(&test_data->device_list_mutex);
    tr_info("test_thread %d exited", test_data->test_thread_index);
    return NULL;
}

static void create_test_threads(stress_tester_t *tester)
{
    int32_t index;
    tester->test_threads = (test_thread_t *) calloc(tester->number_of_threads, sizeof(test_thread_t));
    for (index = 0; index < tester->number_of_threads; index ++) {
        test_thread_t *test_data = &tester->test_threads[index];
        test_data->api_data = &tester->api_threads[index % tester->number_of_protocol_translators];
        pthread_t *thread = &test_data->thread;
        test_data->test_thread_index = index;
        pthread_mutex_init(&test_data->device_list_mutex, NULL);
        pt_device_list_t *device_list = calloc(1, sizeof(pt_device_list_t));
        ns_list_init(device_list);
        test_data->_devices = device_list;
        pthread_create(thread, NULL, &test_thread_func, test_data);
    }
}

/**
 * \brief Creates the stress tester
 * \return true - the tester was created successfully.
 *         false - creating the tester failed.
 */
static bool create_tester(stress_tester_t *tester, DocoptArgs *args)
{
    tester->number_of_threads = atoi(args->number_of_threads);
    tester->min_number_of_devices = atoi(args->min_devices);
    tester->max_number_of_devices = atoi(args->max_devices);
    tester->number_of_protocol_translators = atoi(args->number_of_protocol_translators);
    if (tester->number_of_threads < tester->number_of_protocol_translators) {
        tr_err("Number of test threads is %d which is less than number of protol translators, %d. This is not valid "
               "test setup!",
               tester->number_of_threads,
               tester->number_of_protocol_translators);
        return false;
    }
    tester->sleep_time_ms = atoi(args->sleep_time_ms);
    tester->args = args;
    tester->parallel_connection_lock = atoi(args->parallel_connection_lock);
    tester->test_duration_seconds = atoi(args->test_duration_seconds);
    create_pt_api_threads(tester);
    create_test_threads(tester);
    return true;
}

void *timer_thread_func(void *arg)
{
    tr_debug("Timer thread started");
    stress_tester_t *tester = arg;
    struct timespec time_now;
    pt_api_thread_t *api_data = &tester->api_threads[0];
    while (api_data->keep_running) {
        clock_gettime(CLOCK_REALTIME, &time_now);
        if (time_now.tv_sec - tester->start_time.tv_sec > tester->test_duration_seconds) {
            if (!shutdown_initiated) {
                shutdown_initiated = 1;
                shutdown_and_cleanup(tester);
            }
        }

        sleep_ms(5);
    }
    tr_debug("Timer thread finished");
    return NULL;
}

/**
 * \brief Main entry point to the C-API stress tester application
 *
 * Starts the stress tests and runs them until the user interrupts or the test duration has expired.
 * Before exiting, it frees the data and cleans the allocated resources.
 *
 * \param argc The number of the command line arguments.
 * \param argv The array of the command line arguments.
 */
int main(int argc, char **argv)
{
    clock_gettime(CLOCK_REALTIME, &g_tester.start_time);
    DocoptArgs args = docopt(argc, argv, /* help */ 1, /* version */ "0.1");
    edge_trace_init(args.color_log);
    pthread_t *timer_thread = NULL;
    void *result = NULL;
    /* Setup signal handler to catch SIGINT for shutdown */
    if (!setup_signals()) {
        tr_err("Failed to setup signals.\n");
        return 1;
    }

    if (!args.protocol_translator_name) {
        tr_err("The --protocol-translator-name parameter is mandatory. Please see --help\n");
        return 1;
    }

    stress_tester_t *tester = &g_tester;
    if (!create_tester(tester, &args)) {
        return 1;
    }

    if (tester->test_duration_seconds > 0) {
        timer_thread = calloc(1, sizeof(pthread_t));
        pthread_create(timer_thread, NULL, &timer_thread_func, tester);
    }

    // Note: to avoid a leak, we should join the created thread from the same thread it was created from.
    wait_for_test_threads(tester);
    wait_for_protocol_translator_api_threads(tester);
    pt_client_final_cleanup();
    if (g_tester.shutdown_thread) {
        pthread_join(*g_tester.shutdown_thread, &result);
        free(g_tester.shutdown_thread);
        g_tester.shutdown_thread = NULL;
    }
    if (timer_thread) {
        pthread_join(*timer_thread, &result);
        free(timer_thread);
        timer_thread = NULL;
    }
    tr_err("Destroying trace system");
    edge_trace_destroy();
}

/**
 * @}
 * close EDGE_PT_STRESS_TESTER Doxygen group definition
 */
