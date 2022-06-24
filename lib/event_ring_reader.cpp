#include "event_ring_reader.h"
#include <cstdlib>
#include "globals.h"

static const string EVENT_RING_NAME = string("ocaml.eventring");
static const string ENABLED_NAME = string("ocaml.eventring.enabled");
static const string LOST_EVENTS_NAME = string("ocaml.eventring.lost_events");
static const string RUN_PARAM_NAME = string("ocaml.runparam");
static const string VERSION_PARAM_NAME = string("ocaml.version");
static const uint MAX_EVENTS = 60000;

static std::atomic_bool calledStart_(false);

extern "C" {
    #define CAML_NAME_SPACE

    #include "misc.h"

    #include <caml/threads.h> // runtime lock
    #include <caml/mlvalues.h> // Caml_state
    #include "caml/version.h"
}

#if OCAML_VERSION_MAJOR == 5
 #define CAML_HAS_EVENTRING 1
#endif

#ifdef CAML_HAS_EVENTRING

static long MONOTONIC_TO_REALTIME_ADJUSTMENT_IN_MS = 0;
static const int MONITOR_THIS_PROCESS = -1;
static const int WORD_SIZE = sizeof(unsigned);

static const bool hasEventRing_ = true;
extern "C" {
    #include "caml/runtime_events.h"
    #include "caml/runtime_events_consumer.h"
}

#include <unordered_map>
#include <unordered_set>

#define ADD_EVENT(EV) { EV, "ocaml.eventring."#EV }

struct EventState {
    uint64_t beginTimestamp;
    string eventName;
};


// Not static fields to avoid a race between the metrics thread completing and the program shutting down.
const std::unordered_map<ev_runtime_phase, const char*>* REQUIRED_PHASES = nullptr;
const std::unordered_map<ev_runtime_counter, const char*>* REQUIRED_COUNTERS = nullptr;
std::unordered_map<ev_runtime_phase, EventState>* phaseToEventState_ = nullptr;

void init_counters() {
    REQUIRED_PHASES = new std::unordered_map<ev_runtime_phase, const char*> {
        ADD_EVENT(EV_MINOR), // Pause time for minor collection
        ADD_EVENT(EV_MAJOR), // Sum of pause time slices for major collections
    };

    REQUIRED_COUNTERS = new std::unordered_map<ev_runtime_counter, const char*> {
        // Causes of minor collections starting:
        ADD_EVENT(EV_C_FORCE_MINOR_ALLOC_SMALL),
        ADD_EVENT(EV_C_FORCE_MINOR_MAKE_VECT),
        ADD_EVENT(EV_C_FORCE_MINOR_SET_MINOR_HEAP_SIZE),
        ADD_EVENT(EV_C_FORCE_MINOR_WEAK),
        ADD_EVENT(EV_C_FORCE_MINOR_MEMPROF),
        // amount promoted from the minor heap to the major heap on each minor collection in terms of machine words
        ADD_EVENT(EV_C_MINOR_PROMOTED),

        // amount of allocation in the previous minor cycle in terms of machine words
        ADD_EVENT(EV_C_MINOR_ALLOCATED),
        // ADD_EVENT(EV_STAT_MAJOR_WORDS),
        // ADD_EVENT(EV_STAT_MAJOR_COLLECTIONS),
        // ADD_EVENT(EV_STAT_MINOR_COLLECTIONS),
        // ADD_EVENT(EV_STAT_MINOR_WORDS),
        // ADD_EVENT(EV_STAT_HEAP_WORDS),
        // ADD_EVENT(EV_STAT_MINOR_SIZE)
    };

    phaseToEventState_ = new std::unordered_map<ev_runtime_phase, EventState> {};
};

class PollState {
public:
    PollState(MetricDataListener& listener)
        : listener_(listener), entries_(), lostEvents_(0), lastTimestampInMs_(0) {
    }

    void emitWarning(const string& message) {
        listener_.recordNotification(data::NotificationCategory::USER_WARNING, message);
    }

