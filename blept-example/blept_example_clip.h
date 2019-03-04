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

#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cstring>
#else
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#endif


typedef struct {
    /* options without arguments */
    int clear_cache;
    int color_log;
    int help;
    /* options with arguments */
    char *address;
    char *bluetooth_interface;
    char *edge_domain_socket;
    char *endpoint_postfix;
    char *extended_discovery_file;
    char *protocol_translator_name;
    /* special */
    const char *usage_pattern;
    const char *help_message;
} DocoptArgs;

const char help_message[] =
"BLE Protocol Translator Example.\n"
"\n"
"Usage:\n"
"  blept-example --protocol-translator-name <name> [--endpoint-postfix <name>] [--edge-domain-socket <domain-socket>] [--color-log] [--bluetooth-interface <bluetooth-interface>] [--address <dbus-address>] [--clear-cache] [--extended-discovery-file <string>]\n"
"  blept-example --help\n"
"\n"
"Options:\n"
"  -h --help                                  Show this screen.\n"
"  -n --protocol-translator-name <name>       Name of the Protocol Translator.\n"
"  -e --endpoint-postfix <postfix>            Name for the endpoint postfix [default: -0]\n"
"  --edge-domain-socket <string>              Edge Core domain socket path [default: /tmp/edge.sock].\n"
"  --color-log                                Use ANSI colors in log.\n"
"  -b --bluetooth-interface <string>          HCI transport interface [default: hci0].\n"
"  -a --address <string>                      DBus server address [default: unix:path=/var/run/dbus/system_bus_socket].\n"
"  -c --clear-cache                           Clear BlueZ device cache before starting active scan.\n"
"  -d --extended-discovery-file <string>      Path to extended discovery configuration file. When using this option, BLE Protocol Translator Example\n"
"                                             connects to devices based on configuration in this file. Currently it supports `whitelisted-devices`\n"
"                                             list. Each entry in the list contains a match string `name`. It may be a full match or partial match,\n"
"                                             specified by the `partial-match` name-value. If partial match is used, a substring in the `name` value\n"
"                                             is enough to be able to connect the device. Otherwise the name needs to match exactly to connect the\n"
"                                             device. The file is in json format.\n"
"                                             Example: '{\"whitelisted-devices\":[{\"name\":\"Thunder Sense\", \"partial-match\" : 1}]}'\n"
"                                             Note: using extended discovery mode disables the default mode to discover devices based on supported advertised services.\n"
"";

const char usage_pattern[] =
"Usage:\n"
"  blept-example --protocol-translator-name <name> [--endpoint-postfix <name>] [--edge-domain-socket <domain-socket>] [--color-log] [--bluetooth-interface <bluetooth-interface>] [--address <dbus-address>] [--clear-cache] [--extended-discovery-file <string>]\n"
"  blept-example --help";

typedef struct {
    const char *name;
    bool value;
} Command;

typedef struct {
    const char *name;
    char *value;
    char **array;
} Argument;

typedef struct {
    const char *oshort;
    const char *olong;
    bool argcount;
    bool value;
    char *argument;
} Option;

typedef struct {
    int n_commands;
    int n_arguments;
    int n_options;
    Command *commands;
    Argument *arguments;
    Option *options;
} Elements;


/*
 * Tokens object
 */

typedef struct Tokens {
    int argc;
    char **argv;
    int i;
    char *current;
} Tokens;

Tokens tokens_new(int argc, char **argv) {
    Tokens ts = {argc, argv, 0, argv[0]};
    return ts;
}

Tokens* tokens_move(Tokens *ts) {
    if (ts->i < ts->argc) {
        ts->current = ts->argv[++ts->i];
    }
    if (ts->i == ts->argc) {
        ts->current = NULL;
    }
    return ts;
}


/*
 * ARGV parsing functions
 */

int parse_doubledash(Tokens *ts, Elements *elements) {
    //int n_commands = elements->n_commands;
    //int n_arguments = elements->n_arguments;
    //Command *commands = elements->commands;
    //Argument *arguments = elements->arguments;

    // not implemented yet
    // return parsed + [Argument(None, v) for v in tokens]
    return 0;
}

int parse_long(Tokens *ts, Elements *elements) {
    int i;
    int len_prefix;
    int n_options = elements->n_options;
    char *eq = strchr(ts->current, '=');
    Option *option = NULL;
    Option *options = elements->options;

    len_prefix = (eq-(ts->current))/sizeof(char);
    for (i=0; i < n_options; i++) {
        option = &options[i];
        if (!strncmp(ts->current, option->olong, len_prefix))
            break;
    }
    if ((i == n_options) || (option == NULL)) {
        // TODO '%s is not a unique prefix
        fprintf(stderr, "%s is not recognized\n", ts->current);
        return 1;
    }
    tokens_move(ts);
    if (option->argcount) {
        if (eq == NULL) {
            if (ts->current == NULL) {
                fprintf(stderr, "%s requires argument\n", option->olong);
                return 1;
            }
            option->argument = ts->current;
            tokens_move(ts);
        } else {
            option->argument = eq + 1;
        }
    } else {
        if (eq != NULL) {
            fprintf(stderr, "%s must not have an argument\n", option->olong);
            return 1;
        }
        option->value = true;
    }
    return 0;
}

