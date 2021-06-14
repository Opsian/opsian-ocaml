#include <stdio.h>
#include "globals.h"
#include "profiler.h"
#include <execinfo.h>
#include <signal.h>
#include "caml/mlvalues.h"

extern "C" CAMLprim void start_opsian_native(value ocaml_version_str, value ocaml_executable_name_str);

// Guards deletion of prof against thread start/end
#if defined(PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP)
static pthread_mutex_t threadLock = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
#else
static pthread_mutex_t threadLock = PTHREAD_MUTEX_INITIALIZER;
#endif

static ConfigurationOptions* CONFIGURATION;
static Profiler* prof;

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

void substitute_options(
    ConfigurationOptions* configuration,
    const char* ocaml_executable_name,
    const size_t ocaml_executable_name_len) {

    string substitution_value(ocaml_executable_name, ocaml_executable_name_len);
    size_t last_slash = substitution_value.find_last_of('/');
    if (last_slash != std::string::npos) {
        substitution_value.erase(0, last_slash + 1);
    }
    substitute_option(substitution_value, ".", "_");

    string substitution_variable = "£{EXE_NAME}";

    substitute_option(configuration->logFilePath, substitution_variable, substitution_value);
    substitute_option(configuration->errorLogPath, substitution_variable, substitution_value);
    substitute_option(configuration->debugLogPath, substitution_variable, substitution_value);
    substitute_option(configuration->customCertificateFile, substitution_variable, substitution_value);
    substitute_option(configuration->agentId, substitution_variable, substitution_value);

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

CAMLprim void start_opsian_native(value ocaml_version_str, value ocaml_executable_name_str) {
    signal(SIGSEGV, crashHandler);

    const char* ocaml_version = String_val(ocaml_version_str);
    const char* ocaml_executable_name = String_val(ocaml_executable_name_str);
    const size_t ocaml_executable_name_len = caml_string_length(ocaml_executable_name_str);

    std::istringstream(AGENT_VERSION_STR) >> AGENT_VERSION;

    char* options = getenv("OPSIAN_OPTS");
    if (options == nullptr) {
        logError("Please set the environment variable 'OPSIAN_OPTS' in order to use Opsian\n");
        return;
    }

    CONFIGURATION = new ConfigurationOptions();
    parseArguments(options, *CONFIGURATION);
    substitute_options(CONFIGURATION, ocaml_executable_name, ocaml_executable_name_len);

    printf("OPTS: '%s' '%s' '%s'\n", CONFIGURATION->customCertificateFile.c_str(), CONFIGURATION->agentId.c_str(), CONFIGURATION->debugLogPath.c_str());

    const std::string& errorLogPath = CONFIGURATION->errorLogPath;
    if (!errorLogPath.empty()) {
        // never de-allocated, lives the life of the program
        ERROR_FILE = new ofstream(errorLogPath);
    }

    prof = new Profiler(CONFIGURATION, threadLock, ocaml_version);
    prof->start();
}

void bootstrapHandle(int signum, siginfo_t *info, void *context) {
    IMPLICITLY_USE(info)

    if (!Profiler::isValidThread()) {
        return;
    }

    prof->handle(signum, context);
}

void stop_opsian_native() {
    // Needs to be called to block inflight hooks from being run.
    Profiler::shutdown();

    if (prof->isRunning()) {
        prof->stop();
        // Unregister any hooks here
    }

    pthread_mutex_lock(&threadLock);
    DELETE(prof);
    DELETE(CONFIGURATION);
    pthread_mutex_unlock(&threadLock);
}

// TODO:
// * hook thread start / stop events call prof->onThreadStart() / prof->onThreadEnd()
// * hook stopping the environment
