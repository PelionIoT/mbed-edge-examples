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

#include "mosquitto.h"

#include "common/constants.h"
#include "pt-client/pt_api.h"
#include "mbed-trace/mbed_trace.h"
#include "examples-common/client_config.h"
#include "mqttpt_example_clip.h"
#include "common/edge_trace.h"

#define TRACE_GROUP "clnt-example"

#include "jansson.h"

#include <pthread.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>

struct connection *g_connection = NULL;

void mqttpt_connection_ready_handler(struct connection *connection, void* ctx);
int mqttpt_receive_write_handler(struct connection *connection,
                                 const char *device_id, const uint16_t object_id,
                                 const uint16_t instance_id, const uint16_t resource_id,
                                 const unsigned int operation,
                                 const uint8_t *value, const uint32_t value_size,
                                 void* userdata);
void mqttpt_shutdown_handler(struct connection **connection, void *ctx);
volatile int mqttpt_translator_started = 0;
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
} protocol_translator_api_start_ctx_t;
protocol_translator_api_start_ctx_t *global_pt_ctx;

typedef NS_LIST_HEAD(mqttpt_device_t, link) mqttpt_device_list_t;
bool protocol_translator_shutdown_handler_called = false;
mqttpt_device_list_t *mqttpt_devices;
void mqttpt_add_device(const char* deveui) {
    if (deveui == NULL) {
        return;
    }
    mqttpt_device_t *device = (mqttpt_device_t*)calloc(1, sizeof(mqttpt_device_t));
    if (device == NULL) {
        return;
    }
    device->deveui = strdup(deveui);
    printf("Adding device to list");
    ns_list_add_to_end(mqttpt_devices, device);
}

int mqttpt_device_exists(const char* deveui) {
    printf("Checking device '%s' exists\n", deveui);
    ns_list_foreach(mqttpt_device_t, device, mqttpt_devices) {
        printf("Checking %s\n", device->deveui);
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
    protocol_translator_callbacks_t *pt_cbs = calloc(1, sizeof(protocol_translator_callbacks_t));
    pt_cbs->connection_ready_cb = mqttpt_connection_ready_handler;
    pt_cbs->received_write_cb = mqttpt_receive_write_handler;
    pt_cbs->connection_shutdown_cb = mqttpt_shutdown_handler;

    pt_client_start(pt_start_ctx->socket_path, "testing-mqtt", pt_cbs,
                    (void*) ctx, &g_connection);
    free(pt_cbs);
    return NULL;
}

void mqttpt_start_translator(struct mosquitto *mosq)
{
    if (mqttpt_translator_started == 0) {
        pthread_create(&mqttpt_thread, NULL, &mqttpt_translator_thread_routine, (void *) mosq);
        mqttpt_translator_started = 1;
    }
}

/*
 * Callback handlers for PT operations
 */

void mqttpt_device_register_success_handler(const char *device_id, void *ctx)
{
    if (ctx) {
        printf("A device register finished successfully.\n");
        printf("deveui %s\n", (char*)ctx);
        mqttpt_add_device((const char*)ctx);
    }
    free(ctx);
}

void mqttpt_device_register_failure_handler(const char *device_id, void *ctx)
{
    printf("A device register failed.\n");
    free(ctx);
}

void mqttpt_device_write_success_handler(const char *device_id, void *ctx)
{
    printf("A device write finished successfully.\n");
    free(ctx);
}

void mqttpt_device_write_failure_handler(const char *device_id, void *ctx)
{
    printf("A device write failed.\n");
    free(ctx);
}

void mqttpt_protocol_translator_registration_success_handler(void *ctx)
{
    mqttpt_translator_started = 1;
    printf("MQTT translator registered successfully.\n");
}

void mqttpt_protocol_translator_registration_failure_handler(void *ctx)
{
    struct mosquitto *mosq = (struct mosquitto *) ctx;

    printf("MQTT translator registration failed.\n");
    mosquitto_disconnect(mosq);
}

void mqttpt_connection_ready_handler(struct connection *connection, void* ctx)
{
    pt_register_protocol_translator(g_connection, mqttpt_protocol_translator_registration_success_handler, mqttpt_protocol_translator_registration_failure_handler, ctx);
}

int mqttpt_receive_write_handler(struct connection *connection,
                                 const char *device_id, const uint16_t object_id,
                                 const uint16_t instance_id, const uint16_t resource_id,
                                 const unsigned int operation,
                                 const uint8_t *value, const uint32_t value_size,
                                 void* userdata)
{
    // Do nothing here.
    return 0;
}

void mqttpt_shutdown_handler(struct connection **connection, void *userdata)
{
    struct mosquitto *mosq = (struct mosquitto *) userdata;
    printf("Shutting down the mqttpt example\n");
    mosquitto_disconnect(mosq);
}

/*
 * Create the lwm2m structure for a "generic" sensor object. Same resources can be used
 * to represent temperature and humidity sensors by just changing the object id
 * temperature sensor id = 3303 (http://www.openmobilealliance.org/tech/profiles/lwm2m/3303.xml)
 * humidity sensor id = 3304 (http://www.openmobilealliance.org/tech/profiles/lwm2m/3304.xml)
 */
void mqttpt_create_sensor_object(pt_device_t *device, sensor_type_e sensor_type, const char* value)
{
    uint16_t object_id = 0;
    if (sensor_type == SENSOR_TEMPERATURE) {
        object_id = 3303;
    }
    else {
        object_id = 3304;
    }

    if (value == NULL || device == NULL) {
        return;
    }

    // Resource value buffer ownership is transferred so we need to make copies of the const buffers passed in
    char *value_buf = strdup(value);

    if (value_buf == NULL) {
        free(value_buf);
        return;
    }
    pt_status_t status = PT_STATUS_SUCCESS;
    pt_object_t *object_sensor = pt_device_add_object(device, object_id, &status);
    if (status != PT_STATUS_SUCCESS) {
        printf("Object creation failed, status %d\n", status);
        return;
    }
    pt_object_instance_t *instance_sensor = pt_object_add_object_instance(object_sensor, 0, &status);
    if (status != PT_STATUS_SUCCESS) {
        printf("Object instance creation failed, status %d\n", status);
        // TODO: free object instance
        return;
    }
    pt_resource_opaque_t *resource_value = pt_object_instance_add_resource(instance_sensor,
                                                                           /* Sensor value */ 5700,
                                                                           LWM2M_OPAQUE,
                                                                           (uint8_t*)value_buf,
                                                                           strlen(value_buf),
                                                                           &status);
    if (status != PT_STATUS_SUCCESS) {
        printf("Resource creation failed, status %d\n", status);
        // TODO: free object and object instance
        return;
    }

    if (object_sensor == NULL || instance_sensor == NULL || resource_value == NULL) {
        // TODO: Free all structures if creating one failed
        // Free buffers
        free(value_buf);
    }
}

/*
 * Functions which translate different types of MQTT messages we receive through mqtt
 */
void mqttpt_translate_gw_status_message(struct mosquitto *mosq, const char *payload, const int payload_len)
{
    json_error_t error;
    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        fprintf(stderr, "Could not parse node value json.\n");
        return;
    }
    if (mqttpt_translator_started == 0) {
        mqttpt_start_translator(mosq);
    }
    json_decref(json);
}

