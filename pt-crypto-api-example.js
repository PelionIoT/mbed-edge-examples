/*
 * ----------------------------------------------------------------------------
 * Copyright 2018-2019 ARM Ltd.
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
const crypto = require('crypto')

const JsonRpcWs = require('json-rpc-ws');
const promisify = require('es6-promisify');

const RED    = '\x1b[31m[EdgePTExample]\x1b[0m';
const GREEN  = '\x1b[32m[EdgePTExample]\x1b[0m';
const YELLOW = '\x1b[33m[EdgePTExample]\x1b[0m';

// Timeout time in milliseconds
const TIMEOUT = 10000;

const OPERATIONS = {
    READ       : 0x01,
    WRITE      : 0x02,
    EXECUTE    : 0x04,
    DELETE     : 0x08
};

function EdgePTExample() {
    this.name = 'simple-pt-example';
    this.api_path = '/1/pt';
    this.socket_path = '/tmp/edge.sock';

    this.client = JsonRpcWs.createClient();
}

EdgePTExample.prototype.connect = async function() {
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

EdgePTExample.prototype.disconnect = async function() {
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

EdgePTExample.prototype.registerProtocolTranslator = async function() {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('protocol_translator_register', { 'name': self.name },
            function(error, response) {
                clearTimeout(timeout);
                if (!error) {
                    // Connection ok. Set up to listen for write calls
                    // from Edge Core.
                    self.exposeCertificateRenewalStatusMethod();
                    self.exposeCryptoCertResultMethod();
                    resolve(response);
                } else {
                    reject(error);
                }
            });
    });
};

EdgePTExample.prototype.addCertificateToList = async function(certificateName) {
    let self = this;
    return new Promise((resolve, reject) => {

        params = {
            certificates: [certificateName]
        }

        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('certificate_renewal_list_set', params,
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

EdgePTExample.prototype.renewCertificate = async function(certificateName) {
    let self = this;
    return new Promise((resolve, reject) => {

        params = {
            certificate: certificateName
        }

        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('renew_certificate', params,
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

EdgePTExample.prototype.exposeCertificateRenewalStatusMethod = function() {
    let self = this;
    self.client.expose('certificate_renewal_result', (params, response) => {
        console.log(GREEN, 'Received certificate renewal result');
        console.log(params);
        /* Always respond back to Edge, it is expecting
         * a success response to finish the certificate renewal notification request.
         */
        response(/* no error */ null, /* success */ 'ok');
    });
}

EdgePTExample.prototype.cryptoGetCertificate = async function(certificateName) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('crypto_get_certificate', {certificate: certificateName},
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

EdgePTExample.prototype.cryptoGetPublicKey = async function(keyName) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('crypto_get_public_key', {key: keyName},
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

EdgePTExample.prototype.cryptoGenerateRandom = async function(amount) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        self.client.send('crypto_generate_random', {size: 0},
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

EdgePTExample.prototype.cryptoAsymmetricSign = async function(keyName, data) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        hash = crypto.createHash('sha256');
        hash.update(data);
        hashData = hash.digest('base64')

        self.client.send('crypto_asymmetric_sign', {private_key_name: keyName, hash_digest: hashData},
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

EdgePTExample.prototype.cryptoAsymmetricVerify = async function(keyName, data, signature) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        hash = crypto.createHash('sha256');
        hash.update(data);
        hashData = hash.digest('base64')
        signatureData = signature.toString('base64')

        self.client.send('crypto_asymmetric_verify', {public_key_name: keyName, hash_digest: hashData, signature: signatureData},
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

EdgePTExample.prototype.cryptoECDHKeyAgreement = async function(keyName, peerPublicKey) {
    let self = this;
    return new Promise((resolve, reject) => {
        let timeout = setTimeout(() => {
            reject('Timeout');
        }, TIMEOUT);

        peerKey = peerPublicKey.toString('base64')

        self.client.send('crypto_ecdh_key_agreement', {private_key_name: keyName, peer_public_key: peerKey},
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

EdgePTExample.prototype.exposeCryptoCertResultMethod = function() {
    let self = this;
    self.client.expose('crypto_get_certificate_result', (params, response) => {
        console.log(GREEN, 'Received a write method with data:');
        console.log(params);
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
        edge = new EdgePTExample();

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

        await holdProgress('Press any key to register as protocol translator.');
        let response = await edge.registerProtocolTranslator();
        console.log(GREEN, 'Registered as protocol translator. Response:', response);

        await holdProgress('Press any key to get certificate from edge.');
        response = await edge.cryptoGetCertificate('DLMS');
        console.log(GREEN, 'Get certificate response:', response);

        await holdProgress('Press any key to get public key from edge.');
        response = await edge.cryptoGetPublicKey('DLMS');
        console.log(GREEN, 'Get public key response:', response);
        // Store public key to variable to later use for asymmetric api examples
        peerPublicKey = response.key_data

        await holdProgress('Press any key to add certificate to certificate renewal list.');
        response = await edge.addCertificateToList('DLMS');
        console.log(GREEN, 'Added certificate to list. Response:', response);

        await holdProgress('Press any key to perform certificate renewal. Note: only works if DLMS certificate ' +
                           'exists in Edge.');
        response = await edge.renewCertificate('DLMS');
        console.log(GREEN, 'Performed certificate renewal. Response:', response);

        await holdProgress('Press any key to generate and retrieve a random buffer from edge.');
        response = await edge.cryptoGenerateRandom(32);
        console.log(GREEN, 'Generate random response:', response);

        await holdProgress('Press any key to perform asymmetric sign operation on edge.');
        response = await edge.cryptoAsymmetricSign('DLMS', 'hashdata');
        console.log(GREEN, 'Asymmetric sign response:', response);
        signatureData = response.signature_data

        await holdProgress('Press any key to perform asymmetric verify operation on edge.');
        response = await edge.cryptoAsymmetricVerify('DLMS', 'hashdata', signatureData);
        console.log(GREEN, 'Asymmetric verify response:', response);

        await holdProgress('Press any key to perform ECDH key agreement operation on edge.');
        response = await edge.cryptoECDHKeyAgreement('DLMS', peerPublicKey);
        console.log(GREEN, 'ECDH key agreement response:', response);

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
