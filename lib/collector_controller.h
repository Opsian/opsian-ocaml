#ifndef COLLECTOR_CONTROLLER_H
#define COLLECTOR_CONTROLLER_H

#include "protocol_handler.h"
#include "signal_handler.h"
#include "debug_logger.h"

#include <functional>
#include <vector>
#include <string>

#include "metrics.h"
#include "terminator.h"
#include "gen/data.pb.h"
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/util/delimited_message_util.h>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

using std::vector;
using std::string;

using google::protobuf::io::ArrayOutputStream;
using google::protobuf::util::SerializeDelimitedToZeroCopyStream;

namespace asio = boost::asio;
using asio::ip::tcp;
using boost::system::error_code;
using asio::steady_timer;

struct StashedNotfication {
    data::NotificationCategory category;
    string payload;
};

class CollectorController : public CollectorMessageListener {

public:
    static std::atomic<uint32_t> threadIdDenials;

    explicit CollectorController(
        Network& network,
        bool on,
        const string& apiKey,
        const string& agentId,
        const string& hostname,
        const string& applicationVersion,
        const string& ocamlVersion,
        uint32_t processorCount,
        const std::function<void()>& socketConnectHandler,
        const std::function<void()>& recordAllocationTable,
        SignalHandler& signalHandler,
        DebugLogger& debugLogger,
        Metrics& metrics,
        Terminator& processorTerminator);

    // on determines whether the profiler is running at all or whether
    // it has been terminated. You can be on, but disconnected for example

    const bool isOn() const;

    const bool isConnected() const;

    virtual void onTerminate();
    
    virtual void onSampleRate(uint64_t processTimeStackSampleRateMillis, uint64_t elapsedTimeStackSampleRateMillis,
                              bool switchProcessTimeProfilingOn, bool switchElapsedTimeProfilingOn, bool threadStateOn,
                              bool switchMemoryProfilingOn, uint64_t memoryProfilingPushRateMillis, bool switchMemoryProfilingStacktraceOn,
                              uint32_t memoryProfilingStackSampleRateSamples, bool switchMetricsOn, uint64_t metricsSampleRateMillis, vector<string>& disabledMetricPrefixes);

    virtual void onHeartbeat();

    void onStart();

    void onEnd();

    void onDisconnect();

    bool poll();

    const uint64_t processTimeStackSampleIntervalMillis() const {
        return processTimeStackSampleIntervalMillis_;
    }

    const uint64_t elapsedTimeStackSampleIntervalMillis() const {
        return elapsedTimeStackSampleIntervalMillis_;
    }

    virtual ~CollectorController();

    bool isThreadStateOn();

    void stashNotification(data::NotificationCategory category, const string& payload);

private:

    enum State {
        // hello not sent:
        SOCKET_CONNECTED = 1,

        // sent hello, but no reply acknowledged
        SENT_HELLO = 2,

        // connected, everything grand
        CONNECTED = 3,

        // need to attempt a reconnected
        DISCONNECTED = 4,
    };

    void onSocketConnect();

    void onSentHello();

    void onFirstSampleRate();

    void sendHello();

    void sendHeartbeat();

    bool attemptConnect();

    void scheduleSendTimer();

    void scheduleReceiveTimer();

    void scheduleAllocationTimer();

    void scheduleAgentStatisticsTimer();

    void onMessage();

    void onSendTimer(const error_code& ec);

    void onReceiveTimer(const error_code& ec);

    void onAllocationTimer(const error_code& ec);

    void onAgentStatisticsTimer(const error_code& ec);

    void recordWithSize(data::AgentEnvelope& agentEnvelope);

    void backoff();

    vector<StashedNotfication> notifications;

    uint32_t backoffTimeInMs_;

    steady_timer* sendTimer_;

    steady_timer* receiveTimer_;

    steady_timer* allocationTimer_;

    steady_timer* agentStatisticsTimer_;

    Network& network_;

    State state_;

    bool on_;

    const string apiKey_;

    const string agentId_;

    const string hostname_;

    const string applicationVersion_;

    const string ocamlVersion_;

    const uint32_t processorCount;

    const std::function<void()> socketConnectHandler_;

    const std::function<void()> recordAllocationTable_;

    SignalHandler& signalHandler_;

    DebugLogger& debugLogger_;

    Metrics& metrics_;

    uint64_t processTimeStackSampleIntervalMillis_;

    uint64_t elapsedTimeStackSampleIntervalMillis_;

    uint64_t memoryProfilingPushRateMillis_;

    Terminator& terminator_;

    bool threadStateOn_;

    bool memoryProfilingOn_;

    bool metricsOn_;

    uint64_t metricsSampleRateMillis_;

    DISALLOW_COPY_AND_ASSIGN(CollectorController);

    void sendStashedNotifications();

    void resetAgentStatisticsCounters();
};

#endif // COLLECTOR_CONTROLLER_H
