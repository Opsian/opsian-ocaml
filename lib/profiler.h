#ifndef PROFILER_H
#define PROFILER_H

#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <sstream>
#include <string>

#include "globals.h"
#include "signal_handler.h"
#include "protocol_handler.h"
#include "collector_controller.h"
#include "processor.h"
#include "log_writer.h"
#include "debug_logger.h"
#include "metrics.h"
#include "concurrent_map.h"

#include <sys/types.h>
#include <netdb.h>
#include <stdio.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <sys/stat.h>
#include <boost/asio.hpp>

using namespace std::chrono;
using std::ofstream;
using std::ostringstream;
using std::string;
using google::protobuf::io::FileOutputStream;

class Profiler {
public:
    explicit Profiler(
        ConfigurationOptions *configuration,
        const string ocamlVersion);

    void start();

    void handle(int signum, void *context);

    void on_fork();

    ~Profiler();

private:
    std::atomic<int> wallclockScanId_;

    string ocamlVersion_;

    ConfigurationOptions *configuration_;

    FileOutputStream* logFile;

    DebugLogger* debugLogger_;

    Network* network_;

    LogWriter* writer;

    QueueListener* prometheusQueueListener_;

    CircularQueue* buffer;

    Processor* processor;

    ProtocolHandler* protocolHandler;

    CollectorController* collectorController;

    SignalHandler* handler_;

    Metrics* metrics;

    void configure();

    void onSocketConnected();

    void recordAllocationTable();

    DISALLOW_COPY_AND_ASSIGN(Profiler);
};

#endif // PROFILER_H
