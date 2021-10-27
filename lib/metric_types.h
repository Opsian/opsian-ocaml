#ifndef METRIC_TYPES_H
#define METRIC_TYPES_H

#include <string>
#include <vector>
#include "data.pb.h"

using std::string;
using std::vector;

using std::string;

enum class MetricDataType {
    INVALID = 0,
    LONG = 1,
    STRING = 2
};

enum class MetricVariability {
    INVALID = 0,
    CONSTANT = 1,
    MONOTONIC = 2,
    VARIABLE = 3
};

enum class MetricUnit {
    INVALID = 0,
    NONE = 1,
    BYTES = 2,
    TICKS = 3,
    EVENTS = 4,
    STRING = 5,
    HERTZ = 6,
    MILLISECONDS = 7,
    NANOSECONDS = 8
};

struct MetricData {
    MetricDataType type;
    string valueString;
    int64_t valueLong;
};

struct MetricListenerEntry {
    string name;
    MetricVariability variability;
    MetricUnit unit;
    MetricData data;
};

class MetricDataListener {
public:

    virtual void recordEntries(std::vector <MetricListenerEntry> &entries, const long timestampInMs) = 0;

    virtual void recordNotification(const data::NotificationCategory category, const string& payload) = 0;

    virtual ~MetricDataListener() = default;
};

bool isPrefixDisabled(const string& entryName, vector<string>& disabledPrefixes);

#endif // METRIC_TYPES_H
