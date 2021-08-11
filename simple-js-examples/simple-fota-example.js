/*
 * ----------------------------------------------------------------------------
 * Copyright 2021 Pelion Ltd.
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

const util = require("util");

const JsonRpcWs = require("json-rpc-ws");
const promisify = require("es6-promisify");

const RED = "\x1b[31m[EdgePTExample]\x1b[0m";
const GREEN = "\x1b[32m[EdgePTExample]\x1b[0m";
const YELLOW = "\x1b[33m[EdgePTExample]\x1b[0m";
// example vendor id and class id
const VENDORID = Buffer.from([0x53, 0x55, 0x42, 0x44, 0x45, 0x56, 0x49, 0x43, 0x45, 0x2d, 0x56, 0x45, 0x4e, 0x44, 0x4f, 0x52]);
const CLASSID = Buffer.from([0x53, 0x55, 0x42, 0x44, 0x45, 0x56, 0x49, 0x43, 0x45, 0x2d, 0x2d, 0x43, 0x4c, 0x41, 0x53, 0x53]);
// this example is using manifest version 4 therefore this should be 4
const PROT_VERSION = 4;
// Timeout time in milliseconds
const TIMEOUT = 10000;
const TIMEOUT_FOR_FOTA = 1800000; // 30 mins.

const OPERATIONS = {
	READ: 0x01,
	WRITE: 0x02,
	EXECUTE: 0x04,
	DELETE: 0x08,
};
const DEVICE_ID = "example-fota-device"

function EdgePTExample() {
	this.name = "simple-pt-example-fota";
	this.api_path = "/1/pt";
	this.socket_path = "/tmp/edge.sock";
	this.client = JsonRpcWs.createClient();
}

EdgePTExample.prototype.connect = async function () {
	let self = this;
	return new Promise((resolve, reject) => {
		let url = util.format("ws+unix://%s:%s", this.socket_path, this.api_path);
		console.log(GREEN, 'Connecting to "', url, '"');
		self.client.connect(url, function connected(error, reply) {
			if (!error) {
				resolve(self);
			} else {
				reject(error);
			}
		});
	});
};

EdgePTExample.prototype.disconnect = async function () {
	let self = this;
	return new Promise((resolve, reject) => {
		console.log(GREEN, "Disconnecting from Edge.");
		self.client.disconnect((error, response) => {
			if (!error) {
				resolve(response);
			} else {
				reject(error);
			}
		});
	});
};

EdgePTExample.prototype.exposeWriteMethod = function() {
		let self = this;
		self.client.expose('write', (params, response) => {
				let value = new Buffer.from(params.value, 'base64').readDoubleBE();
				let resourcePath = params.uri.objectId + '/' + params.uri.objectInstanceId
						+ '/' + params.uri.resourceId;
				let deviceId = params.uri.deviceId;
				let operation = '';
				if (params.operation === OPERATIONS.WRITE) {
						operation = 'write';
				} else if (params.operation === OPERATIONS.EXECUTE) {
						operation = 'execute';
				} else {
						operation = 'unknown';
				}

				received = {
						deviceId: deviceId,
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

EdgePTExample.prototype.registerProtocolTranslator = async function () {
	let self = this;
	return new Promise((resolve, reject) => {
		let timeout = setTimeout(() => {
			reject("Timeout");
		}, TIMEOUT);

		self.client.send(
			"protocol_translator_register",
			{ name: self.name },
			function (error, response) {
				clearTimeout(timeout);
				if (!error) {
					// Connection ok. Set up to listen for write calls
					// from Edge Core.
				self.exposeVendorandClass();
				self.exposeWriteMethod();
					resolve(response);
				} else {
					reject(error);
				}
			}
		);
	});
};

EdgePTExample.prototype._createDeviceParams = function (
	deviceId,
	temperatureValue,
	setPointValue,
	version
) {
	// Values are always Base64 encoded strings.
	let temperature = Buffer.allocUnsafe(8);
	temperature.writeDoubleBE(temperatureValue);
	temperature = temperature.toString("base64");
	let setPoint = Buffer.allocUnsafe(8);
	setPoint.writeDoubleBE(setPointValue);
	setPoint = setPoint.toString("base64");
	let data = "0";
	let buff = new Buffer.from(data);
	let base64data = buff.toString("base64");

	let value4 = Buffer.alloc(4);
	value4.writeInt32BE(PROT_VERSION);
	let fourvalue = value4.toString('base64');
	let base64vendor = VENDORID.toString("base64")
	let base64class = CLASSID.toString("base64")
	// component name, this can be main
	let main_buff = new Buffer.from("MAIN");
	let main_base64 = main_buff.toString("base64")
	let ver_buff = new Buffer.from(version);
	let ver_base64 = ver_buff.toString("base64")
	let value = Buffer.alloc(4);
	value.writeInt32BE(version);
	let minusone = value.toString('base64');

	// An IPSO/LwM2M temperature sensor and set point sensor (thermostat)
	params = {
		deviceId: deviceId,
		objects: [
			{
				objectId: 14,
				objectInstances: [
					{
						objectInstanceId: 0,
						resources: [
							{
								resourceId: 0,
								operations: OPERATIONS.READ,
								resourceName: "Component Identity",
								type: "string",
								value: main_base64,
							},
							{
								resourceId: 2,
								operations: OPERATIONS.READ,
								resourceName: "Component Version",
								type: "string",
								value: ver_base64,
							},
						],
					},
				],
			},
			{
				objectId: 3303,
				objectInstances: [
					{
						objectInstanceId: 0,
						resources: [
							{
								resourceId: 5700,
								operations: OPERATIONS.READ,
								type: "float",
								value: temperature,
							},
						],
					},
				],
			},
			{
				objectId: 10252,
				objectInstances: [
					{
						objectInstanceId: 0,
						resources: [
							{
								resourceId: 1,
								operations: OPERATIONS.EXECUTE,
								type: "string",
								value: base64data,
							},
							{
								resourceId: 2,
								operations: OPERATIONS.READ | OPERATIONS.WRITE,
								type: "int",
								value: minusone,
							},
							{
								resourceId: 3,
								operations: OPERATIONS.READ | OPERATIONS.WRITE,
								type: "int",
								value: minusone,
							}
						],
					},
				],
			},
			{
				objectId: 10255,
				objectInstances: [
					{
						objectInstanceId: 0,
						resources: [
							{
								resourceId: 0,
								operations: OPERATIONS.READ | OPERATIONS.WRITE,
								type: "int",
								value: fourvalue,
							},
							{
								resourceId: 1,
								operations: OPERATIONS.READ | OPERATIONS.WRITE,
								type: "string",
								value: base64data,
							},
							{
								resourceId: 2,
								operations: OPERATIONS.READ | OPERATIONS.WRITE,
								type: "string",
								value: base64data,
							},
							{
								resourceId: 3,
								operations: OPERATIONS.READ | OPERATIONS.WRITE,
								type: "string",
								value: base64vendor,
							},
							{
								resourceId: 4,
								operations: OPERATIONS.READ | OPERATIONS.WRITE,
								type: "string",
								value: base64class,
							},
						],
					},
				],
			},
			{
			objectId: 3308,
			objectInstances: [
				{
					objectInstanceId: 0,
					resources: [
						{
							resourceId: 5900,
							operations: OPERATIONS.READ | OPERATIONS.WRITE,
							type: "float",
							value: setPoint,
						},
					],
				},
				],
			},
		],
	};
	return params;
};

EdgePTExample.prototype.registerExampleDevice = async function (deviceId, versionval) {
	let self = this;
	return new Promise((resolve, reject) => {
		params = self._createDeviceParams(
			deviceId,
			21.5 /* temp */,
			23.5 /* set point */,
			versionval
		);

		let timeout = setTimeout(() => {
			reject("Timeout");
		}, TIMEOUT);

		self.client.send("device_register", params, function (error, response) {
			clearTimeout(timeout);
			if (!error) {
				resolve(response);
			} else {
				reject(error);
			}
		});
	});
};

