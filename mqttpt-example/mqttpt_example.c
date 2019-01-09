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
#include "mbed-trace/mbed_trace.h"
#include "examples-common-2/client_config.h"
#include "mqttpt_example_clip.h"
#include "common/edge_trace.h"

#define TRACE_GROUP "clnt-example"

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

/*
 * Callback handlers for PT operations
 */

void mqttpt_device_register_success_handler(const connection_id_t connection_id, const char *device_id, void *ctx)
{
    if (ctx) {
        tr_info("A device register finished successfully.");
        tr_info("deveui %s", (char*)ctx);
        mqttpt_add_device((const char*)ctx);
    }
    free(ctx);
}

void mqttpt_device_register_failure_handler(const connection_id_t connection_id, const char *device_id, void *ctx)
{
    tr_info("A device register failed.");
    free(ctx);
}

void mqttpt_devices_unregister_success_handler(connection_id_t connection_id, void *userdata)
{
    tr_info("Devices unregistration success.");
    (void) connection_id;
    (void) userdata;
    pt_client_shutdown(global_pt_ctx->client);
}

void mqttpt_devices_unregister_failure_handler(connection_id_t connection_id, void *userdata)
{
    tr_err("Devices unregistration failed.");
    (void) connection_id;
    (void) userdata;
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
}

void mqttpt_protocol_translator_registration_failure_handler(void *ctx)
{
    (void) ctx;
    tr_info("MQTT translator registration failed.");
    sem_post(&mosquitto_stop);
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

/*
 * Create the lwm2m structure for a "generic" sensor object. Same resources can be used
 * to represent temperature and humidity sensors by just changing the object id
 * temperature sensor id = 3303 (http://www.openmobilealliance.org/tech/profiles/lwm2m/3303.xml)
 * humidity sensor id = 3304 (http://www.openmobilealliance.org/tech/profiles/lwm2m/3304.xml)
 */
bool mqttpt_create_sensor_object(connection_id_t connection_id, const char *deveui, sensor_type_e sensor_type, const char* value)
{
    uint16_t object_id = 0;
    if (sensor_type == SENSOR_TEMPERATURE) {
        object_id = 3303;
    }
    else {
        object_id = 3304;
    }

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
                                                0,
                                                5700,
                                                LWM2M_OPAQUE,
                                                (uint8_t *) value_buf,
                                                strlen(value_buf),
                                                free);
    status = pt_device_add_resource_with_callback(connection_id,
                                                  deveui,
                                                  object_id,
                                                  0,
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
    int started;
    sem_getvalue(&mqttpt_translator_started, &started);
    if (started == 0) {
        mqttpt_start_translator(mosq);
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
["VALUE","Temperature","+27.9","°C","-50","50","1"],
["VALUE","Humidity","64.52","%","0","100","2"]
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
    int values_count = 0;

    if (!pt_device_resource_exists(g_connection_id, deveui, 3303, 0, 5700)) {
        mqttpt_create_sensor_object(g_connection_id, deveui, SENSOR_TEMPERATURE, "0");
    }
    if (!pt_device_resource_exists(g_connection_id, deveui, 3304, 0, 5700)) {
        mqttpt_create_sensor_object(g_connection_id, deveui, SENSOR_HUMIDITY, "0");
    }

    // Loop through the payload array containing new values
    int payload_field_size = json_array_size(payload_field);
    for (int i = 0; i < payload_field_size; i++) {
        // New values are also an array with format
        // ["VALUE", Sensor type, value, measurement unit description, minimum possible value, maximum possible value, id]
        json_t *value_array = json_array_get(payload_field, i);
        int value_array_size = json_array_size(value_array);
        if (value_array == NULL || value_array_size != 7) {
            tr_err("Translating value message, json has invalid payload array.");
            break;
        }

        const char* type = json_string_value(json_array_get(value_array, 1));
        const char* value = json_string_value(json_array_get(value_array, 2));

        // Determine type of sensor
        if (type != NULL) {
            if (strcmp(type, "Temperature") == 0) {
                pt_device_set_resource_value(g_connection_id,
                                             deveui,
                                             3303,
                                             0,
                                             5700,
                                             (uint8_t *) strdup(value),
                                             strlen(value),
                                             free);
            } else if (strcmp(type, "Humidity") == 0) {
                pt_device_set_resource_value(g_connection_id,
                                             deveui,
                                             3304,
                                             0,
                                             5700,
                                             (uint8_t *) strdup(value),
                                             strlen(value),
                                             free);
            }
            values_count++;
        }
    }

    if (values_count > 0) {
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
            if (strcmp(topic_offset[4], "Val") == 0) {
                mqttpt_translate_node_value_message(mosq, topic_offset[1], topic_offset[3], payload, payload_len);
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
        mqttpt_handle_message(mosq, message->topic, message->payload, message->payloadlen);
        tr_info("%s %s", message->topic, (char *) message->payload);
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

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
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
