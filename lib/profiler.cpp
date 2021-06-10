#include "profiler.h"

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

const int PUSH_ATTEMPTS = 1000;

ThreadIdMap* Profiler::pThreadIdToJavaThread_(nullptr);
std::atomic<bool> Profiler::shuttingDown(false);

bool Profiler::validate(const pthread_t pthreadId) {
    return pthreadId != ThreadIdMap::KeyTraits::NullKey && Profiler::pThreadIdToJavaThread_ != nullptr;
}

bool Profiler::isValidThread() {
    return true;

    // TODO: re-enable this after we've hooked thread creation in ocaml
    /*const pthread_t pThreadId = pthread_self();
    if (!validate(pThreadId)) {
        return false;
    }

    if (pThreadIdToJavaThread_->get(pThreadId) == JAVA_THREAD) {
        return true;
    }

    CollectorController::threadIdDenials++;
    return false;*/
}

void Profiler::initThreadIdMap(ureg size) {
    if (pThreadIdToJavaThread_ == nullptr && isPowerOf2(size)) {
        pThreadIdToJavaThread_ = new ThreadIdMap(size, size / 2);
    }
}

Profiler::Profiler(
    ConfigurationOptions *configuration,
    pthread_mutex_t& threadLock,
    const string ocamlVersion)
    :   wallclockScanId_(0),
        ocamlVersion_(ocamlVersion),
        configuration_(configuration),
        processorTerminator(nullptr),
        metricsTerminator(nullptr),
        logFile(nullptr),
        debugLogger_(nullptr),
        network_(nullptr),
        writer(nullptr),
        buffer(nullptr),
        processor(nullptr),
        protocolHandler(nullptr),
        collectorController(nullptr),
        handler_(nullptr),
        metrics(nullptr) {
    configure(threadLock);
    initThreadIdMap(131072);
}

void Profiler::shutdown() {
    shuttingDown.store(true, std::memory_order_release);
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

    if (errorHolder.type == SUCCESS) {
        // TODO: re-add when we use wallclock
        // const int wallclockScanId = wallclockScanId_.load(std::memory_order::memory_order_relaxed);
        CallTrace trace;
        trace.frames = frames;
        trace.num_frames = num_frames;
        const bool enqueued = buffer->pushStackTrace(trace, signum, 0, 0, stack_ts - start_ts);
        if (!enqueued) {
            if (signum == SIGPROF) {
                CircularQueue::cputimeFailures++;
            } else {
                CircularQueue::wallclockFailures++;
            }
        }
    } else {
        pushError(&errorHolder, buffer);
    }
}

bool Profiler::start() {
    if (__is_running()) {
        logError("WARN: Start called but sampling is already running\n");
        return true;
    }

    handler_->SetAction(SIGPROF, SIGALRM, &bootstrapHandle);
    handler_->SetAction(SIGALRM, SIGPROF, &bootstrapHandle);

    processor->start();
    metrics->startThread();
    return true;
}

void Profiler::stop() {
    *debugLogger_ << "Stopping profiler" << endl;
    if (!__is_running()) {
        return;
    }

    // When stopping the profiler ignore any signals that maybe in flight to avoid errors
    signal(SIGPROF, SIG_IGN);
    signal(SIGALRM, SIG_IGN);

    handler_->stopSigprof();
    metrics->stopThread();
    processor->stop();
}

bool Profiler::isRunning() {
    return __is_running();
}

// non-blocking version (can be called once spin-lock with acquire semantics is grabed)
bool Profiler::__is_running() {
    return (processor != nullptr) && processor->isRunning();
}

using boost::asio::ip::tcp;

void Profiler::configure(pthread_mutex_t& threadLock) {
    const std::string& host = configuration_->host;
    const std::string& port = configuration_->port;
    const std::string& apiKey = configuration_->apiKey;
    std::string& agentId = configuration_->agentId;
    const std::string& fileName = configuration_->logFilePath;

    debugLogger_ = new DebugLogger(configuration_->debugLogPath, apiKey);
    debugLogger_->writeLogStart();
    _DEBUG_LOGGER = debugLogger_;

    processorTerminator = new Terminator();
    metricsTerminator = new Terminator();

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

    if (apiKey.empty()) {
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
        host, port, configuration_->customCertificateFile, *debugLogger_, configuration_->onPremHost);

    handler_ = new SignalHandler(
        configuration_->samplingIntervalMin,
        configuration_->samplingIntervalMax);

    const int processorCount = 1;
    const bool isOn = !apiKey.empty() && hasHostName == 0;

    buffer = new CircularQueue(configuration_->maxFramesToCapture);

    metrics = new Metrics(*metricsTerminator, *debugLogger_, *buffer);

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
        *processorTerminator);

    protocolHandler = new ProtocolHandler(*network_, *collectorController, *debugLogger_);

    writer = new LogWriter(
        logFile,
        *buffer,
        *network_,
        *collectorController,
        *debugLogger_,
        configuration_->maxFramesToCapture,
        configuration_->logCorruption);

    processor = new Processor(
        *writer,
        *buffer,
        *network_,
        *collectorController,
        *debugLogger_,
        *processorTerminator);
}

void Profiler::onSocketConnected() {
    if (protocolHandler != nullptr) {
        protocolHandler->attemptRead();
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
    DELETE(processorTerminator);
    DELETE(metrics);
    DELETE(metricsTerminator);
    DELETE(logFile);
    DELETE(debugLogger_);
}

/*void Profiler::onThreadStartLocked(JNIEnv *jniEnv, const jvmtiThreadInfo &info, const pthread_t pThreadId) {
    // Avoid tracking the opsian thread
    if (strcmp(info.name, PROCESSOR_THREAD_NAME) != 0
     && strcmp(info.name, METRICS_THREAD_NAME) != 0
     && strcmp(info.name, "Signal Dispatcher") != 0) {

        threadScanner->onThreadStart(jniEnv, info, pThreadId);
    }

    if (validate(pThreadId)) {
        pThreadIdToJavaThread_->assign(pThreadId, JAVA_THREAD);
    }
}

// Separate from onThreadStartLocked so it doesn't hold a mutex
void Profiler::onThreadStart(JNIEnv *jniEnv, const jvmtiThreadInfo &info, const pthread_t pThreadId) {
    if (jniEnv != nullptr) {
        const int threadId = getTid();

        for (int i = 0; i < PUSH_ATTEMPTS &&
            buffer != nullptr && !shuttingDown.load(std::memory_order_acquire) &&
            !buffer->pushThread(jniEnv, info.name, threadId); i++) {
            usleep(10);
        }

        if (info.name != nullptr && !shuttingDown.load(std::memory_order_acquire)) {
            jvmti_->Deallocate((unsigned char *) info.name);
        }
    }
}

void Profiler::onThreadEnd(const pthread_t pThreadId) {
    if (validate(pThreadId)) {
        pThreadIdToJavaThread_->erase(pThreadId);
    }

    threadScanner->onThreadEnd(pThreadId);
}*/