EdgePTExample.prototype.unregisterExampleDevice = async function (deviceId) {
	let self = this;
	return new Promise((resolve, reject) => {
		let timeout = setTimeout(() => {
			reject("Timeout");
		}, TIMEOUT);

		self.client.send(
			"device_unregister",
			{ deviceId: deviceId },
			function (error, response) {
				clearTimeout(timeout);
				if (!error) {
					resolve(response);
				} else {
					reject(error);
				}
			}
		);
	});
};

EdgePTExample.prototype.updateExampleDeviceResources = async function (
	deviceId
) {
	let self = this;
	return new Promise((resolve, reject) => {
		params = self._createDeviceParams(
			deviceId,
			19.5 /* temp */,
			20.5 /* set point */,
			"0.0.0",
		);

		let timeout = setTimeout(() => {
			reject("Timeout");
		}, TIMEOUT);

		self.client.send("write", params, function (error, response) {
			clearTimeout(timeout);
			if (!error) {
				resolve(response);
			} else {
				reject(error);
			}
		});
	});
};

// updating the manifest resources after the firmware update.
EdgePTExample.prototype.updateExampleManifestResources = async function (
	deviceId,
	fw_version
	) {
	let self = this;
	return new Promise((resolve, reject) => {
		params = self._createDeviceParams(deviceId,19.5,20.5,fw_version)
		let timeout = setTimeout(() => {
		reject("Timeout");
		}, TIMEOUT);
	
		self.client.send("write", params, function (error, response) {
		clearTimeout(timeout);
		if (!error) {
			resolve(response);
		} else {
			reject(error);
		}
		});
	});
	};

