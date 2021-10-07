// Somewhat originally dervied from:
// http://www.codeproject.com/Articles/43510/Lock-Free-Single-Producer-Single-Consumer-Circular

// Multiple Producer, Single Consumer Queue

#ifndef CIRCULAR_QUEUE_H
#define CIRCULAR_QUEUE_H

#include "globals.h"
#include "metric_types.h"
#include "data.pb.h"
#include <ctime>
#include <cstring>
#include <cstddef>
#include <vector>
#include "internal_queue.h"

using std::string;
using std::vector;

struct MetricSample {
  uint32_t id;
  MetricData data;
};

struct MetricInformation {
  string name;
  uint32_t id;
  MetricVariability variability; 
  MetricDataType dataType;
  MetricUnit unit;
};

class QueueListener {
public:
    virtual void
    recordStackTrace(
            const timespec &ts,
            const CallTrace &item,
            int signum,
            int threadState,
            uint64_t time_tsc) = 0;

    virtual void
    recordThread(int threadId, const string& name) = 0;

    virtual void
    recordAllocation(uintptr_t allocationSize, bool outsideTlab, VMSymbol* symbol) = 0;

    virtual void
    recordNotification(data::NotificationCategory category, const string &payload, int value) = 0;

    virtual void
    recordMetricInformation(const MetricInformation& metricInformation) = 0;

    virtual void
    recordMetricSamples(const timespec& ts, const vector<MetricSample>& metricSamples) = 0;

    virtual void
    recordConstantMetricsComplete() = 0;

    virtual ~QueueListener() {
    }
};

enum ElementType {
    NOTIFICATION,
    METRIC_INFORMATION,
    METRIC_SAMPLES,
    CONSTANT_METRICS_COMPLETE,
};

struct Holder {

    Holder()
        : is_committed(UNCOMMITTED),
          elementType(NOTIFICATION),
          category(),
          payload(),
          value(0),
          metricInformation(),
          tspec(),
          metricSamples() {}

    // Common
    std::atomic<int> is_committed;
    ElementType elementType;

    // Notification
    data::NotificationCategory category;
    string payload;
    int value;

    // MetricInformation
    MetricInformation metricInformation;

    // MetricSamples
    timespec tspec;
    vector<MetricSample> metricSamples;
};

struct AllocationHolder {
    AllocationHolder() :
        is_committed(UNCOMMITTED),
        allocationSize(0),
        outsideTlab(false) {}

    std::atomic<int> is_committed;
    VMSymbol* symbol;
    uintptr_t allocationSize;
    bool outsideTlab;
};

enum StackElementType {
    STACK_TRACE,
    THREAD
};

struct StackHolder {
    StackHolder() :
        is_committed(UNCOMMITTED),
        elementType(STACK_TRACE),

        tspec(),
        trace(),
        signum(0),
        threadState(0),

        threadId(0),
        name() {}

    // Common
    std::atomic<int> is_committed;
    StackElementType elementType;

    // Stack Trace
    timespec tspec;
    CallTrace trace;
    int signum;
    int threadState;
    uint64_t time_tsc;

    // Thread
    int threadId;
    string name;
};

class CircularQueue {
public:
    static const size_t MainQueueSize = 2048;
    static const size_t AllocationQueueSize = 32768;
    static const size_t StackQueueSize = 2048;

    typedef InternalQueue<Holder, MainQueueSize> MainQueue;
    typedef InternalQueue<AllocationHolder, AllocationQueueSize> AllocationQueue;
    typedef InternalQueue<StackHolder, StackQueueSize> StackQueue;

    static const size_t StackCapacity = StackQueue::Capacity;

    static std::atomic<uint32_t> allocationFailures;
    static std::atomic<uint32_t> allocationStackTraceFailures;
    static std::atomic<uint32_t> cputimeFailures;
    static std::atomic<uint32_t> wallclockFailures;
    static std::atomic<uint32_t> metricFailures;

    explicit CircularQueue(int maxFrameSize) : mainQueue() {
        for (int i = 0; i < StackCapacity; ++i) {
            frame_buffer_[i] = new CallFrame[maxFrameSize]();
        }
    }

    ~CircularQueue() {
        for (int i = 0; i < StackCapacity; ++i) {
            delete[] frame_buffer_[i];
        }
    }

    bool pushStackTrace(CallTrace item, int signum, int threadState, uint64_t time_tsc);

    bool pushThread(const char* name, int threadId);

    bool pushAllocation(uintptr_t allocationSize, bool outsideTlab, VMSymbol* symbol);

    bool pushNotification(data::NotificationCategory category, const char* payload);
    bool pushNotification(data::NotificationCategory category, const char* payload, const int value);

    bool pushMetricInformation(const MetricInformation& info);
    bool pushMetricSamples(vector<MetricSample>& metricSamples, const timespec& time);

    bool pushConstantMetricsComplete();

    bool pop(QueueListener &listener);

private:
    MainQueue mainQueue;
    AllocationQueue allocationQueue;
    StackQueue stackQueue;

    CallFrame *frame_buffer_[StackCapacity];

    void write(const CallTrace& item, size_t slot, StackHolder& holder);
};

#endif /* CIRCULAR_QUEUE_H */
