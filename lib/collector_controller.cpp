#include "collector_controller.h"

#include "network.h"
#include "proc_scanner.h"

#include <boost/chrono.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

using boost::chrono::milliseconds;
using boost::chrono::system_clock;
using boost::chrono::duration_cast;

// TODO: make this be configurable
static const int TIMEOUT_IN_MS = 10000;
static const int AGENT_STATISTICS_TIMEOUT_IN_MS = 5000;
static const int SEND_TIMEOUT_IN_MS = TIMEOUT_IN_MS / 4;
static const uint32_t MAX_BACKOFF_TIME_IN_MS = 60 * 1000;
static const uint32_t MIN_BACKOFF_TIME_IN_MS = 1000;
static const uint32_t RANDOMISATION_RANGE_IN_MS = 1000;
static const double BACKOFF_FACTOR = 1.5;
static const int DONT_CHANGE_SAMPLE_RATE = 0;
#define DEFAULT_METRICS_SAMPLE_RATE 1000
#define DEFAULT_PROMETHEUS_PROCESS_SAMPLE_RATE 100
#define DEFAULT_PROMETHEUS_ELAPSED_SAMPLE_RATE 100

std::atomic<uint32_t> CollectorController::threadIdDenials(0);

CollectorController::CollectorController(
    Network &network,
    const bool on,
    const string& apiKey,
    const string& agentId,
    const string& hostname,
    const string& applicationVersion,
    const string& ocamlVersion,
    const uint32_t processorCount,
    const std::function<void()>& socketConnectHandler,
    const std::function<void()>& recordAllocationTable,
    SignalHandler& signalHandler,
    DebugLogger& debugLogger,
    Metrics& metrics,
    const bool prometheusEnabled)
    : notifications(),
      backoffTimeInMs_(0),
      sendTimer_(new steady_timer(getIos())),
      receiveTimer_(new steady_timer(getIos())),
      allocationTimer_(new steady_timer(getIos())),
      agentStatisticsTimer_(new steady_timer(getIos())),
      network_(network),
      state_(prometheusEnabled ? PROMETHEUS_MODE : DISCONNECTED),
      on_(on),
      apiKey_(apiKey),
      agentId_(agentId),
      hostname_(hostname),
      applicationVersion_(applicationVersion),
      ocamlVersion_(ocamlVersion),
      processorCount(processorCount),
      socketConnectHandler_(socketConnectHandler),
      recordAllocationTable_(recordAllocationTable),
      signalHandler_(signalHandler),
      debugLogger_(debugLogger),
      metrics_(metrics),
      processTimeStackSampleIntervalMillis_(0),
      elapsedTimeStackSampleIntervalMillis_(0),
      memoryProfilingPushRateMillis_(0),
      threadStateOn_(false),
      memoryProfilingOn_(false),
      metricsOn_(false),
      metricsSampleRateMillis_(DEFAULT_METRICS_SAMPLE_RATE) {
}

void CollectorController::onTerminate() {
    on_ = false;

    // cleanup code run in onEnd()

    onMessage();
}

void CollectorController::startProfiling(
    uint64_t processTimeStackSampleRateMillis,
    uint64_t elapsedTimeStackSampleRateMillis,
    bool switchProcessTimeProfilingOn,
    bool switchElapsedTimeProfilingOn) {

    if (switchProcessTimeProfilingOn) {
        if (processTimeStackSampleRateMillis != DONT_CHANGE_SAMPLE_RATE)
        {
            signalHandler_.updateProcessInterval(static_cast<int>(processTimeStackSampleRateMillis));
            processTimeStackSampleIntervalMillis_ = processTimeStackSampleRateMillis;
        }
    } else if (signalHandler_.isProcessProfiling()) {
        signalHandler_.stopProcessProfiling();
    }

    if (switchElapsedTimeProfilingOn) {
        if (elapsedTimeStackSampleRateMillis != DONT_CHANGE_SAMPLE_RATE)
        {
            update_scanning_threads_interval(elapsedTimeStackSampleRateMillis);
            elapsedTimeStackSampleIntervalMillis_ = elapsedTimeStackSampleRateMillis;
        }
    } else if (is_scanning_threads()) {
        stop_scanning_threads();
    }
}

