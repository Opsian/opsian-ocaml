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
#include "terminator.h"
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

// 0 is reserved to represent null
const int JAVA_THREAD = 1;
typedef ConcurrentMap<pthread_t, int, NoDebugTrait> ThreadIdMap;

class Profiler {
public:
    static bool validate(pthread_t pthreadId);

    static bool isValidThread();

    static void initThreadIdMap(ureg size);

    static void shutdown();

    explicit Profiler(
        ConfigurationOptions *configuration,
        pthread_mutex_t& threadLock,
        const string ocamlVersion);

    bool start();

    void stop();

    void handle(int signum, void *context);

    bool isRunning();

    /*void onThreadStartLocked(JNIEnv *jniEnv, const jvmtiThreadInfo &info, pthread_t pThreadId);

    void onThreadStart(JNIEnv *jniEnv, const jvmtiThreadInfo &info, pthread_t pThreadId);

    void onThreadEnd(pthread_t pThreadId);*/

    ~Profiler();

private:

    static std::atomic<bool> shuttingDown;

    static ThreadIdMap* pThreadIdToJavaThread_;

    std::atomic<int> wallclockScanId_;

    string ocamlVersion_;

    ConfigurationOptions *configuration_;

    Terminator* processorTerminator;

    Terminator* metricsTerminator;

    FileOutputStream* logFile;

    DebugLogger* debugLogger_;

    Network* network_;

    LogWriter* writer;

    CircularQueue* buffer;

    Processor* processor;

    ProtocolHandler* protocolHandler;

    CollectorController* collectorController;

    SignalHandler* handler_;

    Metrics* metrics;

    void configure(pthread_mutex_t &param);

    void onSocketConnected();

    void recordAllocationTable();

    bool __is_running();

    DISALLOW_COPY_AND_ASSIGN(Profiler);
};

#endif // PROFILER_H
