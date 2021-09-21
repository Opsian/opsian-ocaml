#ifndef OPSIAN_OCAML_EVENT_RING_READER_H
#define OPSIAN_OCAML_EVENT_RING_READER_H

#include <atomic>
#include "metric_types.h"

class EventRingReader {

public:
    EventRingReader(vector<string>& disabledPrefixes);

    void read(MetricDataListener& listener);

    void updateEntryPrefixes(vector<string>& disabledPrefixes);

    const bool hasEmittedConstantMetrics();

private:

    // Written on processor thread, read on metrics thread and processor thread
    std::atomic_bool enabled_;
    std::atomic_bool calledStart_;

    bool hasEmittedConstantMetrics_;

    void emitConstantMetrics(MetricDataListener &listener) const;
};

#endif //OPSIAN_OCAML_EVENT_RING_READER_H
