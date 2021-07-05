#include "metrics.h"

#include <boost/functional/hash.hpp>
#include <boost/thread/condition_variable.hpp>

#include <unistd.h>

#include <sys/types.h>

#include "proc_scanner.h"

static const string DURATION_NAME = "opsian.metrics.read_duration";
static const uint32_t DURATION_ID = 1;
static const uint32_t NANOS_IN_SECOND = 1000000000;
static const uint64_t MAX_SLEEP_IN_MS = 200;

static const MetricInformation DURATION_INFO{
    DURATION_NAME,
    DURATION_ID,
    MetricVariability::VARIABLE,
    MetricDataType::LONG,
    MetricUnit::MILLISECONDS // TODO: make this nanoseconds
};

void* callbackToRunMetrics(void* arg) {
    // Avoid having the metrics thread also receive the PROF signals
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPROF);
    sigaddset(&mask, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) < 0) {
        logError("ERROR: failed to set metrics thread signal mask\n");
    }

    Metrics* metrics = (Metrics*) arg;
    metrics->run();
    return nullptr;
}

void Metrics::startThread() {
    debugLogger_ << "Starting metrics thread" << endl;

    int result = pthread_create(&thread, nullptr, &callbackToRunMetrics, this);
    if (result) {
        logError("ERROR: failed to start processor thread %d\n", result);
    }
}

// Called on processor thread - don't enqueue
void Metrics::enable(vector<string>& disabledPrefixes) {
    // We're going to need to manipulate the readers here, take the lock
    boost::lock_guard<boost::mutex> guard(readersMutex);

    if (!enabled_) {
        cpudataReader_ = new CPUDataReader(disabledPrefixes);

        mustSendDurationMetric.store(true);
        needsToSendConstantMetrics = true;
    } else {
        cpudataReader_->updateEntryPrefixes(disabledPrefixes);
    }

    enabled_ = true;
}

// Called on processor thread - don't enqueue
void Metrics::disable() {
    // We're going to need to manipulate the readers here, take the lock
    boost::lock_guard<boost::mutex> guard(readersMutex);

    // We were enabled, dispose of the readers
    if (enabled_) {
        delete cpudataReader_;
        cpudataReader_ = nullptr;
        metricNameToId.clear();
    }

    mustSendDurationMetric.store(false);
    enabled_ = false;
}

void Metrics::setSampleRate(uint64_t sampleRateMillis) {
    sampleRateMillis_.store(sampleRateMillis);
}

class InternalDataListener : public MetricDataListener {
public:
    InternalDataListener(unordered_map<string, uint32_t>& metricNameToId, CircularQueue& queue)
            : queue_(queue),
              metricNametoId_(metricNameToId),
              entries_(),
              startCycle_(),
              endCycle_() {}

    virtual void recordEntries(vector<MetricListenerEntry>& entries) {
        entries_.insert(entries_.end(), entries.begin(), entries.end());
    }

    virtual void start() {
        clock_gettime(CLOCK_REALTIME, &startCycle_);
    }

    // We batch up all the metrics we've gathered and then flush them in one go, so we can also batch on the server side
    virtual bool flush() {
        vector<MetricSample> samples;
        vector<MetricListenerEntry> failedConstantEntries;

        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            auto& entry = *it;
            string metricName = string(entry.name);

            uint32_t metricId = findOrCreateMetricId(entry);

            // If the metricId we get back is 0 then we failed to publish the MetricInformation to the buffer
            // which means any samples we send would be unnamed. Instead we discard this sample and on the next
            // iteration we'll try again for the next sample.
            if (metricId == 0) {

                // ... unless it's a constant in which case we keep it around for the next attempt
                if (entry.variability == MetricVariability::CONSTANT) {
                    failedConstantEntries.push_back(entry);
                }

                continue;
            }

            MetricSample sample;
            sample.id = metricId;
            sample.data = entry.data;

            samples.push_back(sample);
        }

        clock_gettime(CLOCK_REALTIME, &endCycle_);

        __time_t durationSec = endCycle_.tv_sec - startCycle_.tv_sec;
        __time_t durationNSec = endCycle_.tv_nsec - startCycle_.tv_nsec;
        __time_t durationInNs = durationNSec + durationSec * NANOS_IN_SECOND;

        MetricSample durationSample{
            DURATION_ID,
            {MetricDataType::LONG, "", durationInNs}
        };
        samples.push_back(durationSample);

        queue_.pushMetricSamples(samples, endCycle_);

        entries_ = failedConstantEntries;

        return failedConstantEntries.empty();
    }

    virtual void recordNotification(const data::NotificationCategory category, const string& payload) {
        queue_.pushNotification(category, payload.c_str());
        (payload.c_str());
    }

private:
    uint32_t findOrCreateMetricId(const MetricListenerEntry& entry) {
        auto mapIt = metricNametoId_.find(entry.name);

        if (mapIt != metricNametoId_.end()) {
            // Found it in the map
            return mapIt->second;
        } else {
            // Didn't find it in the map, we need to create a new one
            // also we need to publish the MetricInformation in to the queue
            uint32_t metricId = (uint32_t) metricNametoId_.size() + 1; // start ids at 1
            metricNametoId_.insert({entry.name, metricId});

            MetricInformation info;
            info.name = entry.name;
            info.id = metricId;
            info.dataType = entry.data.type;
            info.variability = entry.variability;
            info.unit = entry.unit;

            if (queue_.pushMetricInformation(info)) {
                return metricId;
            } else {
                metricNametoId_.erase(entry.name);
                return 0;
            }
        }
    }

    CircularQueue& queue_;
    unordered_map<string, uint32_t>& metricNametoId_;
    vector<MetricListenerEntry> entries_;
    timespec startCycle_;
    timespec endCycle_;
};

void Metrics::run() {
    try {
        InternalDataListener metricListener(metricNameToId, queue_);

        metrics_thread_id = getTid();
        metrics_thread_started.store(true);

        scan_threads();
        while (true) {
            uint64_t durationInMs = sampleRateMillis_.load();
            sleep_ms(durationInMs);

            scan_threads();

            // NB: Don't holding the readersMutex when enqueuing because the retrying of the queue can
            // deadlock with the processor thread.
            if (mustSendDurationMetric.load()) {
                metricNameToId.insert({DURATION_NAME, DURATION_ID});
                while (!queue_.pushMetricInformation(DURATION_INFO)) {
                    sleep_ms(1);
                }

                mustSendDurationMetric.store(false);
            }

            {
                // Take the lock because we're going to be using the readers
                // (also not safe to read enabled without it)
                boost::lock_guard<boost::mutex> guard(readersMutex);

                if (enabled_) {
                    metricListener.start();
                    cpudataReader_->read(metricListener);
                    const bool noRemainingConstantsToSend = metricListener.flush();
                    if (needsToSendConstantMetrics) {
                        if (cpudataReader_->hasEmittedConstantMetrics() &&
                            noRemainingConstantsToSend) {
                            needsToSendConstantMetrics = !queue_.pushConstantMetricsComplete();
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        const char* what = e.what();
        logError(what);
        queue_.pushNotification(data::NotificationCategory::INFO_LOGGING, what);
    }
}

Metrics::~Metrics() {
    delete cpudataReader_;
}
