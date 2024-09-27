# Simple Javascript protocol translator and management example

Please note that these example require a working Node.JS environment in your system.
LmP Edge devices do not by default have it.

## simple-fota-example.js
These example protocol translators (PT) demostrate the FOTA capabilities. The PT needs to register these resources:

| Resource                   | Object id  | Instance id | Resource id |
|----------------------------|------------|-------------|-------------|
| Component Identity         | 14         | 0           | 0        |
| Component Version          | 14         | 0           | 2        |
| Manifest Content           | 10252      | 0           | 1        |
| Manifest State             | 10252      | 0           | 2        |
| Manifest Result            | 10252      | 0           | 3        |
| Manifest Protocol Support  | 10255      | 0           | 0        |
| Bootloader hash            | 10255      | 0           | 1        |
| OEM bootloader hash        | 10255      | 0           | 2        |
| Vendor UUID                | 10255      | 0           | 3        |
| Class UUID                 | 10255      | 0           | 4        |

The edge-core uses these resources to receive the manifest, send the status as well as the result back to the cloud. The PT needs to implement the function that receives the vendorid, classid, version and firmware size using `manifest_meta_data` API. The PT then needs to verifies the vendor and class ID. Upon successful verification, the PT needs to send the download request to the edge-core using `download_asset` api. If the edge-core successfully downloaded the firmware, the PT receives the path of the downloaded binary, otherwise it gets the error code. The PT then needs to deregister the device and start the firmware update process. The PT then re-registers the device with the new firmware version (14/0/2) that it received from the `manifest_meta_data` API.

Please note that the manifest file you use must have matching vendor and class ID.

```
vendor:
    vendor-id: 5355424445564943452d56454e444f52
device:
    class-id: 5355424445564943452d2d434c415353
```

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
[Device Management Docs](https://developer.izumanetworks.com/docs/device-management-edge/latest/protocol-translator/index.html).

## simple-mgmt-example.js

This example management application demostrates the calls and parameters to pass
to Edge core management API. The websocket connection and JSONRPC 2.0
specification and communication is left out of the scope to keep the
example simple.

This application is interactive and supports few command that can be given
to control the API. See the usage on the example application startup or
by using `help()` function.

Libraries are used to handle the websocket and JSONRPC 2.0 communication.

Please study the example code to see how to use the management JSONRPC 2.0
API and read the relevant documentation for Edge APIs from
[Device Management Docs](https://developer.izumanetworks.com/docs/device-management-edge/latest/protocol-translator/index.html).

## simple-grm-example.js

This example gateway resource manager demonstrates the calls and parameters to pass to
Edge Core gateway resource management API. The `simple-grm-example.js` demonstrates the
basic resource manager functionality, ie. registering, adding resources and
updation. The websocket connection and JSONRPC 2.0 specification and communication
is left out of the scope to keep the example simple.

Libraries are used to handle the websocket and JSONRPC 2.0 communication.

Please study the example code to see how to use the gateway resource manager
JSONRPC 2.0 API and read the relevant documentation for Edge APIs from
[Device Management Docs](https://developer.izumanetworks.com/docs/device-management-edge/latest/protocol-translator/index.html).

## Dependencies

This example uses `node.js v8` or higher.

Install the dependencies:
```bash
npm install
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
npm ls
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
   node simple-pt-example.js
   ```
   Or, using docker
   ```
   docker build
   docker run -v /tmp:/tmp -it simple-pt-example:latest
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
   [Edge examples](https://github.com/PelionIoT/mbed-edge-examples).
1. Run this example and connect to Edge.
   ```bash
   nodejs simple-mgmt-example.js
   ```
1. Use the `connect()` function provided by the interactive example to connect
   to the Edge Core.
1. After successful connection you can query devices from Edge Core with `devices()` function.
1. See the example application help for other functions.

## Running the gateway resource manager example

Fixed values for the example:
 * Resource Manager name is `simple-grm-example`
 * The example LwM2M object `33001`has one instance with id `0`.
 * The object instance has two resources:
   * `33001/0/0` which is a readable resource of type `string`
   * `33001/0/1` which is a readable and writable resource of type `float`

1. Run the Edge Core
   See the pre-requisites to build and run from the root [README.md](./README.md)
1. Verify that Edge device is connected to Device Management and visible
   from [Device Management Portal](https://portal.mbedcloud.com)
1. Run this example and connect to Edge.
   ```bash
   nodejs simple-grm-example.js
   ```
1. Follow the command prompt to register resource manager, add gateway resources and update them.
1. Monitor the registered Edge and gateway resources from Device Management Portal.
1. Try writing a new float value to the resource `/33001/0/1`. A JSONRPC 2.0 packet will be received by the example.
