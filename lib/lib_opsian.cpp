#include <stdio.h>
#include "globals.h"
#include "profiler.h"
#include <execinfo.h>
#include <signal.h>
#include "caml/mlvalues.h"
#include <time.h>

extern "C" CAMLprim void start_opsian_native(
    value ocaml_version_str, value ocaml_executable_name_str, value ocaml_argv0_str);

static char* OCAML_EXE_NAME;
static size_t OCAML_EXE_NAME_LEN;

static char* OCAML_ARGV0;
static size_t OCAML_ARGV0_LEN;

static char* OPTIONS;
static ConfigurationOptions* CONFIGURATION;
static Profiler* prof;

void setup_configuration();

void assign_range(char* value, char* next, std::string& to) {
    size_t size = (next == 0) ? strlen(value) : (size_t) (next - value);
    to.assign(value, size);
}

void parseArguments(char *options, ConfigurationOptions &configuration) {
    char* next = options;
    for (char *key = options; next != nullptr; key = next + 1) {
        char *value = strchr(key, '=');
        next = strchr(key, ',');
        if (value == nullptr) {
            logError("WARN: No value for key %s\n", key);
            continue;
        } else {
            value++;
            if (strstr(key, "__logPath") == key) {
                assign_range(value, next, configuration.logFilePath);
            } else if (strstr(key, "__maxFrames") == key) {
                configuration.maxFramesToCapture = atoi(value);
            } else if (strstr(key, "__port") == key) {
                assign_range(value, next, configuration.port);
            } else if (strstr(key, "host") == key) {
                assign_range(value, next, configuration.host);
            } else if (strstr(key, "apiKey") == key) {
                assign_range(value, next, configuration.apiKey);
            } else if (strstr(key, "agentId") == key) {
                assign_range(value, next, configuration.agentId);
            } else if (strstr(key, "debugLogPath") == key) {
                assign_range(value, next, configuration.debugLogPath);
            } else if (strstr(key, "errorLogPath") == key) {
                assign_range(value, next, configuration.errorLogPath);
            } else if (strstr(key, "applicationVersion") == key) {
                assign_range(value, next, configuration.applicationVersion);
            } else if (strstr(key, "customCertificateFile") == key) {
                assign_range(value, next, configuration.customCertificateFile);
            } else if (strstr(key, "onPremHost") == key) {
                char onPremHostValue = *value;
                configuration.onPremHost = (onPremHostValue == 'y' || onPremHostValue == 'Y');
            } else if (strstr(key, "__logCorruption") == key) {
                char logCorruptionValue = *value;
                configuration.logCorruption = (logCorruptionValue == 'y' || logCorruptionValue == 'Y');
            } else {
                logError("WARN: Unknown configuration option: %s\n", key);
            }
        }
    }
}

