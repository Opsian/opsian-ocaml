#include "terminator.h"

using boost::chrono::milliseconds;

Terminator::Terminator()
    : isRunning_(false),
      mutex(),
      terminated() {
}

void Terminator::start() {
    isRunning_.store(true, std::memory_order_relaxed);
}

void Terminator::terminate() {
    isRunning_.store(false, std::memory_order_relaxed);
    terminated.notify_all();
}

void Terminator::timedWait(const uint32_t durationInMs) {
    boost::unique_lock<boost::mutex> lock(mutex);
    // NB: not using condition variable in a loop as people normally would
    // since we want to exit even if the condition hasn't changed at the
    // end of the time period
    terminated.wait_for(lock, milliseconds(durationInMs));
}

bool Terminator::isRunning() {
    return isRunning_.load(std::memory_order_relaxed);
}
