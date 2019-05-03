## MQTT Gateway protocol translator example

<span class="warnings">**Warning:** This is an example to demonstrate the protocol
translator for MQTT endpoints. Do not use for production implementation.</span>

A very simple protocol translator example for the MQTT Gateway.
The example translates MQTT endpoints connected to Device Management via Edge Core.

#### Dependencies

The mqttpt-example has a dependency to `jansson`-library. In the example build the
`jansson`-library built by the `mbed-edge`-submodule is used.

### Compilation

Read the README.md in the root for the basic build instructions.

### Operation description

The protocol translator receives MQTT endpoint information and updates from the
MQTT gateway through the MQTT broker by subscribing to the "MQTT/#" and
"MQTTGw/#" topics. The MQTT messages are handled by the `mqttpt_handle_message`
function.

To simplify the example, only the gateway status messages and endpoint
value messages are handled by the protocol translator although there are stubs for
other message types as well. The example uses a hardcoded protocol translator name
(testing-mqtt).

The gateway status message is used to create the protocol
translator instance in the `mqttpt_translate_gw_status_message()` function.
The protocol translator will also be implicitly registered if an endpoint value
message is received and the protocol translator is not yet registered.

Endpoint value messages are handled by `mqttpt_translate_node_value_message()` function.
When an endpoint value message is received, the protocol translator checks from a
list whether it has seen the endpoint before or if it is a new one.

New endpoints are registered by calling the `pt_register_device` API and in the
`mqttpt_device_register_success_handler` they are added to the list. Seen endpoints
only get their value updated by calling the `pt_write_value` API. Sensor values
(temperature and humidity) are parsed from the value update message and a LwM2M
object is created for each respectively (object ID 3303 for temperature and
3304 for humidity). A value resource (5700) is created for each object to
hold the sensor value.

### Running

Start edge-core:

```
$ ./edge-core
```

Start the mqttpt-example:

```
$ mqttpt-example
```

On Device Management, you should see the MQTT endpoints appear as new devices and they
should have the sensors as resources (temperature as /3303/0/5700 and humidity
as /3304/0/5700).

The mqttpt-example supports optional command-line parameters, for example to set Edge Core domain socket path.
For help, use:

```
$ mqttpt-example --help
```

### Certicicate renewal

MQTT Gateway protocol translator example demonstrates certificate renewal in the gateway.
To renew a certicate and subscribe to certificate renewal notifications, do the following when the gateway is in
connected state:

```
$ mqtt_sim/mqtt_gw_certificates_renew.sh certificate_name
```

The script supports updating multiple certificates at the same time and the certificate identified by `certificate_name`
must exist on the device. It's supported to renew factory provisioned certificates, for example ones created byt the FCU
tool. Renewing developer certificates is not supported. Currently it's possible to renew the `LWM2M` certificate using
this API. However it's possible that certificate renewal will be limited only to "third party" certificates.

