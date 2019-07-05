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

#include <signal.h>
#include <errno.h>

#include "mosquitto.h"
#include "ns_list.h"
#include "common/constants.h"
#include "pt-client-2/pt_api.h"
#include "pt-client-2/pt_certificate_api.h"
#include "pt-client-2/pt_crypto_api.h"
#include "mbed-trace/mbed_trace.h"
#include "examples-common-2/client_config.h"
#include "mqttpt_example_clip.h"
#include "common/edge_trace.h"
#include "common/apr_base64.h"

#define TRACE_GROUP "mqtt-example"

#include "jansson.h"

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

connection_id_t g_connection_id = PT_API_CONNECTION_ID_INVALID;
pt_api_mutex_t *g_devices_mutex = NULL;
sem_t mosquitto_stop;
struct mosquitto *mosq = NULL;

void mqttpt_connection_ready_handler(connection_id_t connection_id, const char *name, void *ctx);
bool setup_signals(void);

void mqttpt_protocol_translator_registration_success_handler(void *ctx);
void mqttpt_protocol_translator_registration_failure_handler(void *ctx);

void mqttpt_shutdown_handler(connection_id_t connection_id, void *ctx);
sem_t mqttpt_translator_started;
pthread_t mqttpt_thread;
#define MQTTPT_DEFAULT_LIFETIME 10000

typedef enum {
    SENSOR_TEMPERATURE,
    SENSOR_HUMIDITY
} sensor_type_e;

/*
 * Bookkeeping to keep track of devices registered or "seen"
 */

typedef struct {
    ns_list_link_t link;
    const char* deveui;
} mqttpt_device_t;

typedef struct {
    char *request_id;
    char *certificate;
} pt_api_request_userdata_t;

/**
 * \brief Structure to pass the protocol translator initialization
 * data to the protocol translator API.
 */
typedef struct protocol_translator_api_start_ctx {
    const char *socket_path;
    pt_client_t *client;
} protocol_translator_api_start_ctx_t;

protocol_translator_api_start_ctx_t *global_pt_ctx;

typedef NS_LIST_HEAD(mqttpt_device_t, link) mqttpt_device_list_t;
bool protocol_translator_shutdown_handler_called = false;
mqttpt_device_list_t *mqttpt_devices;
void mqttpt_add_device(const char* deveui) {
    if (deveui == NULL) {
        return;
    }
    mqttpt_device_t *device = (mqttpt_device_t *) calloc(1, sizeof(mqttpt_device_t));
    if (device == NULL) {
        return;
    }
    device->deveui = strdup(deveui);
    tr_info("Adding device to list");
    ns_list_add_to_end(mqttpt_devices, device);
}

int mqttpt_device_exists(const char* deveui) {
    tr_info("Checking device '%s' exists", deveui);
    ns_list_foreach(mqttpt_device_t, device, mqttpt_devices) {
        tr_info("Checking %s", device->deveui);
        if (strcmp(deveui, device->deveui) == 0) {
            return 1;
        }
    }
    return 0;
}

pt_api_request_userdata_t *create_pt_api_request_userdata(const char *request_id)
{
    pt_api_request_userdata_t *userdata = calloc(1, sizeof(pt_api_request_userdata_t));
    char *request_id_copy = strdup(request_id);
    if (userdata == NULL || request_id_copy == NULL) {
        free(userdata);
        free(request_id_copy);
        return NULL;
    }

    userdata->request_id = request_id_copy;
    return userdata;
}

/*
 * Protocol translator's internal eventloop needs a thread to run in
 */
void* mqttpt_translator_thread_routine(void *ctx)
{
    const protocol_translator_api_start_ctx_t *pt_start_ctx = (protocol_translator_api_start_ctx_t*) global_pt_ctx;
    pt_client_start(pt_start_ctx->client,
                    mqttpt_protocol_translator_registration_success_handler,
                    mqttpt_protocol_translator_registration_failure_handler,
                    "testing-mqtt",
                    ctx);
    return NULL;
}

void mqttpt_start_translator(struct mosquitto *mosq)
{
    int started;
    sem_getvalue(&mqttpt_translator_started, &started);
    if (started == 0) {
        pthread_create(&mqttpt_thread, NULL, &mqttpt_translator_thread_routine, (void *) mosq);
        sem_post(&mqttpt_translator_started);
    }
}

static void publish_to_mqtt(const char *topic, json_t *json_message)
{
    char *result_string = json_dumps(json_message, JSON_COMPACT);
    if (result_string == NULL) {
        tr_err("Could not create mqtt message string.");
        return;
    }
    tr_info("Publishing result: %s", result_string);
    mosquitto_publish(mosq, NULL, topic, strlen(result_string), result_string, 0, 0);
    free(result_string);
}

/* Success message format:
 * {
 * "request_id": "id of the request that was succesful",
 * "value": "response value for the request"
 * }
 */
static void construct_and_send_success(const char* request_id, const char *value)
{
    json_t *result_json = json_object();
    if (result_json == NULL) {
        tr_err("Could not allocate JSON for success response message. (request: %s)", request_id);
        return;
    }
    json_object_set_new(result_json, "request_id", json_string(request_id));
    json_object_set_new(result_json, "value", json_string(value));

    publish_to_mqtt("MQTTPt/RequestResponse", result_json);

    json_decref(result_json);
}

/* Success message format:
 * {
 * "request_id": "id of the request that failed",
 * "error": "error code explaining the failure"
 * }
 */
static void construct_and_send_failure(const char* request_id, const char *error)
{
    json_t *result_json = json_object();
    if (result_json == NULL) {
        tr_err("Could not allocate JSON for failure response message. (request: %s)", request_id);
        return;
    }
    json_object_set_new(result_json, "request_id", json_string(request_id));
    json_object_set_new(result_json, "error", json_string(error));

    publish_to_mqtt("MQTTPt/RequestResponse", result_json);

    json_decref(result_json);
}

/* Certificate renewal notification message format:
 * {
 * "certificate": "certificate the the notification affects",
 * "message": "message informing the status of the renewal, can be either success or failure"
 * }
 */
static void construct_and_send_certificate_renewal_notification(const char* certificate, const char *message)
{
    json_t *result_json = json_object();
    if (result_json == NULL) {
        tr_err("Could not allocate JSON for certificate renewal notification message. (cert: %s)", certificate);
        return;
    }
    json_object_set_new(result_json, "certificate", json_string(certificate));
    json_object_set_new(result_json, "message", json_string(message));

    publish_to_mqtt("MQTTPt/CertificateRenewal", result_json);

    json_decref(result_json);
}

/* Certificate renewal notification message format:
 * {
 * "message": "message informing the status of the registration of the protocol translator"
 * "device": "if message applies to specific device, this field is present and contains the device identifier"
 * }
 */
static void construct_and_send_device_notification(const char *message, const char *dev_ui)
{
    json_t *result_json = json_object();
    if (result_json == NULL) {
        tr_err("Could not allocate JSON for translator registration notification message.");
        return;
    }

    json_object_set_new(result_json, "message", json_string(message));
    if (dev_ui) {
        json_object_set_new(result_json, "device", json_string(dev_ui));
    }

    publish_to_mqtt("MQTTPt/DeviceRegistration", result_json);

    json_decref(result_json);
}

/* Certificate renewal notification message format:
 * {
 * "message": "message informing the status of the registration of the protocol translator"
 * }
 */