void CollectorController::onSampleRate(
    uint64_t processTimeStackSampleRateMillis,
    uint64_t elapsedTimeStackSampleRateMillis,
    bool switchProcessTimeProfilingOn,
    bool switchElapsedTimeProfilingOn,
    bool threadStateOn,
    bool switchMemoryProfilingOn,
    uint64_t memoryProfilingPushRateMillis,
    bool switchMemoryProfilingStacktraceOn,
    uint32_t memoryProfilingStackSampleRateSamples,
    bool switchMetricsOn,
    uint64_t metricsSampleRateMillis,
    vector<string>& disabledMetricPrefixes) {

    startProfiling(processTimeStackSampleRateMillis, elapsedTimeStackSampleRateMillis, switchProcessTimeProfilingOn,
        switchElapsedTimeProfilingOn);

    if (switchMetricsOn) {
        if (metricsSampleRateMillis != DONT_CHANGE_SAMPLE_RATE) {
            metrics_.setSampleRate(metricsSampleRateMillis);
        }

        metrics_.enable(disabledMetricPrefixes);
    } else {
        metrics_.disable();
    }

    memoryProfilingOn_ = switchMemoryProfilingOn;
    threadStateOn_ = threadStateOn;
    metricsOn_ = switchMetricsOn;

    // logged since setting the field off just means it gets ignored in DebugString()
    debugLogger_    << " switchProcessTimeProfilingOn: "
                    << switchProcessTimeProfilingOn
                    << " switchElapsedTimeProfilingOn: "
                    << switchElapsedTimeProfilingOn
                    << " threadStateOn: "
                    << threadStateOn
                    << " switchMemoryProfilingOn: "
                    << switchMemoryProfilingOn
                    << endl;

    if (state_ == SENT_HELLO) {
        onFirstSampleRate();
    }

    onMessage();
}

void CollectorController::onHeartbeat() {
    debugLogger_ << "onHeartbeat" << endl;

    onMessage();
    scheduleReceiveTimer();
}

void CollectorController::onStart() {
    if (isOn() && state_ == SOCKET_CONNECTED) {
        onSocketConnect();
    } else if (state_ == PROMETHEUS_MODE) {
        startProfiling(DEFAULT_PROMETHEUS_PROCESS_SAMPLE_RATE, DEFAULT_PROMETHEUS_ELAPSED_SAMPLE_RATE, true, true);
    }
}

void CollectorController::onEnd() {
    signalHandler_.stopProcessProfiling();
    /*if (memoryProfilingOn_) {
        MemoryProfiler::stop();
    }*/

    onDisconnect();
}

void CollectorController::onSocketConnect() {
    debugLogger_ << "onSocketConnect" << endl;

    state_ = SOCKET_CONNECTED;

    backoffTimeInMs_ = 0;

    socketConnectHandler_();

    sendHello();
    onSentHello();
}

void CollectorController::onSentHello() {
    debugLogger_ << "onSentHello" << endl;

    state_ = SENT_HELLO;

    resetAgentStatisticsCounters();

    scheduleReceiveTimer();
}

void CollectorController::resetAgentStatisticsCounters() {
    CircularQueue::allocationFailures.store(0);
    CircularQueue::allocationStackTraceFailures.store(0);
    CircularQueue::cputimeFailures.store(0);
    CircularQueue::wallclockFailures.store(0);
    CircularQueue::metricFailures.store(0);
    threadIdDenials.store(0);
    CPUDataReader::errors.store(0);
    CPUDataReader::warnings.store(0);
}

void CollectorController::onFirstSampleRate() {
    debugLogger_ << "onFirstSampleRate" << endl;

    state_ = CONNECTED;

    sendStashedNotifications();

    scheduleSendTimer();
    scheduleReceiveTimer();
    scheduleAgentStatisticsTimer();
}

void CollectorController::onDisconnect() {
    debugLogger_ << "onDisconnect" << endl;

    state_ = DISCONNECTED;

    sendTimer_->cancel();
    receiveTimer_->cancel();
    allocationTimer_->cancel();
    agentStatisticsTimer_->cancel();

    if (metricsOn_) {
      metrics_.disable();
      metricsOn_ = false;
    }

    network_.close();
}

