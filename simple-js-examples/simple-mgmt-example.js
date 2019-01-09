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

const util = require('util')

const JsonRpcWs = require('json-rpc-ws');
const promisify = require('es6-promisify');
const repl = require('repl')

const RED    = '\x1b[31m[EdgeMgmtExample]\x1b[0m';
const GREEN  = '\x1b[32m[EdgeMgMtExample]\x1b[0m';
const YELLOW = '\x1b[33m[EdgeMgMtExample]\x1b[0m';

const COLOR_CYAN = '\x1b[36m'
const COLOR_END = '\x1b[0m'

// Timeout time in milliseconds
const TIMEOUT = 10000;

const OPERATIONS = {
    READ       : 0x01,
    WRITE      : 0x02,
    EXECUTE    : 0x04,
    DELETE     : 0x08
};

function EdgeMgmtExample() {
    this.client = JsonRpcWs.createClient();
}

EdgeMgmtExample.prototype.connect = async function(apiPath='/1/mgmt',
                                                   socketPath='/tmp/edge.sock') {
    let self = this;
    return new Promise((resolve, reject) => {
        let url = util.format('ws+unix://%s:%s',
                              socketPath,
                              apiPath);
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

EdgeMgmtExample.prototype.disconnect = async function() {
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

EdgeMgmtExample.prototype.devices = async function() {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('devices', {},
            function(error, response) {
                clearTimeout(timeout);
                if (!error) {
                    resolve(response);
                } else {
                    reject(error);
                }
            });
    });
};

EdgeMgmtExample.prototype.readResource = async function(endpointName, uri) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('read_resource',
                         { 'endpointName': endpointName,
                           'uri': uri },
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

EdgeMgmtExample.prototype.writeResource = async function(endpointName, uri, base64Value) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('write_resource',
                         { 'endpointName': endpointName,
                           'uri': uri,
                           'base64Value': base64Value
                         },
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

function help() {
    console.log(COLOR_CYAN);
    console.log('Management API commands: connect, devices, readResource, exit and help.');
    console.log('Example usage:');
    console.log('  Function `connect(apiPath="/1/mgmt", socketPath="/tmp/edge.sock")`.');
    console.log('  Function `devices()`.');
    console.log('  Function `readResource(endpointName, resourceURI)`. Example: `readResource("thermometer-0", "/3303/0/5700")`.');
    console.log('  Function `writeResource(endpointName, resourceURI, base64Value)`. Example: `writeResource("thermostat-0", "/3308/0/5900", "QEcHSP//kFU=")`.');
    console.log('  Function `exit()`');
    console.log('  Function `help()`');
    console.log(COLOR_END);
    return 1;
}


const runPrompt = async (edge) => {
    // Print help on startup
    help();

    return new Promise(resolve => {
        let replServer = repl.start({
            prompt: GREEN + COLOR_CYAN + ' <mgmt>$ ' + COLOR_END
        });
        require('repl.history')(replServer, '.repl_history');

        replServer.context.connect = async function(apiPath, socketPath) {
            edge.connect(apiPath, socketPath)
                .then(function(response) {
                    console.log(GREEN, 'Connected to Edge');
                }, function (reject) {
                    console.log(RED, 'Error: ', reject);
                });
        };

        replServer.context.devices = async function() {
            edge.devices()
                .then(function(response) {
                    console.log(GREEN, 'Device list query response:', JSON.stringify(response, null, 2));
                }, function(reject) {
                    console.log(RED, 'Error: ', reject);
                });
        };

        replServer.context.readResource = function(endpointName, uri) {
            edge.readResource(endpointName, uri)
                .then(function(response) {
                    console.log(GREEN, 'Read resource response:', JSON.stringify(response, null, 2));
                }, function(reject) {
                    console.log(RED, 'Error: ', reject);
                });
        };

        replServer.context.writeResource = function(endpointName, uri, base64Value) {
            edge.writeResource(endpointName, uri, base64Value)
                .then(function(response) {
                    console.log(GREEN, 'Write resource response:', JSON.stringify(response, null, 2));
                }, function(reject) {
                    console.log(RED, 'Error: ', reject);
                });
        };

        replServer.context.help = help;

        replServer.context.exit = function() {
            resolve();
            return 'ok';
        };

        replServer.on('exit', () => {
            resolve();
        });
    });
}

(async function() {
    try {
        edge = new EdgeMgmtExample();
        await runPrompt(edge);
        console.log(GREEN, 'Exiting...');

        try {
            await edge.disconnect();
        } catch (err) {}
        process.exit(1);

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