    void addEntry(MetricListenerEntry& entry, uint64_t timestampInNs) {
        uint64_t timestampInMs = timestampInNs / NS_IN_MS;
        if (lastTimestampInMs_ == 0) {
            lastTimestampInMs_ = timestampInMs;
        } else if (lastTimestampInMs_ != timestampInMs) {
            push();
            lastTimestampInMs_ = timestampInMs;
        }

        addEntry(entry);
    }

    void lostEvents(int lostEvents) {
        lostEvents_ += lostEvents;
    }

    void push() {
//        printf("len=%lu\n", entries_.size());
        if (!entries_.empty()) {
            if (lostEvents_ > 0) {
                struct MetricListenerEntry entry{};

                entry.name = LOST_EVENTS_NAME;
                entry.unit = MetricUnit::NONE;
                entry.variability = MetricVariability::VARIABLE;
                entry.data.type = MetricDataType::LONG;
                entry.data.valueLong = lostEvents_;

                addEntry(entry);
                lostEvents_ = 0;
//                printf("lostEvents_=%d\n", lostEvents_);
            }

            const long realtimeTimestampInMs = MONOTONIC_TO_REALTIME_ADJUSTMENT_IN_MS + (long)lastTimestampInMs_;
            listener_.recordEntries(entries_, realtimeTimestampInMs);
            entries_.clear();
        }
    }

private:
    MetricDataListener& listener_;
    vector<MetricListenerEntry> entries_;
    int lostEvents_;
    uint64_t lastTimestampInMs_;

    void addEntry(MetricListenerEntry& entry) {
        entries_.push_back(entry);
    }
};

struct caml_runtime_events_cursor* cursor_ = nullptr;

int eventRingBegin(int domainId, void* data, uint64_t timestamp, ev_runtime_phase phase) {
//    printf("eventRingBegin %lu %d\n", timestamp, phase);
    const auto& phaseIt = REQUIRED_PHASES->find(phase);
    if (phaseIt == REQUIRED_PHASES->end()) {
        return 1;
    }

    const auto& it = phaseToEventState_->find(phase);
    if (it == phaseToEventState_->end()) {
        EventState eventState{};
        eventState.beginTimestamp = timestamp;
        eventState.eventName = phaseIt->second;

        phaseToEventState_->insert({phase, eventState});
    } else {
        it->second.beginTimestamp = timestamp;
    }
    return 1;
}

int eventRingEnd(int domainId, void* data, uint64_t timestamp, ev_runtime_phase phase) {
//    printf("eventRingEnd %lu %d\n", timestamp, phase);
    if (REQUIRED_PHASES->find(phase) == REQUIRED_PHASES->end()) {
        return 1;
    }

    PollState* pollState = (PollState*) data;
    const auto& it = phaseToEventState_->find(phase);
    if (it == phaseToEventState_->end()) {
        return 1;
    }

    EventState& state = it->second;
    const uint64_t duration = timestamp - state.beginTimestamp;

    struct MetricListenerEntry entry{};

    entry.name = state.eventName;
    entry.unit = MetricUnit::NANOSECONDS;
    entry.variability = MetricVariability::VARIABLE;
    entry.data.type = MetricDataType::LONG;
    entry.data.valueLong = duration;

    pollState->addEntry(entry, timestamp);

    phaseToEventState_->erase(it);
    return 1;
}

int eventRingCounter(int domainId, void* data, uint64_t timestamp, ev_runtime_counter counter, uint64_t value) {
    // printf("eventRingCounter %lu %d %lu %s\n", timestamp, counter, value, EV_COUNTER_NAMES[counter]);
    const auto& phaseIt = REQUIRED_COUNTERS->find(counter);
    if (phaseIt == REQUIRED_COUNTERS->end()) {
        return 1;
    }

    // printf("%s: %lu %lu\n", phaseIt->second, timestamp, value);

    MetricUnit unit = MetricUnit::EVENTS;

    if (counter == EV_C_MINOR_PROMOTED || counter == EV_C_MINOR_ALLOCATED) {
        value *= WORD_SIZE;
        unit = MetricUnit::BYTES;
    }

    struct MetricListenerEntry entry{};
    entry.name = phaseIt->second;
    entry.unit = unit;
    entry.variability = MetricVariability::VARIABLE;
    entry.data.type = MetricDataType::LONG;
    // uint64 to int64 conversion
    entry.data.valueLong = value;

    PollState* pollState = (PollState*) data;
    pollState->addEntry(entry, timestamp);
    return 1;
}

