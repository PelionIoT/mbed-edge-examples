# Simple Javascript protocol translator and management example

## simple-pt-example.js and pt-crypto-api-example.js

These example protocol translators show the calls and parameters to pass to
Edge Core protocol translator API. The `simple-pt-example.js` demonstrates the
basic protocol translator functionality, ie. registering, unregistering and 
device operations. The `pt-crypto-api-example.js` demonstrates specifically 
the usage of the crypto API's, ie. certificate and public key usage, certificate renewal, asymmetric
operations and ECDH key agreement operation. The websocket connection and
JSONRPC 2.0 specification and communication is left out of the scope to
keep the examples simple.

Libraries are used to handle the websocket and JSONRPC 2.0 communication.

Please study the example code to see how to use the protocol translator
JSONRPC 2.0 API and read the relevant documentation for Edge APIs from
[Device Management Docs](https://cloud.mbed.com/docs/current).

## simple-mgmt-example.js

This example management application demostrates the calls and parameters to pass
to Edge core management API. The websocket connection and JSONRPC 2.0
specification and communciation is left out of the scope to keep the
example simple.

This application is interactive and supports few command that can be given
to control the API. See the usage on the example application startup or
by using `help()` function.

Libraries are used to handle the websocket and JSONRPC 2.0 communication.

Please study the example code to see how to use the management JSONRPC 2.0
API and read the relevant documentation for Edge APIs from
[Device Management Docs](https://cloud.mbed.com/docs/current).

## Dependencies

This example uses `node.js v8` or higher.

Install the dependencies:
```bash
$ npm install
```

Dependencies are:

    simple-edge-api-examples
    ├── es6-promisify@6.0.0
    ├─┬ json-rpc-ws@5.0.0
    │ ├─┬ debug@3.1.0
    │ │ └── ms@2.0.0
    │ ├── uuid@3.2.1
    │ └─┬ ws@4.1.0
    │   ├── async-limiter@1.0.0
    │   └── safe-buffer@5.1.2
    └── repl.history@0.1.4

The list with version can be listed with:
```bash
$ npm ls
```

## Running the protocol translator example

Fixed values for the example:
 * Protocol translator name is `simple-pt-example`
 * The device name is `example-device-1`
 * The example device has two LwM2M objects:
   * `3303` which is a temperature sensor and has one readable resource `5700`
   * `3308` which is a set point sensor and has one writable resource `5900`
 * Both resource values are floating point values.

1. Run the Edge Core
   See the pre-requisites to build and run from the root [README.md](./README.md)
1. Verify that Edge device is connected to Device Management and visible
   from [Device Management Portal](https://portal.mbedcloud.com)
1. Run this example and connect to Edge.
   ```bash
   $ nodejs simple-pt-example.js
   ```
1. Monitor the registered Edge and endpoint device from Device Management Portal.

## Running the management API example

Fixed values for the example:

1. Run the Edge Core
   See the pre-requisites to build and run from the root [README.md](./README.md)
1. Verify that Edge device is connected to Device Management and visible
   from [Device Management Portal](https://portal.mbedcloud.com)
1. Connect a protocol translator and some devices to Edge.
   For example one of the protocol translator examples provided from
   [Edge examples](https://github.com/ARMmbed/mbed-edge-examples).
1. Run this example and connect to Edge.
   ```bash
   $ nodejs simple-mgmt-example.js
   ```
1. Use the `connect()` function provided by the interactive example to connect
   to the Edge Core.
1. After successful connection you can query devices from Edge Core with `devices()` function.
1. See the example application help for other functions.
