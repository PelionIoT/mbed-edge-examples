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

void shutdown_and_cleanup()
{
    tr_info("starting shutdown");
    g_idle_add(pt_ble_graceful_shutdown, NULL);
}

/**
 * \brief The client shutdown handler.
 *
 * \param signum The signal number that initiated the shutdown handler.
 */
static void shutdown_handler(int signum)
{
    (void)signum;
    tr_info("Shutdown handler from signal %d", signum);

    shutdown_and_cleanup();
}
/**
 * \brief Set up the signal handler for catching signals from OS.
 * This signal handler setup catches SIGTERM and SIGINT for shutting down
 * the protocol translator client gracefully.
 */
static bool setup_signals(void)
{
    struct sigaction sa = { .sa_handler = shutdown_handler, };
    struct sigaction sa_pipe = { .sa_handler = SIG_IGN, };
    int ret_val;

    if (sigemptyset(&sa.sa_mask) != 0) {
        return false;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return false;
    }
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return false;
    }
    ret_val = sigaction(SIGPIPE, &sa_pipe, NULL);
    if (ret_val != 0) {
        tr_warn("setup_signals: sigaction with SIGPIPE returned error=(%d) errno=(%d) strerror=(%s)",
                ret_val,
                errno,
                strerror(errno));
    }
#ifdef DEBUG
    tr_info("Setting support for SIGUSR2");
    if (sigaction(SIGUSR2, &sa, NULL) != 0) {
        return false;
    }
#endif
    return true;
}

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
    DocoptArgs args = docopt(argc, argv, /* help */ 1, /* version */ "0.1");
    edge_trace_init(args.color_log);

    if (!args.protocol_translator_name) {
        fprintf(stderr, "The --protocol-translator-name parameter is mandatory. Please see --help\n");
        return 1;
    }

    tr_info("Starting mept-ble MbedEdge Protocol Translator for BLE");
    tr_info("Main thread id is %lx", pthread_self());

    global_keep_running = 1;

    devices_init();

    /* Setup signal handler to catch SIGINT for shutdown */
    if (!setup_signals()) {
        tr_err("Failed to setup signals.");
        return 1;
    }

    ctx.name = args.protocol_translator_name;
    ctx.socket_path = args.edge_domain_socket;

    start_protocol_translator_api(&ctx);
    ble_start(args.endpoint_postfix, args.bluetooth_interface, args.address, args.clear_cache, args.extended_discovery_mode);

    tr_info("pt_client_shutdown");
    stop_protocol_translator_api();

    tr_info("devices_free");
    devices_free();

    tr_info("Main thread waiting for protocol translator api to stop.");
    /* Note: to avoid a leak, we should join the created thread from
     * the same thread it was created from. */
    stop_protocol_translator_api_thread();

    return 0;
}