static void construct_and_send_translator_registration_notification(const char *message)
{
    json_t *result_json = json_object();
    if (result_json == NULL) {
        tr_err("Could not allocate JSON for translator registration notification message.");
        return;
    }

    json_object_set_new(result_json, "message", json_string(message));

    publish_to_mqtt("MQTTPt/TranslatorRegistration", result_json);

    json_decref(result_json);
}

/* Device certificate renewal notification message format:
 * {
 * "certificate_name": "Name of the certificate that was enrolled",
 * "certificate_chain": "The enrolled certificate chain as an array"
 * }
 */
static void construct_and_send_device_certificate_renewal_notification(const char *request_id, const char *cert_name, const struct cert_chain_context_s *cert_chain)
{
    json_t *result_json = json_object();
    if (result_json == NULL) {
        tr_err("Could not allocate JSON for device certificate renewal notification message. (cert: %s)", cert_name);
        return;
    }
    json_object_set_new(result_json, "request_id", json_string(request_id));
    json_object_set_new(result_json, "certificate_name", json_string(cert_name));

    // Create array of base64 encoded certs from cert chain
    bool success = true;
    json_t *cert_array = json_array();
    if (cert_chain != NULL) {
        struct cert_context_s *cur_cert = cert_chain->certs;
        char *cur_cert_data = NULL;
        for (int i = 0; i < cert_chain->chain_length && cur_cert != NULL; i++) {
            cur_cert_data = (char *) calloc(1, apr_base64_encode_len(cur_cert->cert_length));
            if (cur_cert_data == NULL) {
                success = false;
                break;
            }
            apr_base64_encode_binary(cur_cert_data, cur_cert->cert, cur_cert->cert_length);
            json_array_append_new(cert_array, json_string(cur_cert_data));
            free(cur_cert_data);
        }
    }
    if (success == false) {
        json_array_clear(cert_array);
    }
    json_object_set_new(result_json, "certificate_chain", cert_array);

    publish_to_mqtt("MQTTPt/DeviceCertificateRenewal", result_json);
    json_decref(result_json);
}

/* Device certificate renewal request message format:
 * {
 * "device": "Id of device whose certificate should be renewed",
 * "certificate_name": "Name of the certificate that was enrolled"
 * }
 */
static void construct_and_send_device_certificate_renewal_request(const char *device_name, const char *cert_name){
    json_t *result_json = json_object();
    if (result_json == NULL) {
        tr_err("Could not allocate JSON for certificate renewal request message. (device: %s, cert: %s)", device_name, cert_name);
        return;
    }
    json_object_set_new(result_json, "device", json_string(device_name));
    json_object_set_new(result_json, "certificate_name", json_string(cert_name));

    publish_to_mqtt("MQTTPt/DeviceCertificateRenewalRequest", result_json);
    json_decref(result_json);
}

/*
 * Callback handlers for PT operations
 */

void mqttpt_device_register_success_handler(const connection_id_t connection_id, const char *device_id, void *ctx)
{
    if (ctx) {
        tr_info("A device register finished successfully.");
        tr_info("deveui %s", (char*)ctx);
        construct_and_send_device_notification("successful_registration", (const char*)ctx);
        mqttpt_add_device((const char*)ctx);
    }
    free(ctx);
}

void mqttpt_device_register_failure_handler(const connection_id_t connection_id, const char *device_id, void *ctx)
{
    tr_info("A device register failed.");
    construct_and_send_device_notification("failed_registration", (const char*)ctx);
    free(ctx);
}

void mqttpt_devices_unregister_success_handler(connection_id_t connection_id, void *userdata)
{
    tr_info("Devices unregistration success.");
    (void) connection_id;
    (void) userdata;
    construct_and_send_device_notification("successful_unregistration", NULL);
    pt_client_shutdown(global_pt_ctx->client);
}

void mqttpt_devices_unregister_failure_handler(connection_id_t connection_id, void *userdata)
{
    tr_err("Devices unregistration failed.");
    (void) connection_id;
    (void) userdata;
    construct_and_send_device_notification("failed_unregistration", NULL);
    pt_client_shutdown(global_pt_ctx->client);
}

void mqttpt_update_object_structure_success_handler(connection_id_t connection_id, const char *device_id, void *ctx)
{
    (void) connection_id;
    tr_info("Object structure update finished successfully.");
    free(ctx);
}

void mqttpt_update_object_structure_failure_handler(connection_id_t connection_id, const char *device_id, void *ctx)
{
    (void) connection_id;
    tr_info("Object structure update failed.");
    free(ctx);
}

void mqttpt_protocol_translator_registration_success_handler(void *ctx)
{
    (void) ctx;
    sem_post(&mqttpt_translator_started);
    tr_info("MQTT translator registered successfully.");
    construct_and_send_translator_registration_notification("successful_registration");
}

void mqttpt_protocol_translator_registration_failure_handler(void *ctx)
{
    (void) ctx;
    tr_info("MQTT translator registration failed.");
    sem_post(&mosquitto_stop);
    construct_and_send_translator_registration_notification("failed_registration");
}

pt_status_t mqtt_minmax_reset_callback(const connection_id_t connection_id,
                                       const char *device_id,
                                       const uint16_t object_id,
                                       const uint16_t instance_id,
                                       const uint16_t resource_id,
                                       const uint8_t operation,
                                       const uint8_t *value,
                                       const uint32_t value_size,
                                       void *userdata)
{
    tr_debug("Min / Max resource reset callback for (%s/%d/%d/%d)",
             device_id,
             object_id,
             instance_id,
             resource_id);
    return PT_STATUS_SUCCESS;
}

pt_status_t mqtt_write_callback(const connection_id_t connection_id,
                                       const char *device_id,
                                       const uint16_t object_id,
                                       const uint16_t instance_id,
                                       const uint16_t resource_id,
                                       const uint8_t operation,
                                       const uint8_t *value,
                                       const uint32_t value_size,
                                       void *userdata)
{
    tr_debug("Write resource for (%s/%d/%d/%d)",
             device_id,
             object_id,
             instance_id,
             resource_id);
    return PT_STATUS_SUCCESS;
}

pt_status_t mqtt_example_callback(const connection_id_t connection_id,
                                       const char *device_id,
                                       const uint16_t object_id,
                                       const uint16_t instance_id,
                                       const uint16_t resource_id,
                                       const uint8_t operation,
                                       const uint8_t *value,
                                       const uint32_t value_size,
                                       void *userdata)
{
    tr_debug("Example callback for (%s/%d/%d/%d)",
             device_id,
             object_id,
             instance_id,
             resource_id);
    return PT_STATUS_SUCCESS;
}

void mqttpt_connection_ready_handler(connection_id_t connection_id, const char *name, void *ctx)
{
    tr_info("mqttpt_connection_ready_handler for connection with id %d name '%s'", connection_id, name);
    g_connection_id = connection_id;
}

void mqttpt_shutdown_handler(connection_id_t connection_id, void *userdata)
{
    (void) connection_id;
    (void) userdata;
    tr_info("Shutting down the mqttpt example");
}

void mqttpt_disconnected_handler(connection_id_t connection_id, void *userdata)
{
    (void) connection_id;
    tr_info("Protocol translator disconnected from the Edge Core.");
}