void mqttpt_translate_node_joined_message(const char* gweui, const char* payload, const int payload_len)
{
    json_error_t error;
    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        fprintf(stderr, "Could not parse node value json.\n");
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
        fprintf(stderr, "Could not parse node value json.\n");
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
    printf("Translating value message\n");

    if (mqttpt_translator_started == 0) {
        mqttpt_start_translator(mosq);
        printf("Translating value message, PT was not registered yet registering it now.\n");
        return;
    }

    if (gweui == NULL || deveui == NULL || payload == NULL) {
        fprintf(stderr, "Translating value message, missing gweui, deveui or payload.\n");
        return;
    }

    json_t *json = json_loads(payload, 0, &error);
    if (json == NULL) {
        fprintf(stderr, "Translating value message, could not parse json.\n");
        return;
    }

    json_t *payload_field = json_object_get(json, "payload_field");
    if (payload_field == NULL) {
        fprintf(stderr, "Translating value message, json missing payload_field.\n");
        json_decref(json);
        return;
    }

    // We store lwm2m representation of node values into pt_object_list_t
    // Create the device structure
    pt_status_t status = PT_STATUS_SUCCESS;
    pt_device_t *device = pt_create_device(strdup(deveui), MQTTPT_DEFAULT_LIFETIME, NONE, &status);
    if (device == NULL || status != PT_STATUS_SUCCESS) {
        fprintf(stderr, "Translating value message, could not create device structure");
        return;
    }
    int values_count = 0;

    // Loop through the payload array containing new values
    int payload_field_size = json_array_size(payload_field);
    for (int i = 0; i < payload_field_size; i++) {
        // New values are also an array with format
        // ["VALUE", Sensor type, value, measurement unit description, minimum possible value, maximum possible value, id]
        json_t *value_array = json_array_get(payload_field, i);
        int value_array_size = json_array_size(value_array);
        if (value_array == NULL || value_array_size != 7) {
            fprintf(stderr, "Translating value message, json has invalid payload array.\n");
            break;
        }

        const char* type = json_string_value(json_array_get(value_array, 1));
        const char* value = json_string_value(json_array_get(value_array, 2));

        // Determine type of sensor
        if (type != NULL) {
            if (strcmp(type, "Temperature") == 0) {
                mqttpt_create_sensor_object(device, SENSOR_TEMPERATURE, value);
            }
            else if (strcmp(type, "Humidity") == 0) {
                mqttpt_create_sensor_object(device, SENSOR_HUMIDITY, value);
            }
            values_count++;
        }
    }

    if (values_count > 0) {
        // We need to only send values if we actually got some
        char* deveui_ctx = strdup(deveui);
        // If device has been registered, then just write the new values
        if (mqttpt_device_exists(deveui)) {
            printf("Writing value to device %s\n", deveui_ctx);
            pt_write_value(g_connection, device, device->objects, mqttpt_device_write_success_handler, mqttpt_device_write_failure_handler, deveui_ctx);
        }
        // If device has not been registered yet, register it
        else {
            printf("Registering device %s\n", deveui_ctx);
            pt_register_device(g_connection, device, mqttpt_device_register_success_handler, mqttpt_device_register_failure_handler, deveui_ctx);
        }
    }

    pt_device_free(device);

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

    printf("mqttpt handling message\n");
    printf("topic 0: %s\n", topic_offset[0]);
    printf("topic 1: %s\n", topic_offset[1]);
    printf("topic 2: %s\n", topic_offset[2]);
    printf("topic 3: %s\n", topic_offset[3]);
    printf("topic 4: %s\n", topic_offset[4]);

    if (strcmp(topic_offset[0], "MQTT") == 0) {
        // MQTT/Evt topic, so gateway status message
        printf("gw status\n");
        mqttpt_translate_gw_status_message(mosq, payload, payload_len);
        return;
    }

    if (strcmp(topic_offset[0], "MQTTGw") == 0) {
        if (topic_offset[2] == NULL) {
            // Topic missing Evt or Node
            fprintf(stderr, "MQTTGw message missing Evt or Node part.\n");
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
                fprintf(stderr, "MQTTGw message missing deveui, Cap or Val part.\n");
                return;
            }
            // MQTTGw/{gweui}/Node/{deveui} topic
            if (strcmp(topic_offset[4], "Val") == 0) {
                mqttpt_translate_node_value_message(mosq, topic_offset[1], topic_offset[3], payload, payload_len);
            }
        }
        else {
            fprintf(stderr, "MQTTGw message has unknown topic part 2\n");
        }
    }
    else {
        fprintf(stderr, "Unknown topic in message\n");
    }
}

