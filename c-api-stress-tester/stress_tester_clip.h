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
    int color_log;
    int help;
    /* options with arguments */
    char *edge_domain_socket;
    char *max_devices;
    char *min_devices;
    char *number_of_protocol_translators;
    char *number_of_threads;
    char *parallel_connection_lock;
    char *protocol_translator_name;
    char *sleep_time_ms;
    char *test_duration_seconds;
    /* special */
    const char *usage_pattern;
    const char *help_message;
} DocoptArgs;

const char help_message[] =
"C-API Stress tester.\n"
"\n"
"Usage:\n"
"  c-api-stress-tester --protocol-translator-name <name> [--edge-domain-socket <domain-socket>] [--number-of-protocol-translators <count>] [--number-of-threads <thread-num>] [--max-devices <max-devices>] [--min-devices <min-devices>] [--test-duration-seconds <duration-seconds>] [--sleep-time-ms <milliseconds>] [--parallel-connection-lock <int>] [--color-log]\n"
"  c-api-stress-tester --help\n"
"\n"
"Options:\n"
"  -h --help                                      Show this screen.\n"
"  -n --protocol-translator-name <name>           Name of the Protocol Translator.\n"
"  --edge-domain-socket <string>                  Edge Core domain socket path. [default: /tmp/edge.sock]\n"
"  -u --number-of-protocol-translators <count>    Number of protocol translators. [default: 1]\n"
"  -t --number-of-threads <thread-num>            Number of threads to create. [default: 1]\n"
"  -a --max-devices <max-devices>                 Max number of devices to create. [default: 100]\n"
"  -i --min-devices <min-devices>                 Minimum number of devices to create. This affects to how many devices can be removed after creating. [default: 10]\n"
"  -r --test-duration-seconds <duration-seconds>  Test duration in seconds. If duration is set to 0, runs for infinitely. [default: 0].\n"
"  -l --parallel-connection-lock <int>            Parallel connection lock from application side. 1 enables. 0 disables. [default: 1]\n"
"  -s --sleep-time-ms <milliseconds>              Thread sleep time in ms. Affects to how long tester thread waits until next operation. [default: 1000]\n"
"  --color-log                                    Use ANSI colors in log.\n"
"\n"
"";

const char usage_pattern[] =
"Usage:\n"
"  c-api-stress-tester --protocol-translator-name <name> [--edge-domain-socket <domain-socket>] [--number-of-protocol-translators <count>] [--number-of-threads <thread-num>] [--max-devices <max-devices>] [--min-devices <min-devices>] [--test-duration-seconds <duration-seconds>] [--sleep-time-ms <milliseconds>] [--parallel-connection-lock <int>] [--color-log]\n"
"  c-api-stress-tester --help";

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
        } else if (!strcmp(option->olong, "--color-log")) {
            args->color_log = option->value;
        } else if (!strcmp(option->olong, "--help")) {
            args->help = option->value;
        } else if (!strcmp(option->olong, "--edge-domain-socket")) {
            if (option->argument)
                args->edge_domain_socket = option->argument;
        } else if (!strcmp(option->olong, "--max-devices")) {
            if (option->argument)
                args->max_devices = option->argument;
        } else if (!strcmp(option->olong, "--min-devices")) {
            if (option->argument)
                args->min_devices = option->argument;
        } else if (!strcmp(option->olong, "--number-of-protocol-translators")) {
            if (option->argument)
                args->number_of_protocol_translators = option->argument;
        } else if (!strcmp(option->olong, "--number-of-threads")) {
            if (option->argument)
                args->number_of_threads = option->argument;
        } else if (!strcmp(option->olong, "--parallel-connection-lock")) {
            if (option->argument)
                args->parallel_connection_lock = option->argument;
        } else if (!strcmp(option->olong, "--protocol-translator-name")) {
            if (option->argument)
                args->protocol_translator_name = option->argument;
        } else if (!strcmp(option->olong, "--sleep-time-ms")) {
            if (option->argument)
                args->sleep_time_ms = option->argument;
        } else if (!strcmp(option->olong, "--test-duration-seconds")) {
            if (option->argument)
                args->test_duration_seconds = option->argument;
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
        0, 0, (char*) "/tmp/edge.sock", (char*) "100", (char*) "10", (char*)
        "1", (char*) "1", (char*) "1", NULL, (char*) "1000", (char*) "0",
        usage_pattern, help_message
    };
    Tokens ts;
    Command commands[] = {
    };
    Argument arguments[] = {
    };
    Option options[] = {
        {NULL, "--color-log", 0, 0, NULL},
        {"-h", "--help", 0, 0, NULL},
        {NULL, "--edge-domain-socket", 1, 0, NULL},
        {"-a", "--max-devices", 1, 0, NULL},
        {"-i", "--min-devices", 1, 0, NULL},
        {"-u", "--number-of-protocol-translators", 1, 0, NULL},
        {"-t", "--number-of-threads", 1, 0, NULL},
        {"-l", "--parallel-connection-lock", 1, 0, NULL},
        {"-n", "--protocol-translator-name", 1, 0, NULL},
        {"-s", "--sleep-time-ms", 1, 0, NULL},
        {"-r", "--test-duration-seconds", 1, 0, NULL}
    };
    Elements elements = {0, 0, 11, commands, arguments, options};

    ts = tokens_new(argc, argv);
    if (parse_args(&ts, &elements))
        exit(EXIT_FAILURE);
    if (elems_to_args(&elements, &args, help, version))
        exit(EXIT_SUCCESS);
    return args;
}