void mqtt_certificate_renewal_notification_handler(const connection_id_t connection_id,
                                                   const char *name,
                                                   int32_t initiator,
                                                   int32_t status,
                                                   const char *description,
                                                   void *userdata)
{
    (void) connection_id;
    tr_info("Certificate renewal notification from the Edge Core: name: '%s' initiator: %d status: %d description: "
            "'%s'",
            name,
            initiator,
            status,
            description);
    construct_and_send_certificate_renewal_notification(name, description);
}

pt_status_t mqtt_device_certificate_renewal_request_handler(const connection_id_t connection_id,
                                                            const char *device_id,
                                                            const char *name,
                                                            void *userdata)
{
    tr_info("Certificate renewal request for device: %s, certificate: %s", device_id, name);
    construct_and_send_device_certificate_renewal_request(device_id, name);
    return PT_STATUS_SUCCESS;
}

/*
 * Create the lwm2m structure for a "generic" sensor object. Same resources can be used
 * to represent temperature and humidity sensors by just changing the object id
 * temperature sensor id = 3303 (http://www.openmobilealliance.org/tech/profiles/lwm2m/3303.xml)
 * humidity sensor id = 3304 (http://www.openmobilealliance.org/tech/profiles/lwm2m/3304.xml)
 */
bool mqttpt_create_sensor_object(connection_id_t connection_id, const char *deveui, int object_id, int object_instance, const char* value)
{

    if (value == NULL || deveui == NULL) {
        return false;
    }

    // Resource value buffer ownership is transferred so we need to make copies of the const buffers passed in
    char *value_buf = strdup(value);

    if (value_buf == NULL) {
        free(value_buf);
        return false;
    }

    pt_status_t status = pt_device_add_resource(connection_id,
                                                deveui,
                                                object_id,
                                                object_instance,
                                                5700,
                                                LWM2M_OPAQUE,
                                                (uint8_t *) value_buf,
                                                strlen(value_buf),
                                                free);
    status = pt_device_add_resource_with_callback(connection_id,
                                                  deveui,
                                                  object_id,
                                                  object_instance,
                                                  5605,
                                                  LWM2M_OPAQUE,
                                                  OPERATION_EXECUTE,
                                                  NULL,
                                                  0,
                                                  free,
                                                  mqtt_minmax_reset_callback);
    if (status != PT_STATUS_SUCCESS) {
        tr_err("Resource creation failed, status %d\n", status);
        return false;
    }

    return true;
}

/* Create a structure using the given LWM2M object identifiers and default callbacks */
bool mqttpt_create_object(connection_id_t connection_id, const char *deveui, int object_id, int object_instance, int resource_id, const char* value, int operations)
{
    if (value == NULL || deveui == NULL) {
        return false;
    }

    // Resource value buffer ownership is transferred so we need to make copies of the const buffers passed in
    char *value_buf = strdup(value);

    if (value_buf == NULL) {
        free(value_buf);
        return false;
    }

    pt_status_t status = PT_STATUS_ERROR;
    //Read Only resource
    if (operations == 1) {
        status = pt_device_add_resource(connection_id,
                                                    deveui,
                                                    object_id,
                                                    object_instance,
                                                    resource_id,
                                                    LWM2M_OPAQUE,
                                                    (uint8_t *) value_buf,
                                                    strlen(value_buf),
                                                    free);
    }
    //Read / Write resource
    else if (operations == 3) {
        status = pt_device_add_resource_with_callback(connection_id,
                                                    deveui,
                                                    object_id,
                                                    object_instance,
                                                    resource_id,
                                                    LWM2M_OPAQUE,
                                                    OPERATION_READ | OPERATION_WRITE,
                                                    (uint8_t *) value_buf,
                                                    strlen(value_buf),
                                                    free,
                                                    mqtt_write_callback);
    }
    else {
        status = pt_device_add_resource_with_callback(connection_id,
                                                      deveui,
                                                      object_id,
                                                      object_instance,
                                                      resource_id,
                                                      LWM2M_OPAQUE,
                                                      operations,
                                                      (uint8_t *) value_buf,
                                                      strlen(value_buf),
                                                      free,
                                                      mqtt_example_callback);
    }

    if (status != PT_STATUS_SUCCESS) {
        tr_err("Resource creation failed, status %d\n", status);
        return false;
    }

    return true;
}

void certificate_renew_success_handler(const connection_id_t connection_id, void *userdata)
{
    tr_info("certificate_renew_success_handler");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*)userdata;

    construct_and_send_success(userdata_struct->request_id, userdata_struct->certificate);

    free(userdata_struct->request_id);
    free(userdata_struct->certificate);
    free(userdata_struct);
}

void certificate_renew_failure_handler(const connection_id_t connection_id, void *userdata)
{
    tr_info("certificate_renew_failure_handler");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*)userdata;

    char error_string[256];
    snprintf(error_string, sizeof(error_string), "Certificate renew failed for certificate '%s'", userdata_struct->certificate);
    construct_and_send_failure(userdata_struct->request_id, error_string);

    free(userdata_struct->request_id);
    free(userdata_struct->certificate);
    free(userdata_struct);
}

void get_item_success_handler(const connection_id_t connection_id, const uint8_t *data, const size_t size, void *userdata)
{
    tr_info("get_item_success_handler");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*)userdata;

    char *encoded_msg = calloc(1, sizeof(char) * apr_base64_encode_len(size));
    if (encoded_msg == NULL) {
        tr_err("Could not allocate char pointer for returning the item. (request: %s)", userdata_struct->request_id);
        return;
    }
    apr_base64_encode_binary(encoded_msg, (const unsigned char *)data, size);
    construct_and_send_success(userdata_struct->request_id, encoded_msg);

    free(encoded_msg);
    free(userdata_struct->request_id);
    free(userdata_struct);
}

void get_item_failure_handler(const connection_id_t connection_id, void *userdata)
{
    tr_info("get_item_failure_handler");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*)userdata;

    construct_and_send_failure(userdata_struct->request_id, "Getting item failed");

    free(userdata_struct->request_id);
    free(userdata_struct);
}

void crypto_success_handler(const connection_id_t connection_id, const uint8_t *data, const size_t size, void *userdata)
{
    tr_info("crypto_get_success_handler");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*)userdata;

    if (data) {
        char *encoded_msg = calloc(1, sizeof(char) * apr_base64_encode_len(size));
        if (encoded_msg == NULL) {
            tr_err("Could not allocate char pointer for crypto_success_handler value. (request: %s)", userdata_struct->request_id);
            return;
        }
        apr_base64_encode_binary(encoded_msg, (const unsigned char *)data, size);
        construct_and_send_success(userdata_struct->request_id, encoded_msg);
        free(encoded_msg);
    }
    else {
        construct_and_send_success(userdata_struct->request_id, "ok");
    }
    free(userdata_struct->request_id);
    free(userdata_struct);
}

void crypto_failure_handler(const connection_id_t connection_id, int error_code, void *userdata)
{
    tr_info("crypto_failure_handler");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*)userdata;

    char error_string[12];
    sprintf(error_string, "%d", error_code);
    construct_and_send_failure(userdata_struct->request_id, error_string);

    free(userdata_struct->request_id);
    free(userdata_struct);
}

static void certificates_set_success_handler(const connection_id_t connection_id, void *userdata)
{
    tr_info("certificates_set_success_handler");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*) userdata;

    construct_and_send_success(userdata_struct->request_id, "ok");
    free(userdata_struct->request_id);
    free(userdata_struct);
}

