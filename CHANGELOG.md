# Changelog for Edge examples

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
