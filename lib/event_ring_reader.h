#ifndef OPSIAN_OCAML_EVENT_RING_READER_H
#define OPSIAN_OCAML_EVENT_RING_READER_H

#include <atomic>
#include "metric_types.h"

class EventRingReader {

public:
    EventRingReader(vector<string>& disabledPrefixes);

    uint32_t read(MetricDataListener& listener);

    void updateEntryPrefixes(vector<string>& disabledPrefixes);

    void disable();

    const bool hasEmittedConstantMetrics();

private:

    // Written on processor thread, read on metrics thread and processor thread
    std::atomic_bool enabled_;

    bool hasEmittedConstantMetrics_;

    void emitConstantMetrics(MetricDataListener &listener) const;
};

#endif //OPSIAN_OCAML_EVENT_RING_READER_H
