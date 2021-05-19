#ifndef METRICS_H
#define METRICS_H

#include <vector>
#include <map>

#include "cpudata_reader.h"
#include "metric_types.h"
#include "circular_queue.h"
#include "terminator.h"
#include "log_writer.h"

#include <boost/thread/mutex.hpp>

using std::vector;
using std::map;

static const char *const METRICS_THREAD_NAME = "Opsian Metrics";

static const int DEFAULT_METRICS_SAMPLE_RATE_MILLIS = 1000;

class Metrics {
public:
    explicit Metrics(Terminator& terminator, DebugLogger& debugLogger, CircularQueue& queue)
    : terminator_(terminator),
      debugLogger_(debugLogger),
      queue_(queue),
      threadRunning(false),
      mustSendDurationMetric(false),
      enabled_(false),
      sampleRateMillis_(DEFAULT_METRICS_SAMPLE_RATE_MILLIS),
      cpudataReader_(nullptr),
      readersMutex(),
      metricNameToId(),
      needsToSendConstantMetrics(false) {}

    ~Metrics();

    void startThread();

    void run();

    void stopThread();

    bool isRunning();

    void enable(vector<string>& disabledPrefixes);

    void disable();

    void setSampleRate(uint64_t sampleRateMillis);

private:
    Terminator& terminator_;
    DebugLogger& debugLogger_;
    CircularQueue& queue_;
    std::atomic_bool threadRunning;
    std::atomic_bool mustSendDurationMetric;
    bool enabled_;
    std::atomic<uint64_t> sampleRateMillis_;
    CPUDataReader* cpudataReader_;
    // Mutex can be held on the processor thread or metrics thread
    // Should not hold this mutex whilst retrying the enqueuing as that could deadlock with
    // the processor thread
    boost::mutex readersMutex;
    unordered_map<string, uint32_t> metricNameToId;
    bool needsToSendConstantMetrics;
};

#endif // METRICS_H