static void certificates_set_failure_handler(const connection_id_t connection_id, void *userdata)
{
    tr_err("Certificates setting to Edge failed!");
    pt_api_request_userdata_t *userdata_struct = (pt_api_request_userdata_t*) userdata;

    construct_and_send_failure(userdata_struct->request_id, "Could not set certificate list!");
    free(userdata_struct->request_id);
    free(userdata_struct);
}

static pt_status_t set_certificates_list(const char *request_id, json_t *params)
{
    tr_info("set_certificates_list");
    pt_status_t status = PT_STATUS_ERROR;
    json_t *json_certificates = json_object_get(params, "certificates");
    json_t *json_certificate = NULL;

    pt_certificate_list_t *list = pt_certificate_list_create();
    int32_t index;
    json_array_foreach(json_certificates, index, json_certificate)
    {
        const char *certificate = json_string_value(json_certificate);
        if (certificate) {
            tr_info("  adding certificate to list: %s", certificate);
            pt_certificate_list_add(list, certificate);
        } else {
            tr_err("Invalid json array entry!");
        }
    }

    pt_api_request_userdata_t *userdata = calloc(1, sizeof(pt_api_request_userdata_t));
    if (userdata == NULL) {
        tr_err("Could not allocate userdata for the certificate_renewal_list_set request. (request: %s)", request_id);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_set_certificates;
    }
    userdata->request_id = strdup(request_id);
    if (userdata->request_id == NULL) {
        tr_err("Could not allocate request_id for userdata. (request: %s)", request_id);
        free(userdata->request_id);
        free(userdata);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_set_certificates;
    }

    status = pt_certificate_renewal_list_set(g_connection_id,
                                             list,
                                             certificates_set_success_handler,
                                             certificates_set_failure_handler,
                                             userdata);
exit_set_certificates:
    pt_certificate_list_destroy(list);
    return status;
}

static pt_status_t renew_certificate(const char *request_id, json_t *params)
{
    tr_info("renew_certificate");
    pt_status_t status = PT_STATUS_ERROR;
    json_t *json_certificate = json_object_get(params, "certificate");
    const char *certificate = json_string_value(json_certificate);

    pt_api_request_userdata_t *userdata = calloc(1, sizeof(pt_api_request_userdata_t));
    if (userdata == NULL) {
        tr_err("Could not allocate userdata for the renew_certificate request. (request: %s)", request_id);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_renew_certificate;
    }
    userdata->certificate = strdup(certificate);
    userdata->request_id = strdup(request_id);
    if (userdata->request_id == NULL || userdata->certificate == NULL) {
        tr_err("Could not allocate request_id or certificate name for userdata. (request: %s)", request_id);
        free(userdata->request_id);
        free(userdata->certificate);
        free(userdata);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_renew_certificate;
    }

    status = pt_certificate_renew(g_connection_id,
                                  userdata->certificate,
                                  certificate_renew_success_handler,
                                  certificate_renew_failure_handler,
                                  userdata);
exit_renew_certificate:
    return status;
}

static pt_status_t get_certificate(const char *request_id, json_t *params)
{
    tr_info("get_certificate");

    const char *certificate = json_string_value(json_object_get(params, "certificate"));
    if (certificate) {
        pt_api_request_userdata_t *userdata = create_pt_api_request_userdata(request_id);
        if (userdata == NULL) {
            tr_err("Could not allocate userdata for the get_certificate request. (request: %s)", request_id);
            return PT_STATUS_ALLOCATION_FAIL;
        }

        return pt_crypto_get_certificate(g_connection_id,
                                         certificate,
                                         get_item_success_handler,
                                         get_item_failure_handler,
                                         userdata);
    }
    else {
        tr_err("Invalid json entry!");
        return PT_STATUS_INVALID_PARAMETERS;
    }
}

static pt_status_t get_public_key(const char *request_id, json_t *params)
{
    tr_info("get_public_key");

    const char *public_key = json_string_value(json_object_get(params, "key"));
    if (public_key) {
        pt_api_request_userdata_t *userdata = create_pt_api_request_userdata(request_id);
        if (userdata == NULL) {
            tr_err("Could not allocate userdata for the get_public_key request. (request: %s)", request_id);
            return PT_STATUS_ALLOCATION_FAIL;
        }

        return pt_crypto_get_public_key(g_connection_id,
                                        public_key,
                                        get_item_success_handler,
                                        get_item_failure_handler,
                                        userdata);
    }
    else {
        tr_err("Invalid json entry!");
        return PT_STATUS_INVALID_PARAMETERS;
    }
}

static pt_status_t generate_random(const char *request_id, json_t *params)
{
    tr_info("generate_random");

    size_t size = json_integer_value(json_object_get(params, "size"));
    if (size) {
        pt_api_request_userdata_t *userdata = create_pt_api_request_userdata(request_id);
        if (userdata == NULL) {
            tr_err("Could not allocate userdata for the generate_random request. (request: %s)", request_id);
            return PT_STATUS_ALLOCATION_FAIL;
        }

        return pt_crypto_generate_random(g_connection_id,
                                         size,
                                         crypto_success_handler,
                                         crypto_failure_handler,
                                         userdata);
    }
    else {
        tr_err("Invalid json entry!");
        return PT_STATUS_INVALID_PARAMETERS;
    }
}

static pt_status_t asymmetric_sign(const char *request_id, json_t *params)
{
    tr_info("asymmetric_sign");

    pt_status_t status = PT_STATUS_ERROR;
    char *decoded_hash_digest = NULL;

    const char *private_key_name = json_string_value(json_object_get(params, "private_key_name"));
    const char *hash_digest = json_string_value(json_object_get(params, "hash_digest"));
    decoded_hash_digest = calloc(1, sizeof(char) * apr_base64_decode_len(hash_digest));
    if (decoded_hash_digest == NULL) {
        tr_err("Could not allocate char pointer for decoding hash digest. (request: %s)", request_id);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_asymmetric_sign;
    }
    size_t decoded_hash_digest_size = apr_base64_decode_binary((unsigned char*) decoded_hash_digest, hash_digest);

    if (private_key_name && decoded_hash_digest) {
        pt_api_request_userdata_t *userdata = create_pt_api_request_userdata(request_id);
        if (userdata == NULL) {
            tr_err("Could not allocate userdata for the asymmetric_sign request. (request: %s)", request_id);
            status = PT_STATUS_ALLOCATION_FAIL;
            goto exit_asymmetric_sign;
        }

        status = pt_crypto_asymmetric_sign(g_connection_id,
                                           private_key_name,
                                           decoded_hash_digest,
                                           decoded_hash_digest_size,
                                           crypto_success_handler,
                                           crypto_failure_handler,
                                           userdata);
    }
    else {
        tr_err("Invalid json entry!");
        status = PT_STATUS_INVALID_PARAMETERS;
    }
exit_asymmetric_sign:
    free(decoded_hash_digest);
    return status;
}

