#include "circular_queue.h"
#include <unistd.h>
#include <ctime>

std::atomic<uint32_t> CircularQueue::allocationFailures(0);
std::atomic<uint32_t> CircularQueue::allocationStackTraceFailures(0);
std::atomic<uint32_t> CircularQueue::cputimeFailures(0);
std::atomic<uint32_t> CircularQueue::wallclockFailures(0);
std::atomic<uint32_t> CircularQueue::metricFailures(0);

bool CircularQueue::pushStackTrace(
        CallTrace item,
        int signum,
        int threadState,
        uint64_t time_tsc) {

    timespec ts;
    // Cannot use C++11 version as not supported in GCC 4.4
    clock_gettime(CLOCK_REALTIME, &ts);

    size_t currentInput;
    if (!stackQueue.acquireWrite(currentInput)) {
        return false;
    }

    StackHolder& holder = stackQueue.get(currentInput);
    write(item, currentInput, holder);
    holder.elementType = STACK_TRACE;
    holder.tspec.tv_sec = ts.tv_sec;
    holder.tspec.tv_nsec = ts.tv_nsec;
    holder.signum = signum;
    holder.threadState = threadState;
    holder.time_tsc = time_tsc;
    stackQueue.commitWrite(holder);

    return true;
}

bool CircularQueue::pushThread(const char *name, int threadId) {
    size_t currentInput;
    if (!stackQueue.acquireWrite(currentInput)) {
        return false;
    }

    StackHolder& holder = stackQueue.get(currentInput);
    holder.elementType = THREAD;
    holder.name = name;
    holder.threadId = threadId;
    stackQueue.commitWrite(holder);

    return true;
}

bool CircularQueue::pushAllocation(const uintptr_t allocationSize, const bool outsideTlab, VMSymbol* symbol) {
    size_t currentInput;
    if (!allocationQueue.acquireWrite(currentInput)) {
        allocationFailures++;
        return false;
    }

    AllocationHolder& holder = allocationQueue.get(currentInput);
    holder.allocationSize = allocationSize;
    holder.outsideTlab = outsideTlab;
    holder.symbol = symbol;
    allocationQueue.commitWrite(holder);

    return true;
}

bool CircularQueue::pushNotification(
    data::NotificationCategory category,
    const char* payload) {

    return pushNotification(category, payload, 0);
}

bool CircularQueue::pushNotification(
    data::NotificationCategory category,
    const char* payload,
    const int value) {

    size_t currentInput;
    if (!mainQueue.acquireWrite(currentInput)) {
        return false;
    }

    Holder& holder = mainQueue.get(currentInput);
    holder.elementType = NOTIFICATION;
    holder.category = category;
    holder.payload = payload;
    holder.value = value;
    mainQueue.commitWrite(holder);;

    return true;
}

bool CircularQueue::pushMetricInformation(
    const MetricInformation& info) {

    size_t currentInput;

    if (!mainQueue.acquireWrite(currentInput)) {
        metricFailures++;
        return false;
    }

    Holder& holder = mainQueue.get(currentInput);
    holder.elementType = METRIC_INFORMATION;
    holder.metricInformation = info;
    mainQueue.commitWrite(holder);;

    return true;
}

bool CircularQueue::pushMetricSamples(vector<MetricSample>& metricSamples, const long time_epoch_millis) {

    size_t currentInput;

    if (!mainQueue.acquireWrite(currentInput)) {
        metricFailures++;
        return false;
    }

    Holder& holder = mainQueue.get(currentInput);
    holder.elementType = METRIC_SAMPLES;
    holder.metricSamples = metricSamples;

    holder.time_epoch_millis = time_epoch_millis;

    mainQueue.commitWrite(holder);

    return true;
}

bool CircularQueue::pushConstantMetricsComplete() {
    size_t currentInput;

    if (!mainQueue.acquireWrite(currentInput)) {
        metricFailures++;
        return false;
    }

    Holder& holder = mainQueue.get(currentInput);
    holder.elementType = CONSTANT_METRICS_COMPLETE;

    mainQueue.commitWrite(holder);

    return true;
}

// Unable to use memcpy inside the push method because its not async-safe
void CircularQueue::write(const CallTrace& item, size_t slot, StackHolder& holder) {
    CallFrame *fb = frame_buffer_[slot];
    for (int frame_num = 0; frame_num < item.num_frames; ++frame_num) {
        fb[frame_num] = item.frames[frame_num];
    }

    holder.trace.frames = fb;
    holder.trace.threadId = item.threadId;
    holder.trace.num_frames = item.num_frames;
}

bool CircularQueue::pop(QueueListener& listener) {
    bool read = false;

    size_t currentOutput;
    if (mainQueue.acquireRead(currentOutput)) {
        read = true;
        Holder& holder = mainQueue.get(currentOutput);
        switch (holder.elementType) {
            case NOTIFICATION: {
                listener.recordNotification(holder.category, holder.payload, holder.value);
                break;
            }

            case METRIC_INFORMATION: {
                listener.recordMetricInformation(holder.metricInformation);
                break;
            }

            case METRIC_SAMPLES: {
                listener.recordMetricSamples(holder.time_epoch_millis, holder.metricSamples);
                break;
            }

            case CONSTANT_METRICS_COMPLETE: {
                listener.recordConstantMetricsComplete();
                break;
            }
        }

        mainQueue.commitRead(currentOutput);
    }

    if (allocationQueue.acquireRead(currentOutput)) {
        read = true;
        AllocationHolder& holder = allocationQueue.get(currentOutput);
        listener.recordAllocation(holder.allocationSize, holder.outsideTlab, holder.symbol);
        holder.symbol = nullptr;
        allocationQueue.commitRead(currentOutput);
    }

    if (stackQueue.acquireRead(currentOutput)) {
        read = true;
        StackHolder& holder = stackQueue.get(currentOutput);
        switch (holder.elementType) {
            case STACK_TRACE: {
                listener.recordStackTrace(
                    holder.tspec,
                    holder.trace,
                    holder.signum,
                    holder.threadState,
                    holder.time_tsc);

                // 0 out all frames so the next write is clean
                CallFrame *fb = frame_buffer_[currentOutput];
                int num_frames = holder.trace.num_frames;
                for (int frame_num = 0; frame_num < num_frames; ++frame_num) {
                    memset(&(fb[frame_num]), 0, sizeof(CallFrame));
                }
                break;
            }

            case THREAD: {
                listener.recordThread(holder.threadId, holder.name);
                break;
            }
        }
        stackQueue.commitRead(currentOutput);
    }

    return read;
}
