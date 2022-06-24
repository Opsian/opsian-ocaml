#include "event_ring_reader.h"
#include <cstdlib>
#include "globals.h"

extern "C" {
    #include "caml/version.h"
}

static const string EVENT_RING_NAME = string("ocaml.eventring");
static const string ENABLED_NAME = string("ocaml.eventring.enabled");
static const string RUN_PARAM_NAME = string("ocaml.runparam");
static const string VERSION_PARAM_NAME = string("ocaml.version");

// Definitions provided in events.enabled.cpp / events.disabled.cpp depending upon whether runtime_events is installed
// or not:

bool has_runtime_events();
// true if enabling succeeded
bool enable_runtime_events(const bool wasEnabled, const bool enable);
uint32_t read_runtime_events(MetricDataListener& listener);
void disable_runtime_events();

EventRingReader::EventRingReader(vector<string>& disabledPrefixes)
    : enabled_(false),
      hasEmittedConstantMetrics_(false) {

    updateEntryPrefixes(disabledPrefixes);
}

// Called on processor thread with readersMutex
void EventRingReader::updateEntryPrefixes(vector<string>& disabledPrefixes) {
    const bool wasEnabled = enabled_;
    const bool enable = !isPrefixDisabled(EVENT_RING_NAME, disabledPrefixes);
    enabled_ = enable_runtime_events(wasEnabled, enable);
}

// Called on processor thread with readersMutex
void EventRingReader::disable() {
    if (enabled_) {
        disable_runtime_events();
    }
}

uint32_t EventRingReader::read(MetricDataListener& listener, const long timestampInMs) {
    uint32_t events = 0;
    if (enabled_) {
        if (!hasEmittedConstantMetrics_) {
            emitConstantMetrics(listener, timestampInMs);
            events++;

            hasEmittedConstantMetrics_ = true;
        }

        events += read_runtime_events(listener);
    }

    return events;
}

void EventRingReader::emitConstantMetrics(MetricDataListener &listener, const long timestampInMs) const {
    vector<MetricListenerEntry> constantMetrics{};

    MetricListenerEntry hasEventRingEntry{};
    hasEventRingEntry.name = ENABLED_NAME;
    hasEventRingEntry.unit = MetricUnit::NONE;
    hasEventRingEntry.variability = MetricVariability::CONSTANT;
    hasEventRingEntry.data.type = MetricDataType::LONG;
    hasEventRingEntry.data.valueLong = has_runtime_events();
    constantMetrics.push_back(hasEventRingEntry);

    char* runParam = getenv("OCAMLRUNPARAM");
    if (runParam == nullptr) {
        // Check fallback name
        runParam = getenv("CAMLRUNPARAM");
    }

    if (runParam != nullptr) {
        MetricListenerEntry runParamEvent{};
        runParamEvent.name = RUN_PARAM_NAME;
        runParamEvent.unit = MetricUnit::STRING;
        runParamEvent.variability = MetricVariability::CONSTANT;
        runParamEvent.data.type = MetricDataType::STRING;
        runParamEvent.data.valueString = runParam;
        constantMetrics.push_back(runParamEvent);
    }

    MetricListenerEntry versionEvent{};
    versionEvent.name = VERSION_PARAM_NAME;
    versionEvent.unit = MetricUnit::NONE;
    versionEvent.variability = MetricVariability::CONSTANT;
    versionEvent.data.type = MetricDataType::STRING;
    versionEvent.data.valueString = OCAML_VERSION_STRING;
    constantMetrics.push_back(versionEvent);

    listener.recordEntries(constantMetrics, timestampInMs);
}

const bool EventRingReader::hasEmittedConstantMetrics() {
    return hasEmittedConstantMetrics_;
}
