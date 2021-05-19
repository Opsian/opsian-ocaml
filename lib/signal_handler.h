#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>

#include <fstream>
#include <unistd.h>
#include <chrono>

#include "globals.h"

const int NUMBER_OF_INTERVALS = 1024;

class SignalHandler {
public:
    SignalHandler(const int samplingIntervalMin, const int samplingIntervalMax) {
        intervalIndex = 0;
        timingIntervals = new int[NUMBER_OF_INTERVALS];
        srand (time(NULL));
        int range = samplingIntervalMax - samplingIntervalMin + 1;
        for (int i = 0; i < NUMBER_OF_INTERVALS; i++) {
            timingIntervals[i] = samplingIntervalMin + rand() % range;
        }
        currentInterval = 0;
        isProfiling_ = false;
    }

    /**
     * Sets the function to be called for a given signal number.
     *
     * @param signalNumber the signal number to update
     * @param maskedSignalNumber the signal number to mask when updating it
     * @param sigaction the function to call
     * @return the previous action that was set, if one was
     */
    struct sigaction SetAction(int signalNumber, int maskedSignalNumber, void (*sigaction)(int, siginfo_t *, void *));

    struct sigaction SetAction(int signalNumber, int maskedSignalNumber, int maskedSignalNumber2, void (*sigaction)(int, siginfo_t *, void *));

    bool updateSigprofInterval();

    bool updateSigprofInterval(int);

    bool stopSigprof();

    bool isProfiling() const;

    ~SignalHandler() {
        delete[] timingIntervals;
    }

private:
    int intervalIndex;
    int *timingIntervals;
    int currentInterval;
    bool isProfiling_;

    DISALLOW_COPY_AND_ASSIGN(SignalHandler);
};

#endif // SIGNAL_HANDLER_H

