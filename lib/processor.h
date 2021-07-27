#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "log_writer.h"
#include "signal_handler.h"
#include "network.h"
#include "collector_controller.h"

static const char *const PROCESSOR_THREAD_NAME = "Opsian Processor";

class Processor {

public:
    explicit Processor(
        LogWriter& logWriter,
        CircularQueue& buffer,
        Network& network,
        CollectorController& collectorController,
        DebugLogger& debugLogger)
        : thread(0),
          logWriter_(logWriter),
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

    LogWriter& logWriter_;

    CircularQueue& buffer_;

    Network& network_;

    CollectorController& collectorController_;

    DebugLogger& debugLogger_;

    DISALLOW_COPY_AND_ASSIGN(Processor);
};

#endif // PROCESSOR_H
