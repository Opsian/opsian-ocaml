#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include "globals.h"
#include "network.h"

#include <vector>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <string>

#include <google/protobuf/io/coded_stream.h>

using std::vector;
using std::string;

namespace asio = boost::asio;
using asio::ip::tcp;
using boost::system::error_code;

using google::protobuf::io::CodedInputStream;

using data::CollectorEnvelope;

const int MAX_BUFFER_SIZE = 1024;

class CollectorMessageListener {

public:
    virtual void onTerminate() = 0;
    
    virtual void onSampleRate(uint64_t processTimeStackSampleRateMillis, uint64_t elapsedTimeStackSampleRateMillis,
                              bool switchProcessTimeProfilingOn, bool switchElapsedTimeProfilingOn, bool threadStateOn,
                              bool switchMemoryProfilingOn, uint64_t memoryProfilingPushRateMillis,
                              bool switch_memory_profiling_stacktrace_on, uint32_t memory_profiling_stack_sample_rate_samples, 
                              bool switchMetricsOn, uint64_t metricsSampleRateMillis, vector<string>& metricsPrefixes) = 0;

    virtual void onHeartbeat() = 0;

    virtual ~CollectorMessageListener() = default;
};

class ProtocolHandler {

public:
    explicit ProtocolHandler(
        Network& network,
        CollectorMessageListener& listener,
        DebugLogger& debugLogger)
        : network_(network),
          buffer(MAX_BUFFER_SIZE),
          size(0),
          offset(0),
          collectorEnvelope(),
          listener_(listener),
          debugLogger_(debugLogger) {
    }

    void attemptRead();

private:
    Network& network_;

    vector<uint8_t> buffer;

    unsigned int size;

    int offset;

    CollectorEnvelope collectorEnvelope;
    
    CollectorMessageListener& listener_;

    DebugLogger& debugLogger_;

    void onReadHeader(const error_code& err);

    void onReadBody(const error_code& err);

    DISALLOW_COPY_AND_ASSIGN(ProtocolHandler);
};

#endif // PROTOCOL_HANDLER_H
