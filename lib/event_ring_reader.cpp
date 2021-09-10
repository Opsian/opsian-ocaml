#include "event_ring_reader.h"

extern "C" {
#define CAML_NAME_SPACE

#include "caml/misc.h" // CamlExtern
#include "caml/eventring.h"
#include "caml/mlvalues.h" // Caml_state
}

static const string EVENT_RING_NAME = string("ocaml.eventring");
static const string ENABLED_NAME = string("ocaml.eventring.enabled");

// TODO: timestamps are nanoseconds

#define CAML_HAS_EVENTRING

#ifdef CAML_HAS_EVENTRING
static const bool hasEventRing_ = true;
extern "C" {
    #include "caml/eventring.h"
}

#include <unordered_map>

static const char* const GC_PHASE_NAMES[] = {
    "EV_COMPACT_MAIN",
    "EV_COMPACT_RECOMPACT",
    "EV_EXPLICIT_GC_SET",
    "EV_EXPLICIT_GC_STAT",
    "EV_EXPLICIT_GC_MINOR",
    "EV_EXPLICIT_GC_MAJOR",
    "EV_EXPLICIT_GC_FULL_MAJOR",
    "EV_EXPLICIT_GC_COMPACT",
    "EV_MAJOR",
    "EV_MAJOR_ROOTS",
    "EV_MAJOR_SWEEP",
    "EV_MAJOR_MARK_ROOTS",
    "EV_MAJOR_MARK_MAIN",
    "EV_MAJOR_MARK_FINAL",
    "EV_MAJOR_MARK",
    "EV_MAJOR_MARK_GLOBAL_ROOTS_SLICE",
    "EV_MAJOR_ROOTS_GLOBAL",
    "EV_MAJOR_ROOTS_DYNAMIC_GLOBAL",
    "EV_MAJOR_ROOTS_LOCAL",
    "EV_MAJOR_ROOTS_C",
    "EV_MAJOR_ROOTS_FINALISED",
    "EV_MAJOR_ROOTS_MEMPROF",
    "EV_MAJOR_ROOTS_HOOK",
    "EV_MAJOR_CHECK_AND_COMPACT",
    "EV_MINOR",
    "EV_MINOR_LOCAL_ROOTS",
    "EV_MINOR_REF_TABLES",
    "EV_MINOR_COPY",
    "EV_MINOR_UPDATE_WEAK",
    "EV_MINOR_FINALIZED",
    "EV_EXPLICIT_GC_MAJOR_SLICE"
};
static const size_t GC_PHASE_NAMES_SIZE = sizeof(GC_PHASE_NAMES) / sizeof(char*);

struct EventState {
    uint64_t beginTimestamp;
    string eventName;
};

static caml_eventring_cursor* cursor_ = nullptr;

static caml_eventring_callbacks callbacks_ = {0};

static MetricDataListener* listener_ = nullptr;
static vector<MetricListenerEntry> entries_{};

static std::unordered_map<ev_gc_phase, EventState> phaseToEventState_{};

void emitWarning(const string& message) {
    if (listener_ != nullptr) {
        listener_->recordNotification(data::NotificationCategory::USER_WARNING, message);
    }
}

void eventRingBegin(uint64_t timestamp, ev_gc_phase phase) {
    printf("eventRingBegin %lu %d\n", timestamp, phase);
    const auto& it = phaseToEventState_.find(phase);
    if (it == phaseToEventState_.end()) {
        if (phase <= GC_PHASE_NAMES_SIZE) {
            EventState eventState{};
            eventState.beginTimestamp = timestamp;
            eventState.eventName = string("ocaml.eventring.") + GC_PHASE_NAMES[phase];
            phaseToEventState_.insert({phase, eventState});
        } else {
            const string msg = "Unknown GC phase: " + phase;
            emitWarning(msg);
        }
    } else {
        it->second.beginTimestamp = timestamp;
    }
}

void eventRingEnd(uint64_t timestamp, ev_gc_phase phase) {
    printf("eventRingEnd %lu %d\n", timestamp, phase);
    const auto& it = phaseToEventState_.find(phase);
    if (it == phaseToEventState_.end()) {
        const string msg = "Received end without begin for phase: " + phase;
        emitWarning(msg);
        return;
    }

    EventState& state = it->second;
    const uint64_t duration = timestamp - state.beginTimestamp;

    struct MetricListenerEntry entry{};

    entry.name = state.eventName;
    entry.unit = MetricUnit::MILLISECONDS;
    entry.variability = MetricVariability::VARIABLE;
    entry.data.type = MetricDataType::LONG;
    entry.data.valueLong = duration;

    entries_.push_back(entry);
}
#else
static const bool hasEventRing_ = false;
#endif

EventRingReader::EventRingReader(vector<string>& disabledPrefixes)
    : enabled_(false),
      calledStart_(false),
      hasEmittedConstantMetrics_(false) {

    updateEntryPrefixes(disabledPrefixes);

    printf("enabled_=%d\n", enabled_);
}

void EventRingReader::updateEntryPrefixes(vector<string>& disabledPrefixes) {
    __attribute__((unused)) bool wasEnabled = enabled_;
    enabled_ = !isPrefixDisabled(EVENT_RING_NAME, disabledPrefixes);

    #ifdef CAML_HAS_EVENTRING
    if (!wasEnabled && enabled_) {
        // on start
        if (!calledStart_) {
            // first time ever
            caml_eventring_start();

            callbacks_.ev_begin = eventRingBegin;
            callbacks_.ev_end = eventRingEnd;

            printf("start\n");
            calledStart_ = true;
        } else {
            caml_eventring_resume();
            printf("caml_eventring_resume()\n");
        }

//        printf("Caml_state->eventlog_startup_pid %ld\n", Caml_state->eventlog_startup_pid);

        cursor_ = caml_eventring_create_cursor(NULL, Caml_state->eventlog_startup_pid);
        if (!cursor_) {
            printf("invalid or non-existent cursor\n"); // TODO: better error logging
        }

        printf("caml_eventring_create_cursor\n");

    } else if (wasEnabled && !enabled_) {
        // on stop
        printf("on stop\n");
//        caml_eventring_pause();

//        caml_eventring_free_cursor(cursor_);
    }
    #endif
}

void EventRingReader::read(MetricDataListener& listener) {
    if (!hasEmittedConstantMetrics_) {
        MetricListenerEntry entry{};
        entry.name = ENABLED_NAME;
        entry.unit = MetricUnit::NONE;
        entry.variability = MetricVariability::CONSTANT;
        entry.data.type = MetricDataType::LONG;
        entry.data.valueLong = hasEventRing_;

        vector<MetricListenerEntry> constantMetrics{};
        constantMetrics.push_back(entry);
        listener.recordEntries(constantMetrics);

        hasEmittedConstantMetrics_ = true;
    }

    #ifdef CAML_HAS_EVENTRING
    if (enabled_) {
        printf("read\n");
        listener_ = &listener;
        printf("read 2\n");
        entries_.clear();
        printf("read 3\n");
//        caml_eventring_read_poll(cursor_, &callbacks_);
        printf("caml_eventring_read_poll\n");
        if (!entries_.empty()) {
            listener.recordEntries(entries_);
        }
        listener_ = nullptr;
    }
    #endif
}

const bool EventRingReader::hasEmittedConstantMetrics() {
    return hasEmittedConstantMetrics_;
}
