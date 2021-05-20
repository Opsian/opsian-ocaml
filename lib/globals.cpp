#include "globals.h"
#include "debug_logger.h"
#include <iostream>
#include <ctime>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdarg.h>

const char* const GIT_HASH = "TODO";
const char* const GIT_TAGS = "TODO";
const char* const GIT_STR = "TODO";
const char* const BUILD_TYPE = "glibc";

const std::string CERTIFICATE_1 = "TODO";
const std::string CERTIFICATE_2 = "TODO";
const std::string ON_PREM_HOST_DEFAULT = "N";

const std::string AGENT_VERSION_STR = "16";
uint32_t AGENT_VERSION;
// Specified in this format so it can be easily read out of the binary using
// strings libopsian.so | grep 'AGENT_VERSION_IS'
const char* STRINGS_AGENT_VERSION_STR  __attribute__((used)) = "TODO"; // Eg: AGENT_VERSION_IS_16

std::ostream* ERROR_FILE = &std::cerr;
DebugLogger* _DEBUG_LOGGER = NULL;

void logError(const char *__restrict format, ...) {
    va_list arg;
    va_start(arg, format);

    *ERROR_FILE << "Opsian ("
                << GIT_STR
                << ") @ ";

    putFormattedTime(*ERROR_FILE);

    *ERROR_FILE << " Err: ";

    char buffer [500];
    sprintf(buffer, format, arg);
    va_end(arg);
    
    *ERROR_FILE << buffer;
    *ERROR_FILE << std::endl;

    if (_DEBUG_LOGGER != NULL) {
        *_DEBUG_LOGGER << buffer << endl;
    }
}

void putFormattedTime(std::ostream &ostream) {
    time_t rawTime;
    struct tm *timeInfo;
    time(&rawTime);
    timeInfo = localtime(&rawTime);
    char timeBuffer[50];
    strftime(timeBuffer, sizeof(timeBuffer), "%d-%m-%y %H:%M:%S", timeInfo);
    ostream << timeBuffer;
}

int getTid() {
#if defined(__linux__)
    return syscall(SYS_gettid);
#elif defined(__APPLE__)
    int tid = mach_thread_self();
  mach_port_deallocate(mach_task_self(), tid);
  return tid;
#elif defined(__FreeBSD__)
  long lwpid;
  thr_self(&lwpid);
  return lwpid;
#else
  return pthread_self();
#endif
}
