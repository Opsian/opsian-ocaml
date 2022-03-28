#include "profiler.h"
#include "proc_scanner.h"
#include "prometheus_exporter.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include <libunwind.h>

#ifdef __x86_64
/* Only for _rdstc */
#include <x86intrin.h>
#elif __aarch64__
int64_t _rdtsc() {
  int64_t virtual_timer_value;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
}
#endif

Profiler::Profiler(
    ConfigurationOptions *configuration,
    const string ocamlVersion)
    :   wallclockScanId_(0),
        ocamlVersion_(ocamlVersion),
        configuration_(configuration),
        logFile(nullptr),
        debugLogger_(nullptr),
        network_(nullptr),
        writer(nullptr),
        prometheusQueueListener_(nullptr),
        buffer(nullptr),
        processor(nullptr),
        protocolHandler(nullptr),
        collectorController(nullptr),
        handler_(nullptr),
        metrics(nullptr) {
    configure();
}

void pushError(ErrorHolder* errorHolder, CircularQueue* queue) {
    const char* functionName;
    switch (errorHolder->type) {
        case GET_CONTEXT_FAIL:
            functionName = "unw_getcontext";
            break;
        case INIT_LOCAL_FAIL:
            functionName = "unw_init_local";
            break;
        case STEP_FAIL:
            functionName = "unw_step";
            break;
        default:
            functionName = "Unknown";
            break;
    }

    const char* errorName = unw_strerror(errorHolder->errorCode);
    const char* formatString = "in linkable_handle for %s: %s";
    int size = snprintf(NULL, 0, formatString, functionName, errorName);
    char buf[size + 1];
    snprintf(buf, sizeof buf, formatString, functionName, errorName);
    queue->pushNotification(data::NotificationCategory::USER_ERROR, buf);
}

void Profiler::handle(int signum, void* context) {
    if (! (signum == SIGPROF || signum == SIGALRM)) {
        buffer->pushNotification(
                data::NotificationCategory::USER_ERROR, "Signal number out of range: ", signum);
        return;
    }

    CallFrame frames[MAX_FRAMES];
    ErrorHolder errorHolder;
    errorHolder.errorCode = 0;
    errorHolder.type = SUCCESS;

    uint64_t start_ts = _rdtsc();
    int num_frames = linkable_handle(frames, &errorHolder);
    uint64_t stack_ts = _rdtsc();

    const bool is_error = errorHolder.type != SUCCESS;

    if (is_error) {
        pushError(&errorHolder, buffer);
    }

    CallTrace trace;
    trace.frames = frames;
    trace.num_frames = is_error ? -1 * num_frames : num_frames;
    trace.threadId = pthread_self();
    const bool enqueued = buffer->pushStackTrace(trace, signum, 0, stack_ts - start_ts);
    if (!enqueued) {
        if (signum == SIGPROF) {
            CircularQueue::cputimeFailures++;
        } else {
            CircularQueue::wallclockFailures++;
        }
    }
}

void Profiler::start() {
    handler_->SetAction(SIGPROF, SIGALRM, &bootstrapHandle);
    handler_->SetAction(SIGALRM, SIGPROF, &bootstrapHandle);

    processor->start();
    metrics->startThread();
}

using boost::asio::ip::tcp;

void Profiler::configure() {
    const std::string& host = configuration_->host;
    const std::string& port = configuration_->port;
    const std::string& apiKey = configuration_->apiKey;
    std::string& agentId = configuration_->agentId;
    const std::string& fileName = configuration_->logFilePath;

    debugLogger_ = new DebugLogger(configuration_->debugLogPath, apiKey);
    debugLogger_->writeLogStart();
    _DEBUG_LOGGER = debugLogger_;

    if (configuration_->prometheusEnabled) {
        *debugLogger_
            << "Prometheus Enabled: " << configuration_->prometheusHost
            << ":" << configuration_->prometheusPort
            << endl;
    }

    if (!fileName.empty()) {
        const char* fileNameStr = fileName.c_str();
        int logFileDescriptor = open(fileNameStr, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (logFileDescriptor == -1) {
            // The JVM will still continue to run though;
            // could call abort() to terminate the JVM abnormally, but don't want to
            logError("ERROR: Failed to open file %s for writing %d\n", fileNameStr, errno);
        }

        logFile = new FileOutputStream(logFileDescriptor);
        logFile->SetCloseOnDelete(true);
    }

    if (!configuration_->prometheusEnabled && apiKey.empty()) {
        logError("ERROR: no api key set for the profiling agent\n");
    }

    char hostname[HOST_NAME_MAX];
    int hasHostName;
    if((hasHostName = gethostname(hostname, HOST_NAME_MAX)) != 0) {
        logError("Unable to lookup hostname");
        *debugLogger_ << "Result from hostname lookup failure" << hasHostName << endl;
    }

    if (agentId.empty()) {
        // If a custom agentId isn't specified, default to the hostname
        agentId.assign(hostname);
    }

    network_ = new Network(
        host, port, configuration_->customCertificateFile, *debugLogger_, configuration_->onPremHost,
        configuration_->prometheusEnabled);

    if (configuration_->prometheusEnabled) {
        bind_prometheus(*configuration_, *debugLogger_);
    }

    handler_ = new SignalHandler();

    const int processorCount = 1;
    const bool isOn = (!apiKey.empty() && hasHostName == 0) || configuration_->prometheusEnabled;

    buffer = new CircularQueue(configuration_->maxFramesToCapture);

    metrics = new Metrics(*debugLogger_, *buffer);

    collectorController = new CollectorController(
        *network_,
        isOn,
        apiKey,
        agentId,
        hostname,
        configuration_->applicationVersion,
        ocamlVersion_,
        static_cast<uint32_t >(processorCount),
        boost::bind(&Profiler::onSocketConnected, this),
        boost::bind(&Profiler::recordAllocationTable, this),
        *handler_,
        *debugLogger_,
        *metrics,
        configuration_->prometheusEnabled);

    protocolHandler = new ProtocolHandler(*network_, *collectorController, *debugLogger_);

    writer = new LogWriter(
        logFile,
        *buffer,
        *network_,
        *collectorController,
        *debugLogger_,
        configuration_->maxFramesToCapture,
        configuration_->logCorruption);

    QueueListener* queueListener = writer;
    if (configuration_->prometheusEnabled) {
        prometheusQueueListener_ = prometheus_queue_listener();
        queueListener = prometheusQueueListener_;
    }

    processor = new Processor(
        *queueListener,
        *buffer,
        *network_,
        *collectorController,
        *debugLogger_);
}

void Profiler::onSocketConnected() {
    if (protocolHandler != nullptr) {
        protocolHandler->attemptRead();
    }
    if (writer != nullptr) {
        writer->onSocketConnected();
    }
}

void Profiler::recordAllocationTable() {
    if (writer != nullptr) {
        writer->recordAllocationTable();
    }
}

Profiler::~Profiler() {
    DELETE(processor);
    DELETE(handler_);
    DELETE(buffer);
    DELETE(collectorController);
    DELETE(protocolHandler);
    DELETE(writer);
    DELETE(network_);
    DELETE(metrics);
    DELETE(logFile);
    DELETE(debugLogger_);
}

void Profiler::on_fork() {
    // Reset state then start
    reset_scan_threads();
    processor->on_fork();
    collectorController->on_fork();
    writer->onSocketConnected();
    network_->on_fork();
    metrics->on_fork();
    start();
}
