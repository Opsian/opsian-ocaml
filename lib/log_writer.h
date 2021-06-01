#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <cstring>

#include "circular_queue.h"
#include "network.h"

#include "gen/data.pb.h"
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

#include <boost/functional/hash.hpp>
#include <boost/asio.hpp>

using std::unordered_set;
using std::unordered_map;
using std::vector;
using std::pair;
using std::make_pair;

using google::protobuf::io::CodedOutputStream;
using google::protobuf::io::ZeroCopyOutputStream;

namespace asio = boost::asio;
using asio::ip::tcp;

struct ThreadInformation {
    int threadId;
    string name;
};

struct AllocationRow {
    uint numberofAllocations;
    uintptr_t totalBytesAllocated;

    AllocationRow() : numberofAllocations(0), totalBytesAllocated(0) {
    }
};

typedef pair<VMSymbol*, bool> AllocationKey;
typedef unordered_map<AllocationKey, AllocationRow, boost::hash<AllocationKey>> AllocationsTable;

class LogWriter : public QueueListener {

public:
    explicit LogWriter(
        ZeroCopyOutputStream* output,
        Network& network,
        CollectorController& controller,
        DebugLogger& debugLogger,
        int maxFramesToCapture,
        bool logCorruption)
            : output_(output),
              network_(network),
              controller_(controller),
              threadIdToInformation(),
              allocationsTable(),
              frameAgentEnvelope_(),
              nameAgentEnvelope_(),
              debugLogger_(debugLogger),
              maxFramesToCapture_(maxFramesToCapture),
              logCorruption_(logCorruption) {
        GOOGLE_PROTOBUF_VERIFY_VERSION;

        initDwarf();
    }

    // override
    virtual void
    recordStackTrace(
            const timespec &ts,
            const CallTrace &trace,
            int signum,
            int threadState,
            int wallclockScanId,
            uint64_t time_tsc);

    // override
    virtual void recordThread(
            int threadId,
            const string& name);

    // override
    virtual void
    recordAllocation(uintptr_t allocationSize, bool outsideTlab, VMSymbol* symbol);

    // override
    virtual void
    recordNotification(data::NotificationCategory category, const string &payload, int value);

    // override
    virtual void recordMetricInformation(const MetricInformation& metricInformation);

    // override
    virtual void recordMetricSamples(const timespec&ts, const vector<MetricSample>& metricSamples);

    // override
    virtual void recordAllocationTable();

    // override
    virtual void recordConstantMetricsComplete();

private:
    ZeroCopyOutputStream* output_;

    Network& network_;

    CollectorController& controller_;

    unordered_map<int, ThreadInformation> threadIdToInformation;

    AllocationsTable allocationsTable;

    // we overlap the process of creating name based messages and frame based messages
    // So allocate separate agent envelope objects, otherwise there's a risk that the frame
    // gets free'd when a new method or module message comes in.
    data::AgentEnvelope frameAgentEnvelope_;

    data::AgentEnvelope nameAgentEnvelope_;

    DebugLogger& debugLogger_;

    int maxFramesToCapture_;

    bool logCorruption_;

    void recordWithSize(data::AgentEnvelope& envelope);

    void initDwarf();

    /*void recordFrame(
        jint bci,
        jmethodID methodId,
        data::CompressedFrameEntry* frameEntry);*/

    void setSampleTime(const timespec &ts, data::StackSample *stackSample) const;

    void setSampleType(int signum, data::StackSample *stackSample) const;

    DISALLOW_COPY_AND_ASSIGN(LogWriter);
};

#endif // LOG_WRITER_H