static pt_status_t asymmetric_verify(const char *request_id, json_t *params)
{
    tr_info("asymmetric_verify");

    pt_status_t status = PT_STATUS_ERROR;
    char *decoded_hash_digest = NULL;
    char *decoded_signature = NULL;

    const char *public_key_name = json_string_value(json_object_get(params, "public_key_name"));

    const char *hash_digest = json_string_value(json_object_get(params, "hash_digest"));
    decoded_hash_digest = calloc(1, sizeof(char) * apr_base64_decode_len(hash_digest));
    if (decoded_hash_digest == NULL) {
        tr_err("Could not allocate char pointer for decoding hash digest. (request: %s)", request_id);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_asymmetric_verify;
    }
    size_t decoded_hash_digest_size = apr_base64_decode_binary((unsigned char*) decoded_hash_digest, hash_digest);

    const char *signature = json_string_value(json_object_get(params, "signature"));
    decoded_signature = calloc(1, sizeof(char) * apr_base64_decode_len(signature));
    if (decoded_hash_digest == NULL) {
        tr_err("Could not allocate char pointer for decoding signature. (request: %s)", request_id);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_asymmetric_verify;
    }
    size_t decoded_signature_size = apr_base64_decode_binary((unsigned char*) decoded_signature, signature);

    if (public_key_name && decoded_hash_digest && decoded_signature) {
        pt_api_request_userdata_t *userdata = create_pt_api_request_userdata(request_id);
        if (userdata == NULL) {
            tr_err("Could not allocate userdata for the asymmetric_verify request. (request: %s)", request_id);
            status = PT_STATUS_ALLOCATION_FAIL;
            goto exit_asymmetric_verify;
        }

        status = pt_crypto_asymmetric_verify(g_connection_id,
                                             public_key_name,
                                             decoded_hash_digest,
                                             decoded_hash_digest_size,
                                             decoded_signature,
                                             decoded_signature_size,
                                             crypto_success_handler,
                                             crypto_failure_handler,
                                             userdata);
    }
    else {
        tr_err("Invalid json entry!");
        status = PT_STATUS_INVALID_PARAMETERS;
    }
exit_asymmetric_verify:
    free(decoded_hash_digest);
    free(decoded_signature);

    return status;
}

static pt_status_t ecdh_key_agreement(const char *request_id, json_t *json_params)
{
    tr_info("ecdh_key_agreement");

    pt_status_t status = PT_STATUS_ERROR;
    char *decoded_peer_public_key = NULL;

    const char *private_key_name = json_string_value(json_object_get(json_params, "private_key_name"));

    const char *peer_public_key = json_string_value(json_object_get(json_params, "peer_public_key"));
    decoded_peer_public_key = calloc(1, sizeof(char) * apr_base64_decode_len(peer_public_key));
    if (decoded_peer_public_key == NULL) {
        tr_err("Could not allocate char pointer for decoding peer public key. (request: %s)", request_id);
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_ecdh_key_agreement;
    }
    size_t decoded_peer_public_key_size = apr_base64_decode_binary((unsigned char*) decoded_peer_public_key, peer_public_key);

    if (private_key_name && decoded_peer_public_key) {
        pt_api_request_userdata_t *userdata = create_pt_api_request_userdata(request_id);
        if (userdata == NULL) {
            tr_err("Could not allocate userdata for the ecdh_key_agreement request. (request: %s)", request_id);
            status = PT_STATUS_ALLOCATION_FAIL;
            goto exit_ecdh_key_agreement;
        }

        status = pt_crypto_ecdh_key_agreement(g_connection_id,
                                              private_key_name,
                                              decoded_peer_public_key,
                                              decoded_peer_public_key_size,
                                              crypto_success_handler,
                                              crypto_failure_handler,
                                              userdata);
    }
    else {
        tr_err("Invalid json entry!");
        status = PT_STATUS_INVALID_PARAMETERS;
    }
exit_ecdh_key_agreement:
    free(decoded_peer_public_key);

    return status;
}

static void device_certificate_renew_success_handler(const connection_id_t connection_id,
                                                     const char *device_id,
                                                     const char *name,
                                                     int32_t status,
                                                     struct cert_chain_context_s *cert_chain,
                                                     void *userdata)
{
    pt_status_t pt_status = pt_device_certificate_renew_request_finish(connection_id, device_id, CE_STATUS_SUCCESS);
    tr_info("Request finish status: %d", pt_status);
    construct_and_send_device_certificate_renewal_notification((const char *)userdata, name, cert_chain);
    free((char *) userdata);
    pt_free_certificate_chain_context(cert_chain);
}

static void device_certificate_renew_failure_handler(const connection_id_t connection_id,
                                                     const char *device_id,
                                                     const char *name,
                                                     int32_t status,
                                                     struct cert_chain_context_s *cert_chain,
                                                     void *userdata)
{
    pt_status_t pt_status = pt_device_certificate_renew_request_finish(connection_id, device_id, CE_STATUS_ERROR);
    tr_info("Request finish status: %d", pt_status);
    char error[64];
    snprintf(error, 64, "Device certificate renew failed (error %d)", status);
    construct_and_send_failure((const char *) userdata, error);
    free((char *) userdata);
}

static pt_status_t device_renew_certificate(const char *request_id, json_t *json_params)
{
    tr_info("device_renew_certificate");

    pt_status_t status = PT_STATUS_SUCCESS;
    char *decoded_csr = NULL;

    const char *device_name = json_string_value(json_object_get(json_params, "device_name"));
    const char *cert_name = json_string_value(json_object_get(json_params, "certificate_name"));
    const char *csr = json_string_value(json_object_get(json_params, "csr"));

    if (device_name == NULL || cert_name == NULL || csr == NULL) {
        tr_err("Invalid json entry!");
        status = PT_STATUS_INVALID_PARAMETERS;
        goto exit_device_renew_certificate;
    }

    decoded_csr = calloc(1, apr_base64_decode_len(csr));
    char *req_id = strdup(request_id);
    if (decoded_csr == NULL || req_id == NULL) {
        status = PT_STATUS_ALLOCATION_FAIL;
        goto exit_device_renew_certificate;
    }

    size_t decoded_csr_size = apr_base64_decode_binary((unsigned char*) decoded_csr, csr);

    status = pt_device_certificate_renew(g_connection_id,
                                         device_name,
                                         cert_name,
                                         decoded_csr,
                                         decoded_csr_size,
                                         device_certificate_renew_success_handler,
                                         device_certificate_renew_failure_handler,
                                         req_id);

exit_device_renew_certificate:
    free(decoded_csr);
    if (status != PT_STATUS_SUCCESS) {
        free(req_id);
    }

    return status;
}

/*
 * Functions which translate different types of MQTT messages we receive through mqtt
 */
