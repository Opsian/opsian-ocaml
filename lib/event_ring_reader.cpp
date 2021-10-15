#include "event_ring_reader.h"
#include <cstdlib>
#include "globals.h"

extern "C" {
#define CAML_NAME_SPACE

#include <caml/misc.h> // CamlExtern
#include <caml/threads.h> // runtime lock
#include <caml/eventring.h>
#include <caml/mlvalues.h> // Caml_state
}

static const string EVENT_RING_NAME = string("ocaml.eventring");
static const string ENABLED_NAME = string("ocaml.eventring.enabled");
static const string LOST_EVENTS_NAME = string("ocaml.eventring.lost_events");
static const string RUN_PARAM_NAME = string("ocaml.runparam");
static const string VERSION_PARAM_NAME = string("ocaml.version");
static const uint MAX_EVENTS = 60000;
static std::atomic_bool calledStart_(false);

#define CAML_HAS_EVENTRING

#ifdef CAML_HAS_EVENTRING

static const int MONITOR_THIS_PROCESS = -1;

static const bool hasEventRing_ = true;
extern "C" {
    #include "caml/eventring.h"
    #include "caml/version.h"
}

#include <unordered_map>
#include <unordered_set>

static const std::unordered_set<ev_runtime_phase> REQUIRED_PHASES {
    EV_MINOR, // Pause time for minor collection
    EV_MAJOR // Sum of pause time slices for major collections
};

static const std::unordered_set<ev_runtime_counter> REQUIRED_COUNTERS {
    // Causes of minor collections starting:
    EV_C_FORCE_MINOR_ALLOC_SMALL,
    EV_C_FORCE_MINOR_MAKE_VECT,
    EV_C_FORCE_MINOR_SET_MINOR_HEAP_SIZE,
    EV_C_FORCE_MINOR_WEAK,
    EV_C_FORCE_MINOR_MEMPROF,
    // amount promoted from the minor heap to the major heap on each minor collection in terms of machine words
    EV_C_MINOR_PROMOTED

    #if OCAML_VERSION >= 50000
        // Multicore only counters:
        ,
        // amount of allocation in the previous minor cycle in terms of machine words
        EV_C_MINOR_ALLOCATED
    #endif
};

static const char* const EV_PHASE_NAMES[] = {
    "ocaml.eventring.EV_COMPACT_MAIN",
    "ocaml.eventring.EV_COMPACT_RECOMPACT",
    "ocaml.eventring.EV_EXPLICIT_GC_SET",
    "ocaml.eventring.EV_EXPLICIT_GC_STAT",
    "ocaml.eventring.EV_EXPLICIT_GC_MINOR",
    "ocaml.eventring.EV_EXPLICIT_GC_MAJOR",
    "ocaml.eventring.EV_EXPLICIT_GC_FULL_MAJOR",
    "ocaml.eventring.EV_EXPLICIT_GC_COMPACT",
    "ocaml.eventring.EV_MAJOR",
    "ocaml.eventring.EV_MAJOR_ROOTS",
    "ocaml.eventring.EV_MAJOR_SWEEP",
    "ocaml.eventring.EV_MAJOR_MARK_ROOTS",
    "ocaml.eventring.EV_MAJOR_MARK_MAIN",
    "ocaml.eventring.EV_MAJOR_MARK_FINAL",
    "ocaml.eventring.EV_MAJOR_MARK",
    "ocaml.eventring.EV_MAJOR_MARK_GLOBAL_ROOTS_SLICE",
    "ocaml.eventring.EV_MAJOR_ROOTS_GLOBAL",
    "ocaml.eventring.EV_MAJOR_ROOTS_DYNAMIC_GLOBAL",
    "ocaml.eventring.EV_MAJOR_ROOTS_LOCAL",
    "ocaml.eventring.EV_MAJOR_ROOTS_C",
    "ocaml.eventring.EV_MAJOR_ROOTS_FINALISED",
    "ocaml.eventring.EV_MAJOR_ROOTS_MEMPROF",
    "ocaml.eventring.EV_MAJOR_ROOTS_HOOK",
    "ocaml.eventring.EV_MAJOR_CHECK_AND_COMPACT",
    "ocaml.eventring.EV_MINOR",
    "ocaml.eventring.EV_MINOR_LOCAL_ROOTS",
    "ocaml.eventring.EV_MINOR_REF_TABLES",
    "ocaml.eventring.EV_MINOR_COPY",
    "ocaml.eventring.EV_MINOR_UPDATE_WEAK",
    "ocaml.eventring.EV_MINOR_FINALIZED",
    "ocaml.eventring.EV_EXPLICIT_GC_MAJOR_SLICE"
};
static const size_t EV_PHASE_NAMES_SIZE = sizeof(EV_PHASE_NAMES) / sizeof(char*);