void mqtt_message_callback(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
    if (protocol_translator_shutdown_handler_called) {
        printf("mqtt_message_callback: shutting down mosquitto loop.\n");
    }
    if(message->payloadlen){
        mqttpt_handle_message(mosq, message->topic, message->payload, message->payloadlen);
        printf("%s %s\n", message->topic, (char *) message->payload);
    }else{
        printf("%s (null)\n", message->topic);
    }
    fflush(stdout);
}

void mqtt_connect_callback(struct mosquitto *mosq, void *userdata, int result)
{
    if(!result){
        mosquitto_subscribe(mosq, NULL, "MQTT/#", 2);
        mosquitto_subscribe(mosq, NULL, "MQTTGw/#", 2);
    }else{
        fprintf(stderr, "Connect failed\n");
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
    printf("%s\n", str);
}

int main(int argc, char *argv[])
{
    bool clean_session = true;
    struct mosquitto *mosq = NULL;

    DocoptArgs args = docopt(argc, argv, /* help */ 1, /* version */ "0.1");
    edge_trace_init(args.color_log);
    mqttpt_devices = (mqttpt_device_list_t*)calloc(1, sizeof(mqttpt_device_list_t));
    ns_list_init(mqttpt_devices);

    global_pt_ctx = malloc(sizeof(protocol_translator_api_start_ctx_t));

    global_pt_ctx->socket_path = NULL;
    global_pt_ctx->socket_path = args.edge_domain_socket;

    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, clean_session, NULL);
    if(!mosq){
        fprintf(stderr, "Error: Out of memory.\n");
        return 1;
    }

    mosquitto_log_callback_set(mosq, mqtt_log_callback);
    mosquitto_connect_callback_set(mosq, mqtt_connect_callback);
    mosquitto_message_callback_set(mosq, mqtt_message_callback);
    mosquitto_subscribe_callback_set(mosq, mqtt_subscribe_callback);

    if(mosquitto_connect(mosq, args.mosquitto_host,
                         atoi(args.mosquitto_port),
                         atoi(args.keep_alive))){
        fprintf(stderr, "Unable to connect.\n");
        return 1;
    }

    mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    void *result = NULL;
    if (mqttpt_translator_started) {
        (void) pthread_join(mqttpt_thread, &result);
    }
    free(global_pt_ctx);
    edge_trace_destroy();
    return 0;
}


/**
 * @}
 * close EDGE_PT_CLIENT_EXAMPLE Doxygen group definition
 */
