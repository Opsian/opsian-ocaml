#ifndef OPSIAN_OCAML_EVENT_RING_READER_H
#define OPSIAN_OCAML_EVENT_RING_READER_H

#include "metric_types.h"

class EventRingReader {

public:
    EventRingReader(vector<string>& disabledPrefixes);

    void read(MetricDataListener& listener);

    void updateEntryPrefixes(vector<string>& disabledPrefixes);

    const bool hasEmittedConstantMetrics();

private:
    bool enabled_;

    bool calledStart_;

    bool hasEmittedConstantMetrics_;
};

#endif //OPSIAN_OCAML_EVENT_RING_READER_H
