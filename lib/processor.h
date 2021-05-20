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
        DebugLogger& debugLogger,
        Terminator& processorTerminator)
        : thread(),
          logWriter_(logWriter),
          buffer_(buffer),
          threadRunning(false),
          network_(network),
          collectorController_(collectorController),
          debugLogger_(debugLogger),
          terminator_(processorTerminator) {
    }

    void start();

    void run();

    void stop();

    bool isRunning() const;

private:

    pthread_t thread;

    LogWriter& logWriter_;

    CircularQueue& buffer_;

    std::atomic_bool threadRunning;

    Network& network_;

    CollectorController& collectorController_;

    DebugLogger& debugLogger_;

    Terminator& terminator_;

    DISALLOW_COPY_AND_ASSIGN(Processor);
};

#endif // PROCESSOR_H
