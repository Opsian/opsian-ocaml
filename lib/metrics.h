#ifndef METRICS_H
#define METRICS_H

#include <vector>
#include <map>

#include "cpudata_reader.h"
#include "metric_types.h"
#include "circular_queue.h"
#include "log_writer.h"

#include <boost/thread/mutex.hpp>

using std::vector;
using std::map;

static const char *const METRICS_THREAD_NAME = "Opsian Metrics";

static const int DEFAULT_METRICS_SAMPLE_RATE_MILLIS = 1000;

// Any change on default initialization values should be replicated in Metrics.on_fork()
class Metrics {
public:
    explicit Metrics(DebugLogger& debugLogger, CircularQueue& queue)
    : debugLogger_(debugLogger),
      queue_(queue),
      thread(),
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

    void enable(vector<string>& disabledPrefixes);

    void disable();

    void setSampleRate(uint64_t sampleRateMillis);

    void on_fork();

private:
    DebugLogger& debugLogger_;
    CircularQueue& queue_;
    pthread_t thread;
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
