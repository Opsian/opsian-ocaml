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
    SignalHandler()
    : currentProcessInterval_(0),
      isProcessProfiling_(false) {
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

    bool updateProcessInterval(int);

    bool stopProcessProfiling();

    bool isProcessProfiling() const;

private:
    int currentProcessInterval_;
    bool isProcessProfiling_;

    DISALLOW_COPY_AND_ASSIGN(SignalHandler);
};

#endif // SIGNAL_HANDLER_H
