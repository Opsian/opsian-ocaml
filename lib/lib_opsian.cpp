#include <stdio.h>
#include "globals.h"
#include "profiler.h"
#include <execinfo.h>
#include <signal.h>

extern "C" void start_opsian_native();

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
            if (strstr(key, "__intervalMin") == key) {
                configuration.samplingIntervalMin = atoi(value);
            } else if (strstr(key, "__intervalMax") == key) {
                configuration.samplingIntervalMax = atoi(value);
            } else if (strstr(key, "__interval") == key) {
                configuration.samplingIntervalMin = configuration.samplingIntervalMax = atoi(value);
            } else if (strstr(key, "__logPath") == key) {
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

void start_opsian_native() {
    signal(SIGSEGV, crashHandler);

    std::istringstream(AGENT_VERSION_STR) >> AGENT_VERSION;

    char* options = getenv("OPSIAN_OPTS");
    if (options == nullptr) {
        printf("Please set the environment variable 'OPSIAN_OPTS' in order to use Opsian\n");
        return;
    }

    printf("Starting Opsian with: %s!\n", options);

    CONFIGURATION = new ConfigurationOptions();
    parseArguments(options, *CONFIGURATION);

    const std::string& errorLogPath = CONFIGURATION->errorLogPath;
    if (!errorLogPath.empty()) {
        // never de-allocated, lives the life of the program
        ERROR_FILE = new ofstream(errorLogPath);
    }

    prof = new Profiler(CONFIGURATION, threadLock);
    prof->start();
}

void bootstrapHandle(int signum, siginfo_t *info, void *context) {
    IMPLICITLY_USE(info)

    if (!Profiler::isValidThread()) {
        return;
    }

    prof->handle(signum, context, nullptr);
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
// * add in thread creation code in order to start the agent
// * get metrics back to a server
// * get debug information into globals.cpp
// * integrate sadiq's profiler prototype - CollectorController::onSampleRate
// * hook thread start / stop events call prof->onThreadStart() / prof->onThreadEnd()
// * hook stopping the environment