static const char* const EV_COUNTER_NAMES[] = {
    "ocaml.eventring.EV_C_ALLOC_JUMP",
    "ocaml.eventring.EV_C_FORCE_MINOR_ALLOC_SMALL",
    "ocaml.eventring.EV_C_FORCE_MINOR_MAKE_VECT",
    "ocaml.eventring.EV_C_FORCE_MINOR_SET_MINOR_HEAP_SIZE",
    "ocaml.eventring.EV_C_FORCE_MINOR_WEAK",
    "ocaml.eventring.EV_C_FORCE_MINOR_MEMPROF",
    "ocaml.eventring.EV_C_MAJOR_MARK_SLICE_REMAIN",
    "ocaml.eventring.EV_C_MAJOR_MARK_SLICE_FIELDS",
    "ocaml.eventring.EV_C_MAJOR_WORK_EXTRA",
    "ocaml.eventring.EV_C_MAJOR_WORK_MARK",
    "ocaml.eventring.EV_C_MAJOR_MARK_SLICE_POINTERS",
    "ocaml.eventring.EV_C_MAJOR_WORK_SWEEP",
    "ocaml.eventring.EV_C_MINOR_PROMOTED",
    "ocaml.eventring.EV_C_REQUEST_MAJOR_ALLOC_SHR",
    "ocaml.eventring.EV_C_REQUEST_MAJOR_ADJUST_GC_SPEED",
    "ocaml.eventring.EV_C_REQUEST_MINOR_REALLOC_REF_TABLE",
    "ocaml.eventring.EV_C_REQUEST_MINOR_REALLOC_EPHE_REF_TABLE",
    "ocaml.eventring.EV_C_REQUEST_MINOR_REALLOC_CUSTOM_TABLE"
};
static const size_t EV_COUNTER_NAMES_SIZE = sizeof(EV_COUNTER_NAMES) / sizeof(char*);

struct EventState {
    uint64_t beginTimestamp;
    string eventName;
};

class PollState {
public:
    PollState(MetricDataListener& listener)
        : listener_(listener), entries_(), lostEvents_(0) {
    }

    void emitWarning(const string& message) {
        listener_.recordNotification(data::NotificationCategory::USER_WARNING, message);
    }

    void addEntry(MetricListenerEntry& entry) {
        entries_.push_back(entry);
    }

    void lostEvents(int lostEvents) {
        lostEvents_ += lostEvents;
    }

    void push() {
        if (lostEvents_ > 0) {
            struct MetricListenerEntry entry{};

            entry.name = LOST_EVENTS_NAME;
            entry.unit = MetricUnit::NONE;
            entry.variability = MetricVariability::VARIABLE;
            entry.data.type = MetricDataType::LONG;
            entry.data.valueLong = lostEvents_;

            addEntry(entry);
//            printf("lostEvents_=%d\n", lostEvents_);
        }

//        printf("len=%lu\n", entries_.size());
        if (!entries_.empty()) {
            listener_.recordEntries(entries_);
        }
    }

private:
    MetricDataListener& listener_;
    vector<MetricListenerEntry> entries_;
    int lostEvents_;
};

static caml_eventring_cursor* cursor_ = nullptr;

static caml_eventring_callbacks callbacks_ = {0};

static std::unordered_map<ev_runtime_phase, EventState> phaseToEventState_{};

void eventRingBegin(int domainId, void* data, uint64_t timestamp, ev_runtime_phase phase) {
    // printf("eventRingBegin %lu %d\n", timestamp, phase);
    if (REQUIRED_PHASES.find(phase) == REQUIRED_PHASES.end()) {
        return;
    }

    PollState* pollState = (PollState*) data;
    const auto& it = phaseToEventState_.find(phase);
    if (it == phaseToEventState_.end()) {
        if (phase <= EV_PHASE_NAMES_SIZE) {
            EventState eventState{};
            eventState.beginTimestamp = timestamp;
            eventState.eventName = EV_PHASE_NAMES[phase];
            phaseToEventState_.insert({phase, eventState});
        } else {
            const string msg = "Unknown GC phase: " + std::to_string(phase);
            pollState->emitWarning(msg);
        }
    } else {
        it->second.beginTimestamp = timestamp;
    }
}

