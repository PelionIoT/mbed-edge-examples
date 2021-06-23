# Changelog for Edge examples

## Release 0.18.0

### Features
 * Updated git submodule mbed-edge to 0.18.0
 * Updated docopt script to use python3.
 * Use double in node.js examples to fix conversion errors.
 * Added Dockerfile to build and run pt-example and simple-js-examples inside docker container.

### Known issues
 * Pelion Device Management users do not receive notifications for the translated device’s LwM2M resources which are registered with operations write (PUT) or execute (POST).

## Release 0.17.0
 * Updated git submodule mbed-edge to 0.17.0

## Release 0.16.0 (2021-03-15)
 * Updated git submodule mbed-edge to 0.16.0
 * PT and Gateway Resource Management (GRM) examples can now update LwM2M resource name while registering gateway or device resources.
 * Added JS example `simple-fota-example.js` to demonstrate the API use of subdevice FOTA feature.
 * Updated the C example `pt-example` to use new `manifest_vendor_and_class` API call to receive vendorID and classID of the parsed/validated update manifest.

## Release 0.15.0 (2021-01-12)
 * Updated git submodule mbed-edge to 0.15.0

## Release 0.14.0 (2020-08-31)
 * Updated git submodule mbed-edge to 0.14.0
 * Added `simple-grm-example.js` to demonstrate gateway resource management. It helps understand the basic resource manager functionality, such as registering service, adding and updating LwM2M objects.
 * Updated the C example `pt-example` to demonstrate the flow of firmware update process for devices behind the gateway.

## Release 0.13.0 (2020-04-29)
 * Update git submodule mbed-edge to 0.13.0

## Release 0.12.0 (2020-01-08)
 * Updated git submodule mbed-edge to 0.12.0

## Release 0.11.0 (2019-10-03)
 * Updated git submodule mbed-edge to 0.11.0

## Release 0.10.0 (2019-07-05)
 * Added fetch for certificate and public key to the mqtt-example.
 * Added device certificate renewal support in mqtt-example.
 * Added option to create a generic object to the mqtt-example.
 * Split the `simple-pt-example.js` Javascript example to 2 examples:
   * `simple-pt-example.js` demonstrating PT and Device management.
   * `pt-crypto-api-example.js` demonstrating the crypto operations.
 * Added new crypto operations to `pt-crypto-api-example.js`.
 * Replace MQTT example `renew-certificates` API with 2 separate methods:
   * `renew-certificate` API which renews a single certificate.
   * `set-certificates-list` API which subscribes to receive notifications for given list of certificates.

## Release 0.9.0 (2019-04-19)

Added certificate renewal for the examples.

## Release 0.8.0 (2019-02-27)

 * BLE PT Example bugfixes.
   * Fix invalid read of options variant in `ble_write_characteristic` function.
   * Fix unsafe signal handler calls to `g_idle_add` by using GLib provided signal handlers.
   * Remove stray call to `pthread_mutex_unlock` in `devices_find_device`.
   * Fix thread safety issues when device is removed from dbus.
   * Add logic to reconnect to the BLE device if it gets disconnected.
   * Add functionality to reconnect to the bluetooth daemon if the bluetooth daemon restarts.
   * Add a json configuration file to extended discovery mode with support to whitelist devices.
   * Fix a possible jam if starting the BLE PT example fails for example with incorrect parameters.
 * PT example bugfixes.
   * Measures max value reset corrected to `-FLT_MAX`.
 * cmake 3.5 required

## Release 0.7.1 (2018-01-07)

 * Bluetooth LE protocol translator example.
 * Updated examples to use the Protocol Translator API v2.
 * Parameterized simple protocol translator functions to create, update and unregister devices with `deviceId`. The device ID is given in the main entry functions.

## Release 0.6.0 (2018-10-19)

 * Separated Edge example repository introduced.
 * Added local management API example to `simple-js-examples`.
 * Renamed `lorapt-example` to `mqttpt-example` which reflects the actual
   implementation better.
 * Started using `pt_resource_t` instead of `pt_resource_opaque_t`, because `pt_resource_opaque_t` is deprecated.