int parse_shorts(Tokens *ts, Elements *elements) {
    char *raw;
    int i;
    int n_options = elements->n_options;
    Option *option = NULL;
    Option *options = elements->options;

    raw = &ts->current[1];
    tokens_move(ts);
    while (raw[0] != '\0') {
        for (i=0; i < n_options; i++) {
            option = &options[i];
            if (option->oshort != NULL && option->oshort[1] == raw[0])
                break;
        }
        if ((i == n_options) || (option == NULL)) {
            // TODO -%s is specified ambiguously %d times
            fprintf(stderr, "-%c is not recognized\n", raw[0]);
            return 1;
        }
        raw++;
        if (!option->argcount) {
            option->value = true;
        } else {
            if (raw[0] == '\0') {
                if (ts->current == NULL) {
                    fprintf(stderr, "%s requires argument\n", option->oshort);
                    return 1;
                }
                raw = ts->current;
                tokens_move(ts);
            }
            option->argument = raw;
            break;
        }
    }
    return 0;
}

int parse_argcmd(Tokens *ts, Elements *elements) {
    int i;
    int n_commands = elements->n_commands;
    //int n_arguments = elements->n_arguments;
    Command *command;
    Command *commands = elements->commands;
    //Argument *arguments = elements->arguments;

    for (i=0; i < n_commands; i++) {
        command = &commands[i];
        if (!strcmp(command->name, ts->current)){
            command->value = true;
            tokens_move(ts);
            return 0;
        }
    }
    // not implemented yet, just skip for now
    // parsed.append(Argument(None, tokens.move()))
    /*fprintf(stderr, "! argument '%s' has been ignored\n", ts->current);
    fprintf(stderr, "  '");
    for (i=0; i<ts->argc ; i++)
        fprintf(stderr, "%s ", ts->argv[i]);
    fprintf(stderr, "'\n");*/
    tokens_move(ts);
    return 0;
}

int parse_args(Tokens *ts, Elements *elements) {
    int ret;

    while (ts->current != NULL) {
        if (strcmp(ts->current, "--") == 0) {
            ret = parse_doubledash(ts, elements);
            if (!ret) break;
        } else if (ts->current[0] == '-' && ts->current[1] == '-') {
            ret = parse_long(ts, elements);
        } else if (ts->current[0] == '-' && ts->current[1] != '\0') {
            ret = parse_shorts(ts, elements);
        } else
            ret = parse_argcmd(ts, elements);
        if (ret) return ret;
    }
    return 0;
}

int elems_to_args(Elements *elements, DocoptArgs *args, bool help,
                  const char *version){
    Command *command;
    Argument *argument;
    Option *option;
    int i;

    // fix gcc-related compiler warnings (unused)
    (void)command;
    (void)argument;

    /* options */
    for (i=0; i < elements->n_options; i++) {
        option = &elements->options[i];
        if (help && option->value && !strcmp(option->olong, "--help")) {
            printf("%s", args->help_message);
            return 1;
        } else if (version && option->value &&
                   !strcmp(option->olong, "--version")) {
            printf("%s\n", version);
            return 1;
        } else if (!strcmp(option->olong, "--clear-cache")) {
            args->clear_cache = option->value;
        } else if (!strcmp(option->olong, "--color-log")) {
            args->color_log = option->value;
        } else if (!strcmp(option->olong, "--help")) {
            args->help = option->value;
        } else if (!strcmp(option->olong, "--address")) {
            if (option->argument)
                args->address = option->argument;
        } else if (!strcmp(option->olong, "--bluetooth-interface")) {
            if (option->argument)
                args->bluetooth_interface = option->argument;
        } else if (!strcmp(option->olong, "--edge-domain-socket")) {
            if (option->argument)
                args->edge_domain_socket = option->argument;
        } else if (!strcmp(option->olong, "--endpoint-postfix")) {
            if (option->argument)
                args->endpoint_postfix = option->argument;
        } else if (!strcmp(option->olong, "--extended-discovery-file")) {
            if (option->argument)
                args->extended_discovery_file = option->argument;
        } else if (!strcmp(option->olong, "--protocol-translator-name")) {
            if (option->argument)
                args->protocol_translator_name = option->argument;
        }
    }
    /* commands */
    for (i=0; i < elements->n_commands; i++) {
        command = &elements->commands[i];
    }
    /* arguments */
    for (i=0; i < elements->n_arguments; i++) {
        argument = &elements->arguments[i];
    }
    return 0;
}


/*
 * Main docopt function
 */

DocoptArgs docopt(int argc, char *argv[], bool help, const char *version) {
    DocoptArgs args = {
        0, 0, 0, (char*) "unix:path=/var/run/dbus/system_bus_socket", (char*)
        "hci0", (char*) "/tmp/edge.sock", (char*) "-0", NULL, NULL,
        usage_pattern, help_message
    };
    Tokens ts;
    Command commands[] = {
    };
    Argument arguments[] = {
    };
    Option options[] = {
        {"-c", "--clear-cache", 0, 0, NULL},
        {NULL, "--color-log", 0, 0, NULL},
        {"-h", "--help", 0, 0, NULL},
        {"-a", "--address", 1, 0, NULL},
        {"-b", "--bluetooth-interface", 1, 0, NULL},
        {NULL, "--edge-domain-socket", 1, 0, NULL},
        {"-e", "--endpoint-postfix", 1, 0, NULL},
        {"-d", "--extended-discovery-file", 1, 0, NULL},
        {"-n", "--protocol-translator-name", 1, 0, NULL}
    };
    Elements elements = {0, 0, 9, commands, arguments, options};

    ts = tokens_new(argc, argv);
    if (parse_args(&ts, &elements))
        exit(EXIT_FAILURE);
    if (elems_to_args(&elements, &args, help, version))
        exit(EXIT_SUCCESS);
    return args;
}