int  eventRingLostEvents(int domainId, void* data, int lost_events) {
    // printf("eventRingLostEvents %d\n", lost_events);
    PollState* pollState = (PollState*) data;
    pollState->lostEvents(lost_events);
    return 1;
}

#else
static const bool hasEventRing_ = false;
#endif

EventRingReader::EventRingReader(vector<string>& disabledPrefixes)
    : enabled_(false),
      hasEmittedConstantMetrics_(false) {

    updateEntryPrefixes(disabledPrefixes);
}

// Called on processor thread with readersMutex
void EventRingReader::updateEntryPrefixes(vector<string>& disabledPrefixes) {
    __attribute__((unused)) bool wasEnabled = enabled_;
    const bool enabled = !isPrefixDisabled(EVENT_RING_NAME, disabledPrefixes);

    #ifdef CAML_HAS_EVENTRING
    if (!wasEnabled && enabled) {
        // on start
        if (!calledStart_) {
            // first time ever
            init_counters();

            caml_acquire_runtime_system();
            caml_runtime_events_start();
            caml_release_runtime_system();

            timespec ts {0};
            clock_gettime(CLOCK_REALTIME, &ts);
            const long clockRealTimeInMs = toMillis(ts);
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const long clockMonotonicInMs = toMillis(ts);
            MONOTONIC_TO_REALTIME_ADJUSTMENT_IN_MS = clockRealTimeInMs - clockMonotonicInMs;

            calledStart_ = true;
        } else {
            caml_acquire_runtime_system();
            caml_runtime_events_resume();
            caml_release_runtime_system();
        }

        runtime_events_error error = caml_runtime_events_create_cursor(NULL, MONITOR_THIS_PROCESS, &cursor_);
        if (error != E_SUCCESS) {
            printf("%d\n", (int)error);
            int value = (int)error;
            logError("error creating cursor: %d\n", value);
            cursor_ = nullptr;
            enabled_ = false;
            return;
        } else {
            caml_runtime_events_set_runtime_begin(cursor_, eventRingBegin);
            caml_runtime_events_set_runtime_end(cursor_, eventRingEnd);
            caml_runtime_events_set_runtime_counter(cursor_, eventRingCounter);
            caml_runtime_events_set_lost_events(cursor_, eventRingLostEvents);
        }
    } else if (wasEnabled && !enabled) {
        // on stop
        disable();
    }
    #endif

    enabled_ = enabled;
}

// Called on processor thread with readersMutex
void EventRingReader::disable() {
    #ifdef CAML_HAS_EVENTRING
        if (enabled_ && calledStart_) {
            caml_acquire_runtime_system();
            caml_runtime_events_pause();
            caml_release_runtime_system();
        }

        if (cursor_ != nullptr) {
            caml_runtime_events_free_cursor(cursor_);
            cursor_ = nullptr;
        }
    #endif
}

uint32_t EventRingReader::read(MetricDataListener& listener, const long timestampInMs) {
    uint32_t events = 0;
    if (enabled_) {
        if (!hasEmittedConstantMetrics_) {
            emitConstantMetrics(listener, timestampInMs);
            events++;

            hasEmittedConstantMetrics_ = true;
        }

        #ifdef CAML_HAS_EVENTRING
        if (cursor_ != nullptr) {
            PollState pollState(listener);
            uintnat eventsRead = 0;
            runtime_events_error error = caml_runtime_events_read_poll(cursor_, &pollState, MAX_EVENTS, &eventsRead);
            if (error != E_SUCCESS) {
                logError("caml_runtime_events_read_poll error: %d\n", (int)error);
            }
            events += eventsRead;
            pollState.push();
        }
        #endif
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
    hasEventRingEntry.data.valueLong = hasEventRing_;
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