void mqttpt_translate_gw_status_message(struct mosquitto *mosq, const char *payload, const int payload_len)
{
    json_error_t error;
    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        tr_err("Could not parse node value json.");
        return;
    }

    json_t *method = json_object_get(json, "method");
    json_t *params = json_object_get(json, "params");
    pt_status_t status = PT_STATUS_ERROR;
    const char *request_id = json_string_value(json_object_get(json, "request_id"));
    if (method && request_id) {
        //Requests with parameters
        if (params && json_is_object(params)) {
            if (0 == strcmp("renew_certificate", json_string_value(method))) {
                status = renew_certificate(request_id, params);
            }
            else if (0 == strcmp("set_certificates_list", json_string_value(method))) {
                status = set_certificates_list(request_id, params);
            }
            else if (0 == strcmp("get_certificate", json_string_value(method))) {
                status = get_certificate(request_id, params);
            }
            else if (0 == strcmp("get_public_key", json_string_value(method))) {
                status = get_public_key(request_id, params);
            }
            else if (0 == strcmp("generate_random", json_string_value(method))) {
                status = generate_random(request_id, params);
            }
            else if (0 == strcmp("asymmetric_sign", json_string_value(method))) {
                status = asymmetric_sign(request_id, params);
            }
            else if (0 == strcmp("asymmetric_verify", json_string_value(method))) {
                status = asymmetric_verify(request_id, params);
            }
            else if (0 == strcmp("ecdh_key_agreement", json_string_value(method))) {
                status = ecdh_key_agreement(request_id, params);
            }
            else if (0 == strcmp("device_renew_certificate", json_string_value(method))) {
                status = device_renew_certificate(request_id, params);
            }
            else {
                tr_err("Unknown GW status method: '%s'", json_string_value(method));
                status = PT_STATUS_ERROR;
            }
        }
        //Requests without parameters
        else {
            if (0 == strcmp("start_pt", json_string_value(method))) {
                int started;
                sem_getvalue(&mqttpt_translator_started, &started);
                if (started == 0) {
                    mqttpt_start_translator(mosq);
                    status = PT_STATUS_SUCCESS;
                }
                else {
                    tr_err("Attempting to start a running protocol translator!");
                    status = PT_STATUS_ERROR;
                }
            }
            else {
                tr_err("Unknown GW status method: '%s'", json_string_value(method));
                status = PT_STATUS_ERROR;
            }
        }
    }
    else {
        if (!method) {
            tr_err("Method name missing");
        }
        if (!request_id) {
            tr_err("Request id missing");
        }
        status = PT_STATUS_INVALID_PARAMETERS;
    }

    if (status == PT_STATUS_SUCCESS) {
        construct_and_send_success(request_id, "handled");
    }
    else {
        char error_string[12];
        sprintf(error_string, "%d", status);
        construct_and_send_failure(request_id, error_string);
    }

    json_decref(json);
}

void mqttpt_translate_node_joined_message(const char* gweui, const char* payload, const int payload_len)
{
    json_error_t error;
    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        tr_err("Could not parse node value json.");
        return;
    }
}

/*
 * Capability message format:
{
"deveui":"76FE48FFFF000003",
"appeui":"00000000000000ab",
"port":1,
"cap":[
{"id":1,"name":"Temperature","type":"VALUE","decimalpoint":2,"plusmn":1,"unit":"
°C
","min":"
-
50","max":"50"},
{"id":2,"name":"Humidity","type":"VALUE","decimalpoint":2,"plusmn":0,"unit":"%","min":"0","max":"100
"}
],
"
remark":"Node
1“
}
 */
void mqttpt_translate_node_capability_message(const char* gweui, const char* deveui, const char* payload, const int payload_len)
{
    json_error_t error;
    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        tr_err("Could not parse node value json.");
        return;
    }
}

/*
 * Value message topic: MQTTGw/gweui/Node/deveui/Val
 * Value message format:
{
"deveui":"76FE48FFFF000003",
"appeui":"00000000000000ab",
"payload_raw":"EQwBAwAKlQICGTQDAtAYBAIgzA==",
"payload_field":[
["VALUE","Temperature","+27.9","°C","-50","50","3303","0","5700","5"],
["VALUE","Humidity","64.52","%","0","100","3304","0","5700","5"],
["VALUE","Set point","21.5","°C","-50","50","3308","0","5900","3"]
],
"metadata":{
"port":1,
"seqno":14854,
"frequency":923.300000,
"modulation":"MQTT",
"data_rate":"SF8BW500",
"code_rate":"4/5",
"gateway":[
{
"gweui":"080027ffff50f414",
"time":"2017-0309T10:24:29Z",
"rssi":-87,
"snr":6,
"rfchan":0,
"chan":0
}
]
},
"
remark":"Node
1“
}
 */
void mqttpt_translate_node_value_message(struct mosquitto *mosq,
                                         const char *gweui,
                                         char *deveui,
                                         const char *payload,
                                         const int payload_len)
{
    json_error_t error;
    tr_info("Translating value message");

    int started;
    sem_getvalue(&mqttpt_translator_started, &started);
    if (started == 0) {
        mqttpt_start_translator(mosq);
        tr_info("Translating value message, PT was not registered yet registering it now.");
        return;
    }

    if (gweui == NULL || deveui == NULL || payload == NULL) {
        tr_err("Translating value message, missing gweui, deveui or payload.");
        return;
    }

    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        tr_err("Translating value message, could not parse json.");
        return;
    }

    json_t *payload_field = json_object_get(json, "payload_field");
    if (payload_field == NULL) {
        tr_err("Translating value message, json missing payload_field.");
        json_decref(json);
        return;
    }

    // We store lwm2m representation of node values into pt_object_list_t
    // Create the device structure if device does not exist yet.
    if (!pt_device_exists(g_connection_id, deveui)) {
        pt_status_t status = pt_device_create(g_connection_id, deveui, MQTTPT_DEFAULT_LIFETIME, NONE);
        if (status != PT_STATUS_SUCCESS) {
            tr_err("Could not create a device %s error code: %d", deveui, (int32_t) status);
            return;
        }
    }

    // Loop through the payload array containing new values
    int payload_field_size = json_array_size(payload_field);
    tr_debug("PAYLOAD FIELD SIZE %d", payload_field_size);
    for (int i = 0; i < payload_field_size; i++) {
        // New values are also an array with format
        // ["VALUE", Sensor type, value, measurement unit description, minimum possible value, maximum possible value, id]
        json_t *value_array = json_array_get(payload_field, i);
        int value_array_size = json_array_size(value_array);
        if (value_array == NULL || value_array_size != 10) {
            tr_err("Translating value message, json has invalid payload array.");
            break;
        }

        const char* value = json_string_value(json_array_get(value_array, 2));

        int object_id = atoi(json_string_value(json_array_get(value_array, 6)));
        int object_instance = atoi(json_string_value(json_array_get(value_array, 7)));
        int resource_id = atoi(json_string_value(json_array_get(value_array, 8)));
        int resource_operations = atoi(json_string_value(json_array_get(value_array, 9)));

        if (!pt_device_resource_exists(g_connection_id, deveui, object_id, object_instance, resource_id)) {
            //If temperature or humidity, create sensor
            if (object_id == 3304 || object_id == 3303) {
                tr_info("Creating sensor.");
                mqttpt_create_sensor_object(g_connection_id, deveui, object_id, object_instance, value);
            }
            else {
                tr_info("Creating other");
                mqttpt_create_object(g_connection_id, deveui, object_id, object_instance, resource_id, value, resource_operations);
            }
        }

        pt_device_set_resource_value(g_connection_id,
                                     deveui,
                                     object_id,
                                     object_instance,
                                     resource_id,
                                     (uint8_t *) strdup(value),
                                     strlen(value),
                                     free);

    }

    if (payload_field_size > 0) {
        // We need to only send values if we actually got some
        char* deveui_ctx = strdup(deveui);
        // If device has been registered, then just write the new values
        if (mqttpt_device_exists(deveui)) {
            tr_info("Updating the changed object structure %s\n", deveui_ctx);
            pt_device_write_values(g_connection_id,
                                   deveui,
                                   mqttpt_update_object_structure_success_handler,
                                   mqttpt_update_object_structure_failure_handler,
                                   deveui_ctx);
        } else {
            // If device has not been registered yet, register it
            tr_info("Registering device %s\n", deveui_ctx);
            pt_device_register(g_connection_id,
                               deveui,
                               mqttpt_device_register_success_handler,
                               mqttpt_device_register_failure_handler,
                               deveui_ctx);
        }
    }

    json_decref(json);
}