void eventRingEnd(int domainId, void* data, uint64_t timestamp, ev_runtime_phase phase) {
    // printf("eventRingEnd %lu %d\n", timestamp, phase);
    if (REQUIRED_PHASES.find(phase) == REQUIRED_PHASES.end()) {
        return;
    }

    PollState* pollState = (PollState*) data;
    const auto& it = phaseToEventState_.find(phase);
    if (it == phaseToEventState_.end()) {
        // TODO: re-enable this warning once we've improved polling frequency
        // const string msg = string("Received end without begin for phase: ") + EV_PHASE_NAMES[phase];
        // pollState->emitWarning(msg);
        return;
    }

    EventState& state = it->second;
    const uint64_t duration = timestamp - state.beginTimestamp;

    struct MetricListenerEntry entry{};

    entry.name = state.eventName;
    entry.unit = MetricUnit::NANOSECONDS;
    entry.variability = MetricVariability::VARIABLE;
    entry.data.type = MetricDataType::LONG;
    entry.data.valueLong = duration;

    // printf("%s=%lu\n", EV_PHASE_NAMES[phase], duration);

    pollState->addEntry(entry);

    phaseToEventState_.erase(it);
}

void eventRingCounter(int domainId, void* data, uint64_t timestamp, ev_runtime_counter counter, uint64_t value) {
    // printf("eventRingCounter %lu %d %lu %s\n", timestamp, counter, value, EV_COUNTER_NAMES[counter]);
    if (REQUIRED_COUNTERS.find(counter) == REQUIRED_COUNTERS.end()) {
        return;
    }

    // printf("%s: %lu %lu\n", EV_COUNTER_NAMES[counter], timestamp, value);

    PollState* pollState = (PollState*) data;
    if (counter <= EV_COUNTER_NAMES_SIZE) {
        struct MetricListenerEntry entry{};

        entry.name = EV_COUNTER_NAMES[counter];
        entry.unit = MetricUnit::NONE;
        entry.variability = MetricVariability::VARIABLE;
        entry.data.type = MetricDataType::LONG;
        // uint64 to int64 conversion
        entry.data.valueLong = value;

        pollState->addEntry(entry);
    } else {
        const string msg = "Unknown GC counter: " + std::to_string(counter);
        pollState->emitWarning(msg);
    }
}

void eventRingLostEvents(int domainId, void* data, int lost_events) {
    // printf("eventRingLostEvents %d\n", lost_events);
    PollState* pollState = (PollState*) data;
    pollState->lostEvents(lost_events);
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
            caml_acquire_runtime_system();
            caml_eventring_start();
            caml_release_runtime_system();

            callbacks_.ev_runtime_begin = eventRingBegin;
            callbacks_.ev_runtime_end = eventRingEnd;
            callbacks_.ev_runtime_counter = eventRingCounter;
            callbacks_.ev_lost_events = eventRingLostEvents;

            calledStart_ = true;
        } else {
            caml_acquire_runtime_system();
            caml_eventring_resume();
            caml_release_runtime_system();
        }

        cursor_ = caml_eventring_create_cursor(NULL, MONITOR_THIS_PROCESS);
        if (cursor_ == nullptr) {
            logError("invalid or non-existent cursor\n");
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
    if (enabled_ && calledStart_) {
        caml_acquire_runtime_system();
        caml_eventring_pause();
        caml_release_runtime_system();
    }

    if (cursor_ != nullptr) {
        caml_eventring_free_cursor(cursor_);
        cursor_ = nullptr;
    }
}

uint32_t EventRingReader::read(MetricDataListener& listener) {
    uint32_t events = 0;
    if (enabled_) {
        if (!hasEmittedConstantMetrics_) {
            emitConstantMetrics(listener);
            events++;

            hasEmittedConstantMetrics_ = true;
        }

        #ifdef CAML_HAS_EVENTRING
        if (cursor_ != nullptr) {
            PollState pollState(listener);
            uint eventsRead = caml_eventring_read_poll(cursor_, &callbacks_, &pollState, MAX_EVENTS);
//            printf("eventsRead=%d\n", eventsRead);
            events += eventsRead;
            pollState.push();
        }
        #endif
    }

    return events;
}

void EventRingReader::emitConstantMetrics(MetricDataListener& listener) const {
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

    listener.recordEntries(constantMetrics);
}

const bool EventRingReader::hasEmittedConstantMetrics() {
    return hasEmittedConstantMetrics_;
}