void substitute_option(
    std::string& value,
    const std::string& search,
    const std::string& replace) {
    size_t pos = 0;
    while ((pos = value.find(search, pos)) != std::string::npos) {
        value.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}

void substitutePath(ConfigurationOptions* configuration, const char* name,
                    const size_t& len, string substitution_variable) {

    string substitution_value(name, len);
    size_t last_slash = substitution_value.find_last_of('/');
    if (last_slash != std::string::npos) {
        substitution_value.erase(0, last_slash + 1);
    }
    substitute_option(substitution_value, ".", "_");

    substitute_option(configuration->logFilePath, substitution_variable, substitution_value);
    substitute_option(configuration->errorLogPath, substitution_variable, substitution_value);
    substitute_option(configuration->debugLogPath, substitution_variable, substitution_value);
    substitute_option(configuration->customCertificateFile, substitution_variable, substitution_value);
    substitute_option(configuration->agentId, substitution_variable, substitution_value);
}

/*
 * If you use £{EXE_NAME} in your options strings then it should sub in the process name.
 * For example "debugLogPath=£{EXE_NAME}-debug.log" with a program whose executable command is
 * "./_build/default/examples/opsian_examples.exe" results in a debug log file of "opsian_examples_exe-debug.log"
 */
void substitute_options(
    ConfigurationOptions* configuration) {

    substitutePath(configuration, OCAML_EXE_NAME, OCAML_EXE_NAME_LEN, "£{EXE_NAME}");
    substitutePath(configuration, OCAML_ARGV0, OCAML_ARGV0_LEN, "£{ARGV_0}");

    char pid_buffer[100];
    const int pid_len = snprintf(pid_buffer, 100, "%d", getpid());
    substitutePath(configuration, pid_buffer, pid_len, "£{PID}");
}

void crashHandler(int sig) {
    const int MAX_BT_SIZE = 20;
    void *array[MAX_BT_SIZE];
    size_t size;

    size = backtrace(array, MAX_BT_SIZE);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

// ---------------------
// BEGIN Fork Callbacks
// ---------------------

void prepare_fork() {
    network_prepare_fork();
}

void parent_fork() {
    *_DEBUG_LOGGER << "parent_fork " << getpid() << endl;
    network_parent_fork();
}

void child_fork() {
    setup_configuration();
    _DEBUG_LOGGER->on_fork(CONFIGURATION->debugLogPath);
    *_DEBUG_LOGGER << "child_fork " << getpid() << endl;
    network_child_fork();
    prof->on_fork();
}

// ---------------------
// END Fork Callbacks
// ---------------------

char* copy(char* source, const size_t size) {
    char* result = new char[size + 1];
    strncpy(result, source, size + 1);
    return result;
}

CAMLprim void start_opsian_native(
    value ocaml_version_str, value ocaml_executable_name_str, value ocaml_argv0_str) {
    // signal(SIGSEGV, crashHandler);

    const char* ocaml_version = String_val(ocaml_version_str);

    char* ocaml_exe_name = (char*) String_val(ocaml_executable_name_str);
    OCAML_EXE_NAME_LEN = caml_string_length(ocaml_executable_name_str);
    OCAML_EXE_NAME = copy(ocaml_exe_name, OCAML_EXE_NAME_LEN);

    char* ocaml_argv0 = (char*) String_val(ocaml_argv0_str);
    OCAML_ARGV0_LEN = caml_string_length(ocaml_argv0_str);
    OCAML_ARGV0 = copy(ocaml_argv0, OCAML_ARGV0_LEN);

    std::istringstream(AGENT_VERSION_STR) >> AGENT_VERSION;

    OPTIONS = getenv("OPSIAN_OPTS");
    if (OPTIONS == nullptr) {
        logError("Please set the environment variable 'OPSIAN_OPTS' in order to use Opsian\n");
        return;
    }

    CONFIGURATION = new ConfigurationOptions();
    setup_configuration();

    const std::string& errorLogPath = CONFIGURATION->errorLogPath;
    if (!errorLogPath.empty()) {
        // never de-allocated, lives the life of the program
        ERROR_FILE = new ofstream(errorLogPath);
    }

    prof = new Profiler(CONFIGURATION, ocaml_version);
    pthread_atfork(&prepare_fork, &parent_fork, &child_fork);
    prof->start();
}

void setup_configuration() {
    parseArguments(OPTIONS, *CONFIGURATION);
    substitute_options(CONFIGURATION);
}

void bootstrapHandle(int signum, siginfo_t *info, void *context) {
    IMPLICITLY_USE(info)

    prof->handle(signum, context);
}

void sleep_ms(uint64_t durationInMs) {
    // Uses Posix function.
    timespec duration;
    duration.tv_sec = durationInMs / MS_IN_S;
    duration.tv_nsec = (durationInMs % MS_IN_S) * NS_IN_MS;

    int res = nanosleep(&duration, &duration);
    if (res != 0) {
        logError("Error (%d) invoking nanosleep for %lu", res, durationInMs);
    }
}
