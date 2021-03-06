#ifndef GLOBALS_H
#define GLOBALS_H

#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <signal.h>
#include <string>
#include <string.h>
#include <time.h>
#include <fstream>
#include "linkable_profiler.h"
#include <atomic>

extern "C" int linkable_handle(CallFrame* frames, ErrorHolder* errorHolder);

#ifdef __MACH__
#   include <mach/clock.h>
#   include <mach/mach.h>
#endif

// Not supported in GCC 4.4, so make it fail on other platforms as well
// this also ensures that ASIO generates boost::chrono methods, not std::chrono ones
#define BOOST_ASIO_DISABLE_STD_CHRONO

#define NS_IN_MS 1000000
#define MS_IN_S 1000
void sleep_ms(uint64_t durationInMs);

long toMillis(const timespec& timestamp);

int getTid();

void substitute_option(
    std::string& value,
    const std::string& search,
    const std::string& replace);

const int MAX_HEADER_SIZE = 5;

class Profiler;
class CollectorController;
class DebugLogger;

void logError(const char *__restrict format, ...);

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
// Declared here so that the error log and proc scanner can log to it
extern DebugLogger* _DEBUG_LOGGER;

const char* const DEFAULT_HOST = "collector.opsian.com";
const char* const DEFAULT_PORT = "50052";

#if __GNUC__ == 4 && __GNUC_MINOR__ < 6 && !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__clang__)
#	include <cstdatomic>
#	define C_ATOMICS
#else
#	include <atomic>
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

#define DEFAULT_PROMETHEUS_PROCESS_SAMPLE_RATE 100
#define DEFAULT_PROMETHEUS_ELAPSED_SAMPLE_RATE 100


struct ConfigurationOptions {
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
    bool prometheusEnabled;
    std::string prometheusHost;
    std::vector<int> prometheusPorts;
    std::string prometheusSegment;
    int prometheusProcessSampleRate;
    int prometheusElapsedSampleRate;

    ConfigurationOptions() :
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
            logCorruption(false),
            prometheusEnabled(false),
            prometheusHost(""),
            prometheusPorts(),
            prometheusSegment(""),
            prometheusProcessSampleRate(DEFAULT_PROMETHEUS_PROCESS_SAMPLE_RATE),
            prometheusElapsedSampleRate(DEFAULT_PROMETHEUS_ELAPSED_SAMPLE_RATE) {
    }

    ~ConfigurationOptions() {
    }
};

// Gets us around -Wunused-parameter
#define IMPLICITLY_USE(x) (void) x;

#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                     \
  TypeName(const TypeName &);                                                  \
  void operator=(const TypeName &)

void bootstrapHandle(int signum, siginfo_t* info, void* context);

// Way to generate time, not guaranteed to be thread safe.
// Cannot use C++11 std::put_time
void putFormattedTime(std::ostream &ostream);

#endif // GLOBALS_H
