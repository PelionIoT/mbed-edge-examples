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

### Crypto API

MQTT Gateway protocol translator example demonstrates multiple cryptographic operations in the gateway.
To subscribe to certificate renewal notifications, do the following when the gateway is in
connected state:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh set-certificates-list certificate_names
```

To renew a certificate, it must first be added to the certificate list with the `set-certificates-list` operation as described above. To renew a certificate, do the following:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh renew-certificate certificate_name
```

Only one certificate can be renewed at a time, and the certificate identified by `certificate_name` must exist on the device. It's supported to renew factory provisioned certificates, 
for example ones created by the FCU tool. Renewing developer certificates is not supported. Currently it's possible to renew the 
`LWM2M` certificate using this API. However it's possible that certificate renewal will be limited only to "third party" certificates.

To test out fetching of the certificate or public key, do the following when the gateway is in the connected state:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh get-certificate certificate_name
$ mqttgw_sim/mqtt_gw_crypto_api.sh get-public-key certificate_name
```

Only one certificate or public key can be fetched at the same time. The certificates identified by `certificate_name`
must exist on the device.

To test out the asymmetric signing and verifying, do the following:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh asymmetric-sign private_key_name hash_digest
$ mqttgw_sim/mqtt_gw_crypto_api.sh asymmetric-verify private_key_name hash_digest signature
```

The certificates identified by `private_key_name` must exist on the device. `Hash-digest` is a string that has been 
sha256-summed and base64-encoded. Suitable string can be created with following commands:

```
hash_digest="test_string"
#Create sha256 sum
hash_digest=$(echo $hash_digest |sha256sum)
#Cut the first 64 characters, the actual sha256-sum
hash_digest=$(echo $hash_digest |cut -c -64)
#Convert to binary
hash_digest=$(echo $hash_digest |xxd -r -p)
#Base64 encode
hash_digest=$(echo $hash_digest |base64)
```

To test out the ecdh key agreement, do the following:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh ecdh-key-agreement private_key_name peer_public_key
```

The certificates identified by `private_key_name` must exist on the device. Parameter `peer_public_key` is a public key
that has been base64-encoded.

To test out the random byte array generation, do the following:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh ecdh-key-agreement generate-random size_of_array
```

### Device certificate renewal

The MQTT protocol translator example demonstrates device certificate renewal or enrollment operation. To enable certificate enrollment on a device a `cert_renewal` key should be set to `true` in the endpoint value message (see the mqttgw_sim/mqtt_ep.sh script for example).

#### Device initiated certificate renewal
The device initiated certificate renewal or enrollment is performed using the `mqttgw_sim/mqtt_gw_crypto_api.sh` script:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh device-cert-renew device_name certificate_name csr
```

where `device_name` is the device that is performing the enrollment, `certificate_name` is the certificate to enroll and `csr` is the certificate signing request in DER format and base64 encoded.

#### Cloud initiated certificate renewal

The renewal or enrollment operation can also be requested from the cloud, for this [see service API documentation on certificate enrollment](https://www.pelion.com/docs/device-management/current/service-api-references/certificate-enrollment.html). When a certificate enrollment is requested from the cloud, a certificate renewal request is published to `MQTTPt/DeviceCertificateRenewalRequest` topic with the payload containing the device name and certificate name. After a certificate renewal request is received, the enrollment should be continue the same way as in the [device initiated renewal](#Device-initiated-certificate-renewal).

#### Generating test CSR

A test certificate signing request (CSR) can be generated with openssl:

```
$ openssl ecparam -name prime256v1 -genkey -noout -out private.key     # Create private key
$ openssl req -new -nodes -out csr.req -key private.key -subj "/CN=CUSTOM_CERT_TEST/O=TEST/L=Test/ST=Test/C=EN/OU=ARM" -sha256 -outform DER     # Create CSR
$ base64 -w 0 csr.req > csr_b64.txt    # Base64 encode the CSR
```

The generated CSR can then be used when performing the enrollment for example like this:

```
$ mqttgw_sim/mqtt_gw_crypto_api.sh device-cert-renew device_name certificate_name $(cat csr_b64.txt)
```
