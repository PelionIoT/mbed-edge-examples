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
/**
 * \file main.c
 * \brief BLE protocol translator for Mbed Edge
 */
#include "devices.h"
#include "blept_example_clip.h"
#include "pt_edge.h"
#include "pt_ble.h"

#include <mbed-trace/mbed_trace.h>
#include "common/edge_trace.h"
#include <stdio.h>

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <edge_examples_version_info.h>

// ============================================================================
// Enums, Structs and Defines
// ============================================================================
#define TRACE_GROUP "btpt"

// ============================================================================
// Global Variables
// ============================================================================
/**
 * \brief Flag for the protocol translator run state
 *
 * 0 if the protocol translator main loop should terminate.\n
 * 1 if the protocol translator main loop should continue.
 */
volatile int global_keep_running = 0;

// ============================================================================
// Code
// ============================================================================

/**
 * \brief Main entry point to the mept-ble application.
 *
 * Mandatory arguments:
 * \li Protocol translator name
 *
 * Optional arguments:
 * \li Endpoint postfix to indicate the running mept-ble in device names.
 * \li Edge domain socket path if default path is not used.
 *
 * Starts the protocol translator client and registers the connection ready callback handler.
 *
 * \param argc The number of the command line arguments.
 * \param argv The array of the command line arguments.
 */
int main(int argc, char **argv)
{
    protocol_translator_api_start_ctx_t ctx;
    int service_based_discovery = 1;
    DocoptArgs args = docopt(argc, argv, /* help */ 1, /* version */ "0.1");
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    edge_trace_init(args.color_log);

    if (!args.protocol_translator_name) {
        fprintf(stderr, "The --protocol-translator-name parameter is mandatory. Please see --help\n");
        return 1;
    }

    tr_info("Starting mept-ble MbedEdge Protocol Translator for BLE");
    tr_info("Version: %s", VERSION_STRING);
    tr_info("Binary built at: " __DATE__ " " __TIME__);
    tr_info("Main thread id is %lx", pthread_self());

    global_keep_running = 1;

    devices_init();

    /* Setup signal handler to catch SIGINT for shutdown */
    if (!pt_ble_setup_signals()) {
        tr_err("Failed to setup signals.");
        return 1;
    }

    ctx.name = args.protocol_translator_name;
    ctx.socket_path = args.edge_domain_socket;

    start_protocol_translator_api(&ctx);
    if (args.extended_discovery_file) {
        service_based_discovery = 0;
    }

    int ret_val = ble_start(args.endpoint_postfix,
                            args.bluetooth_interface,
                            args.address,
                            args.clear_cache,
                            args.extended_discovery_file,
                            service_based_discovery);
    if (0 != ret_val) {
        tr_err("ble_start returned error code %d", ret_val);
    }

    tr_info("pt_client_shutdown");
    stop_protocol_translator_api();

    tr_info("Main thread waiting for protocol translator api to stop.");
    /* Note: to avoid a leak, we should join the created thread from
     * the same thread it was created from. */
    stop_protocol_translator_api_thread();

    return 0;
}
