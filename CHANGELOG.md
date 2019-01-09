# Changelog for Edge examples

## Release 0.7.1 (2018-01-07)

 * Bluetooth LE protocol translator example.
 * Updated examples to use the protocol translator API v2.
 * Parameterized simple protocol translator device create, update and unregister functions
   with `deviceId`. The device ID is given in the main entry functions.

## Release 0.6.0 (2018-10-19)

 * Separated Edge example repository introduced.
 * Added local management API example to `simple-js-examples`.
 * Renamed `lorapt-example` to `mqttpt-example` which reflects the actual
   implementation better.
 * Started using `pt_resource_t` instead of `pt_resource_opaque_t`, because `pt_resource_opaque_t` is deprecated.

