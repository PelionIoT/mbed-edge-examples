## MQTT gateway simulator

A simple MQTT gateway simulator that can be used to test mqttpt-example protocol translator (without real MQTT hardware). Publishes gateway status messages (`mqtt_gw.sh`) and device value messages (`mqtt_ep.sh`). The device script `mqtt_ep.sh` accepts parameters to register multiple devices or update temperature and/or humidity sensor values.

### Requirements
Requires `mosquitto_pub` client for publishing the MQTT messages to MQTT broker.
`$ apt install mosquitto-clients`

### Usage
1. Run `edge-core` and `mqttpt-example`
2. Register MQTT gateway by running `mqtt_gw.sh` script.

`$ ./mqtt_gw.sh`

3. Register device by running `mqtt_ep.sh` script.

`$ ./mqtt_ep.sh`

4. Update certificates on the gateway

`$ ./mqtt_gw_certificates_renew.sh cert_id_1 cert_id_2`

Above updates certificates with names `cert_id_1` and `cert_id_2`.

Notice that the device script accepts parameters for device name, temperature and humidity. For example to register device `MY_DEVICE` and set temperature value to 20.1C and humidity value to 67.5%:

`$ ./mqtt_ep.sh -d MY_DEVICE -t 20.1 -h 67.5`