bool CollectorController::attemptConnect() {
    debugLogger_ << "attemptConnect" << endl;
    if (network_.connect()) {
        onSocketConnect();

        return true;
    }

    return false;
}

bool CollectorController::poll() {
    switch(state_) {
        case DISCONNECTED: {
            backoff();
            return attemptConnect();
        }

        default: {
            return false;
        }
    }
}

void CollectorController::sendHello() {
    data::AgentEnvelope agentEnvelope;
    auto hello = agentEnvelope.mutable_hello();
    hello->set_api_key(apiKey_);
    hello->set_process_id(getpid());

    auto context = hello->mutable_context();
    if (!agentId_.empty()) {
        context->set_agent_id(agentId_);
    }
    context->set_agent_version(AGENT_VERSION);
    context->set_agent_type(data::AgentType::OCAML);
    context->set_host_name(hostname_);
    context->set_platform_version(ocamlVersion_);
    context->set_hardware_thread_count(processorCount);
    if (!applicationVersion_.empty()) {
        context->set_application_version(applicationVersion_);
    }
    context->set_agent_git_string(GIT_STR);
    context->set_build_type(BUILD_TYPE);

    time_t currentTime = time(nullptr);
    tm tm;
    localtime_r(&currentTime, &tm);
    context->set_timezone_name(tm.tm_zone);
    context->set_timezone_offset_seconds(tm.tm_gmtoff);
    context->set_personality(data::PERS_OCAML);

    recordWithSize(agentEnvelope);
}

void CollectorController::sendHeartbeat() {
    data::AgentEnvelope agentEnvelope;
    auto heartbeat = agentEnvelope.mutable_heartbeat();

    auto duration = system_clock::now().time_since_epoch();
    auto millis = duration_cast<milliseconds>(duration).count();
    heartbeat->set_time(millis);

    recordWithSize(agentEnvelope);
}

void CollectorController::scheduleSendTimer() {
    sendTimer_->expires_from_now(milliseconds(SEND_TIMEOUT_IN_MS));
    sendTimer_->async_wait(
        boost::bind(
            &CollectorController::onSendTimer,
            this,
            asio::placeholders::error));
}

void CollectorController::scheduleReceiveTimer() {
    receiveTimer_->expires_from_now(milliseconds(TIMEOUT_IN_MS));
    receiveTimer_->async_wait(
        boost::bind(
            &CollectorController::onReceiveTimer,
            this,
            asio::placeholders::error));
}

void CollectorController::scheduleAllocationTimer() {
    allocationTimer_->expires_from_now(milliseconds(memoryProfilingPushRateMillis_));
    allocationTimer_->async_wait(
            boost::bind(
                    &CollectorController::onAllocationTimer,
                    this,
                    asio::placeholders::error));
}

void CollectorController::scheduleAgentStatisticsTimer() {
    agentStatisticsTimer_->expires_from_now(milliseconds(AGENT_STATISTICS_TIMEOUT_IN_MS));
    agentStatisticsTimer_->async_wait(
            boost::bind(
                    &CollectorController::onAgentStatisticsTimer,
                    this,
                    asio::placeholders::error));
}

void CollectorController::onSendTimer(const error_code& ec) {
    if (ec != asio::error::operation_aborted) {
        sendHeartbeat();
        scheduleSendTimer();
    }
}

void CollectorController::onReceiveTimer(const error_code& ec) {
    if (ec != asio::error::operation_aborted) {
        onDisconnect();
        scheduleReceiveTimer();
    }
}

void CollectorController::onAllocationTimer(const error_code& ec) {
    if (ec != asio::error::operation_aborted) {
        debugLogger_ << "recording allocation table timer called" << endl;
        recordAllocationTable_();
        scheduleAllocationTimer();
    } else {
      debugLogger_ << "operation aborted when recording allocation table" << endl;
    }
}