/*
 * Value message topic: MQTTGw/gweui/Node/deveui/EdgeVal
 * Value message format:
{
"deveui":"76FE48FFFF000003",
"appeui":"00000000000000ab",
"payload_raw":"EQwBAwAKlQICGTQDAtAYBAIgzA==",
"payload_field": [
    {
        "payload_type": "VALUE",
        "objectid": "3303",
        "objectinstances": [{
            "objectinstance": "0",
            "resources": [{
                "resourceid": "5700",
                "resourcename": "Temperature",
                "value": "$temperature",
                "unit": "\u2103",
                "minvalue": "-50",
                "maxvalue": "50",
                "operations": "5"
            }]
        }]
    },
    {
        "payload_type": "VALUE",
        "objectid":"3304",
        "objectinstances": [{
            "objectinstance": "0",
            "resources": [{
                "resourceid": "5700",
                "type": "Humidity",
                "value": "$humidity",
                "unit": "%",
                "minvalue": "0",
                "maxvalue": "100",
                "operations": "5"
            }]
        }]

    },
    {
        "payload_type": "VALUE",
        "objectid": "3308",
        "objectinstances": [{
            "objectinstance": "0",
            "resources": [{
                "resourceid": "5900",
                "resoucename": "Set point",
                "value": "$set_point",
                "unit": "\u2103",
                "minvalue": "-50",
                "maxvalue": "50",
                "operations": "3"
            }]
        }]
    }
],
"metadata":{
"port":1,
"seqno":14854,
"frequency":923.300000,
"modulation":"MQTT",
"data_rate":"SF8BW500",
"code_rate":"4/5",
"gateway":[
{
"gweui":"080027ffff50f414",
"time":"2017-0309T10:24:29Z",
"rssi":-87,
"snr":6,
"rfchan":0,
"chan":0
}
]
},
"
remark":"Node
1“
}
 */

void mqttpt_translate_node_edge_value_message(struct mosquitto *mosq,
                                         const char *gweui,
                                         char *deveui,
                                         const char *payload,
                                         const int payload_len)
{
    json_error_t error;
    tr_info("Translating edge value message");

    int started;
    sem_getvalue(&mqttpt_translator_started, &started);
    if (started == 0) {
        mqttpt_start_translator(mosq);
        tr_info("Translating edge value message, PT was not registered yet registering it now.");
        return;
    }

    if (gweui == NULL || deveui == NULL || payload == NULL) {
        tr_err("Translating edge value message, missing gweui, deveui or payload.");
        return;
    }

    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        tr_err("Translating edge value message, could not parse json.");
        return;
    }

    json_t *payload_field = json_object_get(json, "payload_field");
    if (payload_field == NULL) {
        tr_err("Translating edge value message, json missing payload_field.");
        json_decref(json);
        return;
    }

    bool cert_renewal_support = false;
    json_t *cert_renewal_field = json_object_get(json, "cert_renewal");
    if (cert_renewal_field && json_boolean_value(cert_renewal_field)) {
        cert_renewal_support = true;
    }

    // We store lwm2m representation of node values into pt_object_list_t
    // Create the device structure if device does not exist yet.
    if (!pt_device_exists(g_connection_id, deveui)) {
        uint32_t features = PT_DEVICE_FEATURE_NONE;
        if (cert_renewal_support) {
            features |= PT_DEVICE_FEATURE_CERTIFICATE_RENEWAL;
        }
        pt_status_t status = pt_device_create_with_feature_flags(g_connection_id, deveui, MQTTPT_DEFAULT_LIFETIME, NONE, features, NULL);
        if (status != PT_STATUS_SUCCESS) {
            tr_err("Could not create a device %s error code: %d", deveui, (int32_t) status);
            return;
        }
    }

    // Loop through the payload array containing objects
    int payload_field_size = json_array_size(payload_field);
    tr_debug("PAYLOAD FIELD SIZE %d", payload_field_size);
    for (int i = 0; i < payload_field_size; i++) {
        //Loop through object instances
        json_t *cur_object = json_array_get(payload_field, i);
        json_t *object_instances = json_object_get(cur_object, "objectinstances");
        int object_instances_size = json_array_size(object_instances);
        for (int j = 0; j < object_instances_size; j++) {
            //Loop through resources
            json_t *cur_object_instance = json_array_get(object_instances, j);
            json_t *resources = json_object_get(cur_object_instance, "resources");
            int resources_size = json_array_size(resources);
            for (int k = 0; k < resources_size; k++) {
                json_t *cur_resource = json_array_get(resources, k);
                const char* value = json_string_value(json_object_get(cur_resource, "value"));

                int object_id = atoi(json_string_value(json_object_get(cur_object, "objectid")));
                int object_instance = atoi(json_string_value(json_object_get(cur_object_instance, "objectinstance")));
                int resource_id = atoi(json_string_value(json_object_get(cur_resource, "resourceid")));
                int resource_operations = atoi(json_string_value(json_object_get(cur_resource, "operations")));

                if (!pt_device_resource_exists(g_connection_id, deveui, object_id, object_instance, resource_id)) {
                    //If temperature or humidity, create sensor
                    if (object_id == 3304 || object_id == 3303) {
                        tr_info("Creating sensor.");
                        mqttpt_create_sensor_object(g_connection_id, deveui, object_id, object_instance, value);
                    }
                    else {
                        tr_info("Creating generic object.");
                        mqttpt_create_object(g_connection_id, deveui, object_id, object_instance, resource_id, value, resource_operations);
                    }
                }

                pt_device_set_resource_value(g_connection_id,
                                             deveui,
                                             object_id,
                                             object_instance,
                                             resource_id,
                                             (uint8_t *) strdup(value),
                                             strlen(value),
                                             free);
            }
        }
    }

    if (payload_field_size > 0) {
        char* deveui_ctx = strdup(deveui);
        // If device has been registered, then just write the new values
        if (mqttpt_device_exists(deveui)) {
            tr_info("Updating the changed object structure %s\n", deveui_ctx);
            pt_device_write_values(g_connection_id,
                                   deveui,
                                   mqttpt_update_object_structure_success_handler,
                                   mqttpt_update_object_structure_failure_handler,
                                   deveui_ctx);
        } else {
            // If device has not been registered yet, register it
            tr_info("Registering device %s\n", deveui_ctx);
            pt_device_register(g_connection_id,
                               deveui,
                               mqttpt_device_register_success_handler,
                               mqttpt_device_register_failure_handler,
                               deveui_ctx);
        }
    }

    json_decref(json);
}

/*
 * Function for parsing the mqtt messages we receive from the MQTT gateway.
 * The event type is parsed from topic field and id's and values are parsed from payload.
 *
 * Some examples of possible topics that are received from MQTT GW:
 *
 * GW status: MQTT/Evt
 * Node capabilities: MQTTGw/74fe48ffff19d306/Node/74FE48FFFF1DFD14/Cap
 *                   (MQTTGw/{gweui}/Node/{deveui}/Cap)
 * Node values: MQTTGw/74fe48ffff19d306/Node/74FE48FFFF1DFD14/Val
 *             (MQTTGw/{gweui}/Node/{deveui}/Cap)
 *
 */
