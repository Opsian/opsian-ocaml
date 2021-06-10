#include "signal_handler.h"

const int NOT_PROFILING = 0;

bool SignalHandler::updateSigprofInterval() {
    bool res = updateSigprofInterval(timingIntervals[intervalIndex]);
    intervalIndex = (intervalIndex + 1) % NUMBER_OF_INTERVALS;
    return res;
}

bool SignalHandler::updateSigprofInterval(const int timingIntervalInMillis) {
    if (timingIntervalInMillis == currentInterval)
        return true;
    static struct itimerval timer;
    timer.it_interval.tv_sec = timingIntervalInMillis / 1000;
    timer.it_interval.tv_usec = (timingIntervalInMillis * 1000) % 1000000;
    timer.it_value = timer.it_interval;

    if (setitimer(ITIMER_PROF, &timer, 0) == -1) {
        logError("Scheduling profiler interval failed with error %d\n", errno);
        return false;
    }
    currentInterval = timingIntervalInMillis;
    isProfiling_ = timingIntervalInMillis != NOT_PROFILING;
    return true;
}

bool SignalHandler::stopSigprof() {
    return updateSigprofInterval(NOT_PROFILING);
}

bool SignalHandler::isProfiling() const {
    return isProfiling_;
}

struct sigaction SignalHandler::SetAction(
    const int signalNumber,
    const int maskedSignalNumber,
    void (*action)(int, siginfo_t *, void *)) {

    struct sigaction sa;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = NULL;
    sa.sa_sigaction = action;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    if (maskedSignalNumber != 0) {
        sigaddset(&sa.sa_mask, maskedSignalNumber);
    }

    struct sigaction old_handler;

    // Set the sigprof signal handler
    if (sigaction(signalNumber, &sa, &old_handler) != 0) {
        logError("Scheduling profiler action failed with error %d\n", errno);
        return old_handler;
    }

    return old_handler;
}

struct sigaction SignalHandler::SetAction(
        const int signalNumber,
        const int maskedSignalNumber,
        const int maskedSignalNumber2,
        void (*action)(int, siginfo_t *, void *)) {

    struct sigaction sa;
#ifdef __clang__
    #pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = NULL;
    sa.sa_sigaction = action;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    sigemptyset(&sa.sa_mask);
    if (maskedSignalNumber != 0) {
        sigaddset(&sa.sa_mask, maskedSignalNumber);
    }
    if (maskedSignalNumber2 != 0) {
        sigaddset(&sa.sa_mask, maskedSignalNumber2);
    }

    struct sigaction old_handler;

    // Set the sigprof signal handler
    if (sigaction(signalNumber, &sa, &old_handler) != 0) {
        logError("Scheduling profiler action failed with error %d\n", errno);
        return old_handler;
    }

    return old_handler;
}