void CollectorController::onAgentStatisticsTimer(const error_code &ec) {
    if (ec != asio::error::operation_aborted) {
        debugLogger_ << "agent statistics timer called" << endl;

        data::AgentEnvelope agentEnvelope;
        data::AgentStatistics* agentStatistics = agentEnvelope.mutable_agent_statistics();
        agentStatistics->set_allocation_enqueue_failures(CircularQueue::allocationFailures);
        agentStatistics->set_cputime_enqueue_failures(CircularQueue::cputimeFailures);
        agentStatistics->set_wallclock_enqueue_failures(CircularQueue::wallclockFailures);
        agentStatistics->set_thread_id_denials(CollectorController::threadIdDenials);
        agentStatistics->set_cpu_metric_errors(CPUDataReader::errors);
        agentStatistics->set_cpu_metric_warnings(CPUDataReader::warnings);
        agentStatistics->set_metric_enqueue_failures(CircularQueue::metricFailures);
        agentStatistics->set_allocation_stack_trace_enqueue_failures(CircularQueue::allocationStackTraceFailures);
        recordWithSize(agentEnvelope);

        scheduleAgentStatisticsTimer();
    }
}

const bool CollectorController::isOn() const {
    return on_;
}

const bool CollectorController::isConnected() const {
    return state_ == CONNECTED;
}

const bool CollectorController::isActive() const {
    return isConnected() || state_ == PROMETHEUS_MODE;
}

void CollectorController::onMessage() {
    // scheduling a new timer cancels the previously scheduled timer
}

void CollectorController::recordWithSize(data::AgentEnvelope& agentEnvelope) {
    network_.sendWithSize(*this, agentEnvelope);
}

boost::random::mt19937 gen((uint32_t) std::time(NULL));
boost::random::uniform_int_distribution<uint32_t> dist(0, RANDOMISATION_RANGE_IN_MS);

void CollectorController::backoff() {
    // Don't backoff first attempt, otherwise double the backoff with a max
    if (backoffTimeInMs_ == 0) {
        backoffTimeInMs_ = MIN_BACKOFF_TIME_IN_MS;
        return;
    } else {
        backoffTimeInMs_ = (uint32_t ) ((double) backoffTimeInMs_ * BACKOFF_FACTOR);
        backoffTimeInMs_ = std::min(backoffTimeInMs_, MAX_BACKOFF_TIME_IN_MS);
    }

    uint32_t randomNumber = dist(gen);
    uint32_t randomisedBackoffTime = backoffTimeInMs_+ randomNumber;

    // Total failure to get sprintf to work with this
    std::stringstream ss;
    ss << "Backing off for "<< randomisedBackoffTime << "ms before reconnecting";
    logError(ss.str().c_str());

    sleep_ms(randomisedBackoffTime);
}

CollectorController::~CollectorController() {
    delete sendTimer_;
    delete receiveTimer_;
    delete allocationTimer_;
    delete agentStatisticsTimer_;
}

bool CollectorController::isThreadStateOn() {
    return threadStateOn_;
}

void CollectorController::stashNotification(data::NotificationCategory category, const string& payload) {
    StashedNotfication notfication = {
        category,
        payload
    };

    notifications.push_back(notfication);
}

void CollectorController::sendStashedNotifications() {
    data::AgentEnvelope agentEnvelope;
    vector<StashedNotfication>::iterator it = notifications.begin();
    while (it != notifications.end()) {
        StashedNotfication& stashedNotification = *it;

        data::Notification* notification = agentEnvelope.mutable_notification();
        notification->set_category(stashedNotification.category);
        notification->set_payload(stashedNotification.payload);
        network_.sendWithSize(*this, agentEnvelope);

        it++;
    }

    notifications.clear();
}

void CollectorController::on_fork() {
    notifications.clear();
    backoffTimeInMs_ = 0;
    state_ = DISCONNECTED;
    processTimeStackSampleIntervalMillis_ = 0;
    elapsedTimeStackSampleIntervalMillis_ = 0;
    memoryProfilingPushRateMillis_ = 0;
    threadStateOn_ = false;
    memoryProfilingOn_ = false;
    metricsOn_ = false;
    metricsSampleRateMillis_ = DEFAULT_METRICS_SAMPLE_RATE;
}
