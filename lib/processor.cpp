#include <thread>
#include <iostream>
#include "processor.h"
#include "globals.h"

const int MAX_POLLS = 10;

const uint MIN_SLEEP_IN_MS = 2;
const uint MAX_SLEEP_IN_MS = 200;

void Processor::run() {

    collectorController_.onStart();

    uint sleepDuration = MIN_SLEEP_IN_MS;

    // Want to check isRunning after every sleep
    while (collectorController_.isOn() && terminator_.isRunning()) {
        bool doneWork = collectorController_.poll();
        // poll() can sleep as part of a reconnect backooff
        if (!terminator_.isRunning()) {
            break;
        }

        doneWork |= network_.poll();

        if (collectorController_.isConnected()) {
            int i;
            for (i = 0; i < MAX_POLLS && buffer_.pop(logWriter_); i++) {
            }
            doneWork |= (i > 0);
        }

        if (!doneWork) {
            terminator_.timedWait(sleepDuration);
            sleepDuration = std::min(MAX_SLEEP_IN_MS, sleepDuration * 2);
        } else {
            sleepDuration = MIN_SLEEP_IN_MS;
        }
    }

    collectorController_.onEnd();

    threadRunning.store(false);
    // no shared data access after this point, can be safely deleted
}

void callbackToRunProcessor(void *arg) {
    //Avoid having the processor thread also receive the PROF signals
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPROF);
    sigaddset(&mask, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) < 0) {
        logError("ERROR: failed to set processor thread signal mask\n");
    }

    Processor *processor = (Processor *) arg;
    processor->run();
}

void Processor::start() {
    debugLogger_ << "Starting Processor Thread" << endl;
    terminator_.start();
    threadRunning.store(true);

    // TODO: start thread with callbackToRunProcessor
}

void Processor::stop() {
    terminator_.terminate();
    debugLogger_ << "Stopping Processor Thread" << endl;
    while (threadRunning.load()) {
        sched_yield();
    }
    debugLogger_ << "Stopped Processor Thread" << endl;
}

bool Processor::isRunning() const {
    return terminator_.isRunning();
}
