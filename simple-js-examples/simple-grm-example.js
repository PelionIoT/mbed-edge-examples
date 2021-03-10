/*
 * ----------------------------------------------------------------------------
 * Copyright 2020 ARM Ltd.
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

const util = require('util')

const JsonRpcWs = require('json-rpc-ws');

const RED    = '\x1b[31m[EdgeGRMExample]\x1b[0m';
const GREEN  = '\x1b[32m[EdgeGRMExample]\x1b[0m';
const YELLOW = '\x1b[33m[EdgeGRMExample]\x1b[0m';

// Timeout time in milliseconds
const TIMEOUT = 10000;

const OPERATIONS = {
    READ       : 0x01,
    WRITE      : 0x02,
    EXECUTE    : 0x04,
    DELETE     : 0x08
};

function EdgeGRMExample() {
    this.name = 'simple-grm-example';
    this.api_path = '/1/grm';
    this.socket_path = '/tmp/edge.sock';

    this.client = JsonRpcWs.createClient();
}

EdgeGRMExample.prototype.connect = async function() {
    let self = this;
    return new Promise((resolve, reject) => {
        let url = util.format('ws+unix://%s:%s',
                              this.socket_path,
                              this.api_path);
        console.log(GREEN, 'Connecting to "', url, '"');
        self.client.connect(url,
            function connected(error, reply) {
                if (!error) {
                    resolve(self);
                } else {
                    reject(error);
                }
            });
    });
};

EdgeGRMExample.prototype.disconnect = async function() {
    let self = this;
    return new Promise((resolve, reject) => {
        console.log(GREEN, 'Disconnecting from Edge.');
        self.client.disconnect((error, response) => {
            if (!error) {
                resolve(response);
            } else {
                reject(error);
            }
        });
    });
};

EdgeGRMExample.prototype.registerGatewayResourceManager = async function() {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('gw_resource_manager_register', { 'name': self.name },
            function(error, response) {
                clearTimeout(timeout);
                if (!error) {
                    // Connection ok. Set up to listen for write calls
                    // from Edge Core.
                    self.exposeWriteMethod();
                    resolve(response);
                } else {
                    reject(error);
                }
            });
    });
};

EdgeGRMExample.prototype._createResourceParams = function(value1, value2) {
    // Values are always Base64 encoded strings.
    let resource1 = Buffer.from(value1).toString('base64')

    let resource2 = Buffer.allocUnsafe(4);
    resource2.writeFloatBE(value2);
    resource2 = resource2.toString('base64');

    /* An IPSO/LwM2M Object is identified by a unique objectId
     * An Object can hold multiple instances with unique objectInstanceId.
     * An instance can hold mutiple resources with unique resourceId
     * A resource must be defined with:
       - operations allowed, 
       - type of value it holds, and 
       - the value to be set

     * Note: Object 1, 3, 14, 10252, 10255, 26241 and 35011 are reserved in edge-core.
    */
    params = {
        objects: [{
            objectId: 33001,
            objectInstances: [{
                objectInstanceId: 0,
                resources: [{
                    resourceId: 0,
                    resourceName: "Name",
                    operations: OPERATIONS.READ,
                    type: 'string',
                    value: resource1
                },{
                    resourceId: 1,
                    resourceName: "Example Value",
                    operations: OPERATIONS.READ | OPERATIONS.WRITE,
                    type: 'float',
                    value: resource2
                }]
            }]
        }]
    };
    return params;
}

EdgeGRMExample.prototype.addResource = async function() {
    let self = this;
    return new Promise((resolve, reject) => {

        params = self._createResourceParams("example-resource", 1.0);

        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('add_resource', params,
            function(error, response) {
                clearTimeout(timeout);
                if (!error) {
                    console.log(YELLOW, 'Created example resources - /33001/0/0 , /33001/0/1');
                    resolve(response);
                } else {
                    reject(error);
                }
            });
    });
}


EdgeGRMExample.prototype.updateResourceValue = async function() {
    let self = this;
    return new Promise((resolve, reject) => {

        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        let params = self._createResourceParams("example-resource", 2.0);


        self.client.send('write_resource_value', params,
            function(error, response) {
                clearTimeout(timeout);
                if (!error) {
                    resolve(response);
                } else {
                    reject(error);
                }
            });
    });
}

EdgeGRMExample.prototype.exposeWriteMethod = function() {
    let self = this;
    self.client.expose('write', (params, response) => {
        let valueBuff = new Buffer.from(params.value, 'base64');
        let value = valueBuff.toString('utf-8');
        let resourcePath = params.uri.objectId + '/' + params.uri.objectInstanceId
            + '/' + params.uri.resourceId;

        let operation = '';
        if (params.operation === OPERATIONS.WRITE) {
            operation = 'write';
        } else if (params.operation === OPERATIONS.EXECUTE) {
            operation = 'execute';
        } else {
            operation = 'unknown';
        }

        received = {
            resourcePath: resourcePath,
            operation: operation,
            value: value
        }
        console.log(GREEN, 'Received a write method with data:');
        console.log(received);
        console.log(GREEN, 'The raw received JSONRPC 2.0 params:');
        console.log(params);

        /* Always respond back to Edge, it is expecting
         * a success response to finish the write/execute action.
         * If an error is returned the value write is discarded
         * also in the Edge Core.
         */
        response(/* no error */ null, /* success */ 'ok');
    });
};

const holdProgress = async (message) => {
    process.stdin.setRawMode(true)
    console.log(YELLOW, util.format('\x1b[1m%s\x1b[0m', message));
    return new Promise(resolve => process.stdin.once('data', () => {
        process.stdin.setRawMode(false);
        resolve();
    }));
}

(async function() {
    try {
        edge = new EdgeGRMExample();

        // Set SIGINT handle
        let quitImmediately = false;
        let sigintHandler;
        process.on('SIGINT', sigintHandler = async function() {
            if (quitImmediately) process.exit(1);
            try {
                await edge.disconnect();
            } catch (ex) {}
            process.exit(1);
        });

        // For waiting user input for example progress
        await holdProgress('Press any key to connect Edge.');

        await edge.connect();
        console.log(GREEN, 'Connected to Edge');

        await holdProgress('Press any key to register as Gateway Resource Manager.');
        let response = await edge.registerGatewayResourceManager();
        console.log(GREEN, 'Registered as Gateway Resource Manager. Response:', response);

        await holdProgress('Press any key to add the example resources.');
        response = await edge.addResource();
        console.log(GREEN, 'Added the example resources. Response:', response);

        await holdProgress('Press any key to update example resource values.');
        response = await edge.updateResourceValue();
        console.log(GREEN, 'Updated the resource values. Response:', response);

        console.log(GREEN, 'Kill the example with Ctrl+C');
    } catch (ex) {
        try {
            console.error(RED, 'Error...', ex);
            await edge.disconnect();
            process.exit(1);
        } catch (err) {
            console.error(RED, 'Error on closing the Edge Core connection.', err);
            process.exit(1);
        }
    }
})();
