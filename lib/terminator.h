#ifndef OPSIAN_TERMINATOR_H
#define OPSIAN_TERMINATOR_H

#include "globals.h"

#include <thread>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

class Terminator {
public:
    Terminator();

    // This MUST be called before any future invocations of terminate()
    void start();

    // This MUST be called after start() has been called once
    void terminate();

    void timedWait(uint32_t durationInMs);

    bool isRunning();

private:
    // not a regular boolean protected by mutex so we can poll isRunning() easily
    std::atomic_bool isRunning_;
    boost::mutex mutex;
    boost::condition_variable terminated;

    DISALLOW_COPY_AND_ASSIGN(Terminator);
};


#endif //OPSIAN_TERMINATOR_H