EdgePTExample.prototype.exposeVendorandClass = function () {
	let self = this;
	self.client.expose("manifest_meta_data", (params, response) => {
		// checking the request
		if (params.uri.deviceId == null || params.classid == null || params.vendorid == null || params.component_name == null || params.version == null ) {
			console.log(RED,"Missing any of the filed in params: classid, deviceId, vendorid , url, version, component_name");
			response({
			"code": -32602,
			"data": "Missing any of the filed in params: classid, deviceId, vendorid, version, component name",
			"message": "Missing any of the filed in params: classid, deviceId, vendorid, version, component name"
			},  null);
			return;
		}
		console.log(GREEN, "received fota request")
		console.log(params)
		var fw_size = params.size
		var classid = new Buffer.from(params.classid, "base64");
		var vendorid = new Buffer.from(params.vendorid, "base64");
		var b_64version = params.version
		var data = new Buffer.from(b_64version, "base64");
		var fw_version = data.toString("ascii")
		var deviceid = params.uri.deviceId;
		var send_data = {
			deviceId: deviceid,
			size: fw_size
		}
		// checking vendor and class id from manifest is equal or not.
		if((Buffer.compare(vendorid, VENDORID)!= 0) && (Buffer.compare(classid, CLASSID)!= 0)) {
			response({
				"code": -32602,
				"data": "wrong vendor or class ID",
				"message": "wrong vendor or class ID"
			}, /* success */ null);
		}
		else {
			response( null,'ok');
			let timeout = setTimeout(() => {
							reject("Timeout");
							}, TIMEOUT_FOR_FOTA);

			self.client.send("download_asset", send_data ,function (error, response) {
				clearTimeout(timeout);
				if (!error) {
					console.log(GREEN,"Updating Device, Firmware file location "+response.filename)
					self.unregisterExampleDevice(DEVICE_ID).then((a) => {
						console.log(GREEN,"Rebooting device.")
						setTimeout(()=> {
							self.updateExampleManifestResources(DEVICE_ID,fw_version).then(()=>{
								console.log(GREEN,"Manifest Resource updated, Device is updated")
								console.log(YELLOW, "Press any key to unregister the example device")
							}).catch((error)=>{
								console.log(RED,error)
							});
						},5000)
					}).catch((error) => {
						console.log(error)
						});
					} 
				else {
					console.log(RED,"Error ",error);
				}
			});

		}
	 });
};

const holdProgress = async (message) => {
	process.stdin.setRawMode(true);
	console.log(YELLOW, util.format("\x1b[1m%s\x1b[0m", message));
	return new Promise((resolve) =>
		process.stdin.once("data", () => {
			process.stdin.setRawMode(false);
			resolve();
		})
	);
};

(async function () {
	try {
		edge = new EdgePTExample();
		// Set SIGINT handle
		let quitImmediately = false;
		let sigintHandler;
		process.on(
			"SIGINT",
			(sigintHandler = async function () {
				if (quitImmediately) process.exit(1);
				try {
					await edge.disconnect();
				} catch (ex) {}
				process.exit(1);
			})
		);

		// For waiting user input for example progress
		await holdProgress("Press any key to connect Edge.");
		await edge.connect();
		console.log(GREEN, "Connected to Edge");
		await holdProgress("Press any key to register as protocol translator.");
		let response = await edge.registerProtocolTranslator();
		console.log(
			GREEN,
			"Registered as protocol translator. Response:",
			response
		);
		await holdProgress("Press any key to register the example device.");
		response = await edge.registerExampleDevice(DEVICE_ID,"0.0.0");
		console.log(GREEN, "Registered an example device. Response:", response);

		await holdProgress("Press any key to update example device values.");
		response = await edge.updateExampleDeviceResources(DEVICE_ID);
		console.log(GREEN, "Updated the resource values. Response:", response);

		await holdProgress("Press any key to unregister the example device.");
		response = await edge.unregisterExampleDevice(DEVICE_ID);
		console.log(GREEN, "Example device unregistered. Response:", response);

		console.log(GREEN, "Kill the example with Ctrl+C");
	} catch (ex) {
		try {
			console.error(RED, "Error...", ex);
			await edge.disconnect();
			process.exit(1);
		} catch (err) {
			console.error(RED, "Error on closing the Edge Core connection.", err);
			process.exit(1);
		}
	}
})();
