#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "signal_handler.h"
#include "network.h"
#include "collector_controller.h"

// Do not set this to longer than 16 characters
static const char *const PROCESSOR_THREAD_NAME = "Opsian Proc";

class Processor {

public:
    explicit Processor(
        QueueListener& queueListener,
        CircularQueue& buffer,
        Network& network,
        CollectorController& collectorController,
        DebugLogger& debugLogger)
        : thread(0),
          queueListener_(queueListener),
          buffer_(buffer),
          network_(network),
          collectorController_(collectorController),
          debugLogger_(debugLogger) {
    }

    void start();

    void run();

    void on_fork();

private:

    pthread_t thread;

    QueueListener& queueListener_;

    CircularQueue& buffer_;

    Network& network_;

    CollectorController& collectorController_;

    DebugLogger& debugLogger_;

    DISALLOW_COPY_AND_ASSIGN(Processor);
};

#endif // PROCESSOR_H
