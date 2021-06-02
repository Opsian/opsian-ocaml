#ifndef GLOBALS_H
#define GLOBALS_H

#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <signal.h>
#include <string>
#include <string.h>
#include <fstream>
#include "linkable_profiler.h"

extern "C" int linkable_handle(CallFrame* frames);

#ifdef __MACH__
#   include <mach/clock.h>
#   include <mach/mach.h>
#endif

// Not supported in GCC 4.4, so make it fail on other platforms as well
// this also ensures that ASIO generates boost::chrono methods, not std::chrono ones
#define BOOST_ASIO_DISABLE_STD_CHRONO

int getTid();

const int MAX_HEADER_SIZE = 5;

class Profiler;
class CollectorController;
class DebugLogger;

void logError(const char *__restrict format, ...);

const int DEFAULT_SAMPLING_INTERVAL = 1;

extern const char* const GIT_HASH;
extern const char* const GIT_TAGS;
extern const char* const GIT_STR;
extern const char* const BUILD_TYPE;
extern const std::string CERTIFICATE_1;
extern const std::string CERTIFICATE_2;
extern const std::string AGENT_VERSION_STR;
extern const char* STRINGS_AGENT_VERSION_STR;
extern uint32_t AGENT_VERSION;
extern const std::string ON_PREM_HOST_DEFAULT;

extern std::ostream* ERROR_FILE;
// Declared here so that the error log can log to it
// Try to avoid using this for anything else
extern DebugLogger* _DEBUG_LOGGER;

const char* const DEFAULT_HOST = "collector.opsian.com";
const char* const DEFAULT_PORT = "50052";

#if __GNUC__ == 4 && __GNUC_MINOR__ < 6 && !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__clang__)
#	include <cstdatomic>
#	define C_ATOMICS
#else
#	include <atomic>
#endif

// If we're on an old g++ then use their internal pre-standardised nullptr implementation.
#if !defined(nullptr)
#	define nullptr NULL
#endif

#include <vector>
#include <mutex>
#include <iostream>

#if defined(STATIC_ALLOCATION_ALLOCA)
  #define STATIC_ARRAY(NAME, TYPE, SIZE, MAXSZ) TYPE *NAME = (TYPE*)alloca((SIZE) * sizeof(TYPE))
#elif defined(STATIC_ALLOCATION_PEDANTIC)
  #define STATIC_ARRAY(NAME, TYPE, SIZE, MAXSZ) TYPE NAME[MAXSZ]
#else
#endif

#define DELETE(PTR) \
    delete PTR;\
    PTR = NULL;

void assign_range(char* value, char* next, std::string& to);
char *safe_copy_string(const char *value, const char *next);

struct ConfigurationOptions {
    /** Interval in microseconds */
    int samplingIntervalMin;
    int samplingIntervalMax;
    std::string logFilePath;
    std::string host;
    std::string port;
    int maxFramesToCapture;
    std::string apiKey;
    std::string agentId;
    std::string debugLogPath;
    std::string errorLogPath;
    std::string applicationVersion;
    std::string customCertificateFile;
    bool onPremHost;
    bool logCorruption;

    ConfigurationOptions() :
            samplingIntervalMin(DEFAULT_SAMPLING_INTERVAL),
            samplingIntervalMax(DEFAULT_SAMPLING_INTERVAL),
            logFilePath(""),
            host(DEFAULT_HOST),
            port(DEFAULT_PORT),
            maxFramesToCapture(MAX_FRAMES),
            apiKey(""),
            agentId(""),
            debugLogPath(""),
            errorLogPath(""),
            applicationVersion(""),
            customCertificateFile(""),
            onPremHost(ON_PREM_HOST_DEFAULT == "Y"),
            logCorruption(false) {
    }

    ~ConfigurationOptions() {
    }
};

// Gets us around -Wunused-parameter
#define IMPLICITLY_USE(x) (void) x;

// Log error code for lock acquisition
#define SAFE_PTHREAD_MUTEX_LOCK(threadLock)                                   \
    int lockResult = pthread_mutex_lock(&threadLock);                         \
    if (lockResult != 0) {                                                    \
        logError("Error obtaining lock, status code: %d\n", lockResult);      \
    } else                                                                    \

#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                     \
  TypeName(const TypeName &);                                                  \
  void operator=(const TypeName &)

#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName)                               \
  TypeName();                                                                  \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

void bootstrapHandle(int signum, siginfo_t* info, void* context);

// Way to generate time, not guaranteed to be thread safe.
// Cannot use C++11 std::put_time
void putFormattedTime(std::ostream &ostream);

#endif // GLOBALS_H