#define MQTT_TOPIC_OFFSET_COUNT 5
void mqttpt_handle_message(struct mosquitto *mosq, char *topic, char *payload, int payload_len)
{
    char *saveptr;
    // Parse topic offsets to identify event and id's
    char *topic_offset[MQTT_TOPIC_OFFSET_COUNT];

    if (topic == NULL) {
        return;
    }

    topic_offset[0] = strtok_r(topic, "/", &saveptr);; // points to "MQTT"
    topic_offset[1] = strtok_r(NULL, "/", &saveptr); // points to "Evt" or "{gweui}"
    topic_offset[2] = strtok_r(NULL, "/", &saveptr); // points to "Node" or "Evt"
    topic_offset[3] = strtok_r(NULL, "/", &saveptr); // points to "{deveui}"
    topic_offset[4] = strtok_r(NULL, "/", &saveptr); // points to "Val" or "Cap"

    tr_info("mqttpt handling message");
    tr_info("topic 0: %s", topic_offset[0]);
    tr_info("topic 1: %s", topic_offset[1]);
    tr_info("topic 2: %s", topic_offset[2]);
    tr_info("topic 3: %s", topic_offset[3]);
    tr_info("topic 4: %s", topic_offset[4]);

    if (strcmp(topic_offset[0], "MQTT") == 0) {
        // MQTT/Evt topic, so gateway status message
        tr_info("gw status\n");
        mqttpt_translate_gw_status_message(mosq, payload, payload_len);
        return;
    }

    if (strcmp(topic_offset[0], "MQTTGw") == 0) {
        if (topic_offset[2] == NULL) {
            // Topic missing Evt or Node
            tr_err("MQTTGw message missing Evt or Node part.");
            return;
        }
        // MQTTGw/ topic, topic_offset[1] contains gweui

        if (strcmp(topic_offset[2], "Evt") == 0) {
            // MQTTGw/{gweui}/Evt topic
            mqttpt_translate_node_joined_message(topic_offset[1], payload, payload_len);
        }
        else if (strcmp(topic_offset[2], "Node") == 0) {
            if (topic_offset[3] == NULL || topic_offset[4] == NULL) {
                // Topic is missing {deveui} or Cap or Val
                tr_err("MQTTGw message missing deveui, Cap or Val part.");
                return;
            }
            // MQTTGw/{gweui}/Node/{deveui} topic
            //Val is the old example, where the resource is given as an array
            if (strcmp(topic_offset[4], "Val") == 0) {
                mqttpt_translate_node_value_message(mosq, topic_offset[1], topic_offset[3], payload, payload_len);
            }
            //EdgeVal is new example, where the resource is given as object structure
            else if (strcmp(topic_offset[4], "EdgeVal") == 0) {
                mqttpt_translate_node_edge_value_message(mosq, topic_offset[1], topic_offset[3], payload, payload_len);
            }
        }
        else {
            tr_err("MQTTGw message has unknown topic part 2");
        }
    }
    else {
        tr_err("Unknown topic in message");
    }
}

void mqtt_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
    if (protocol_translator_shutdown_handler_called) {
        tr_info("mqtt_message_callback: shutting down mosquitto loop.");
    }
    if(message->payloadlen){
        tr_info("%s %s", message->topic, (char *) message->payload);
        mqttpt_handle_message(mosq, message->topic, message->payload, message->payloadlen);
    }else{
        tr_info("%s (null)", message->topic);
    }
    fflush(stdout);
}

void mqtt_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
    if(!result){
        mosquitto_subscribe(mosq, NULL, "MQTT/#", 2);
        mosquitto_subscribe(mosq, NULL, "MQTTGw/#", 2);
    }else{
        tr_err("Connect failed");
    }
}

void mqtt_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
    int i;

    printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
    for(i=1; i<qos_count; i++){
        printf(", %d", granted_qos[i]);
    }
    printf("\n");
}

void mqtt_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
    /* Pring all log messages regardless of level. */
    tr_info("%s", str);
}

static void shutdown_and_cleanup()
{
    int started;
    sem_getvalue(&mqttpt_translator_started, &started);
    if (started) {
        pt_status_t status = pt_devices_unregister_devices(g_connection_id,
                                                           mqttpt_devices_unregister_success_handler,
                                                           mqttpt_devices_unregister_failure_handler,
                                                           NULL);
        if (status != PT_STATUS_SUCCESS) {
            tr_warn("Device unregistration failed.");
            pt_client_shutdown(global_pt_ctx->client);
        }
    }
}

int main(int argc, char *argv[])
{
    bool clean_session = true;
    mosq = NULL;

    setup_signals();

    DocoptArgs args = docopt(argc, argv, /* help */ 1, /* version */ "0.1");
    edge_trace_init(args.color_log);
    mqttpt_devices = (mqttpt_device_list_t *) calloc(1, sizeof(mqttpt_device_list_t));
    ns_list_init(mqttpt_devices);
    pt_api_init();
    protocol_translator_callbacks_t *pt_cbs = calloc(1, sizeof(protocol_translator_callbacks_t));
    pt_cbs->connection_ready_cb = mqttpt_connection_ready_handler;
    pt_cbs->connection_shutdown_cb = mqttpt_shutdown_handler;
    pt_cbs->disconnected_cb = mqttpt_disconnected_handler;
    pt_cbs->certificate_renewal_notifier_cb = mqtt_certificate_renewal_notification_handler;
    pt_cbs->device_certificate_renew_request_cb = mqtt_device_certificate_renewal_request_handler;

    global_pt_ctx = malloc(sizeof(protocol_translator_api_start_ctx_t));

    global_pt_ctx->socket_path = args.edge_domain_socket;
    pt_client_t *client = pt_client_create(global_pt_ctx->socket_path,
                                           pt_cbs);
    global_pt_ctx->client = client;

    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, clean_session, NULL);
    if(!mosq){
        tr_err("Error: Out of memory.");
        return 1;
    }

    mosquitto_log_callback_set(mosq, mqtt_log_callback);
    mosquitto_connect_callback_set(mosq, mqtt_connect_callback);
    mosquitto_message_callback_set(mosq, mqtt_message_callback);
    mosquitto_subscribe_callback_set(mosq, mqtt_subscribe_callback);

    if(mosquitto_connect(mosq, args.mosquitto_host,
                         atoi(args.mosquitto_port),
                         atoi(args.keep_alive))){
        tr_err("Unable to connect.");
        return 1;
    }

    sem_init(&mosquitto_stop, 0, 0);
    int stop = 0;
    while(!stop) {
        mosquitto_loop(mosq, -1, 1);
        sem_getvalue(&mosquitto_stop, &stop);
    }
    tr_info("Mosquitto event loop stopped.");

    void *result = NULL;
    shutdown_and_cleanup();
    int started;
    sem_getvalue(&mqttpt_translator_started, &started);
    if (started) {
        (void) pthread_join(mqttpt_thread, &result);
    }
    pt_client_free(client);
    free(global_pt_ctx);
    free(pt_cbs);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    edge_trace_destroy();
    return 0;
}

/**
 * \brief The mqttpt client example shutdown handler.
 *
 * \param signum The signal number that initiated the shutdown handler.
 */
void shutdown_handler(int signum)
{
    sem_post(&mosquitto_stop);
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
#ifdef DEBUG
    tr_info("Setting support for SIGUSR2");
    if (sigaction(SIGUSR2, &sa, NULL) != 0) {
        return false;
    }
#endif
    return true;
}

/**
 * @}
 * close EDGE_PT_CLIENT_EXAMPLE Doxygen group definition
 */
