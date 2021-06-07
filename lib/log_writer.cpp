#include "log_writer.h"
#include "collector_controller.h"
#include <cstdlib>
#include "unistd.h"
#include <google/protobuf/util/delimited_message_util.h>
#include "network.h"
#include <cstdint>

using google::protobuf::util::SerializeDelimitedToZeroCopyStream;

using std::copy;
using data::SampleTimeType;

char *debuginfo_path=NULL;

Dwfl_Callbacks callbacks = {
    .find_elf=dwfl_linux_proc_find_elf,
    .find_debuginfo=dwfl_standard_find_debuginfo,
    .debuginfo_path=&debuginfo_path,
};

Dwfl* dwfl;

void LogWriter::initDwarf() {
    dwfl = dwfl_begin(&callbacks);
    int ret = dwfl_linux_proc_report(dwfl, getpid());
    if (ret != 0) {
        // TODO: error code here, report error over the wire
    }
    ret = dwfl_report_end(dwfl, NULL, NULL);
    if (ret != 0) {
        // TODO: error code here report error over the wire
    }
}

void LogWriter::recordWithSize(data::AgentEnvelope& envelope) {
    if (output_ != nullptr) {
        if (!SerializeDelimitedToZeroCopyStream(envelope, output_))
        {
            logError("Failed to serialize sample message");
        }
    }

    debugLogger_ << "start send" << endl;
    network_.sendWithSize(controller_, envelope);
    debugLogger_ << "end send" << endl;
}

void LogWriter::recordStackTrace(
        const timespec& ts,
        const CallTrace& trace,
        int signum,
        int threadState,
        int wallclockScanId,
        uint64_t time_tsc) {
    debugLogger_ << "start record" << endl;

    data::StackSample* stackSample = frameAgentEnvelope_.mutable_stack_sample();
    setSampleType(signum, stackSample);
    setSampleTime(ts, stackSample);

    // TODO: recordThread(0, "FakeThreadName");
    stackSample->set_thread_name("FakeThreadName");
    stackSample->set_thread_id(0);

    stackSample->set_thread_state(threadState);

    int numFrames = trace.num_frames;
    const bool isError = numFrames < 0;
    stackSample->clear_compressedframes();
    stackSample->set_error_code(isError ? numFrames : 0);
    stackSample->set_wallclockscanid(wallclockScanId);

    // Lookup symbols for frames
    CallFrame* frames = trace.frames;
    if (!isError) {
        for (int frame_idx = 0; frame_idx < numFrames; frame_idx++) {
            if (true) { // !frames[frame_idx].isForeign
                Dwarf_Addr addr = (Dwarf_Addr) frames[frame_idx].frame;

                data::CompressedFrameEntry *frameEntry = stackSample->add_compressedframes();
                auto it = knownAddrToInformation_.find(addr);
                if (it != knownAddrToInformation_.end()) {
                    // If we've seen this address before, just add the compressed stack frame
                    AddrInformation& info = it->second;
                    frameEntry->set_methodid(info.methodId);
                    frameEntry->set_line(info.lineNumber);
                } else {
                    // Looked the symbol information from dwarf
                    Dwfl_Module* module = dwfl_addrmodule(dwfl, addr);
                    const char* function_name = dwfl_module_addrname(module, addr);

                    if (function_name == NULL) {
                        debugLogger_ << "Missing method information: " << addr << endl;
                    } else {
                        const uintptr_t methodId = reinterpret_cast<uintptr_t>(function_name);

                        int lineNumber = 0;
                        const char* filename = "Unknown";
                        Dwfl_Line* line = dwfl_getsrc(dwfl, addr);
                        if (line != NULL) {
                            Dwarf_Addr addr;
                            filename = dwfl_lineinfo(line, &addr, &lineNumber, NULL, NULL, NULL);
                        }

                        const uintptr_t fileId = reinterpret_cast<uintptr_t>(filename);

                        // Save the address information into the cache
                        AddrInformation addrInformation = {
                            methodId,
                            lineNumber
                        };
                        knownAddrToInformation_.insert( { addr, addrInformation } );
                        frameEntry->set_methodid(methodId);
                        frameEntry->set_line(lineNumber);

                        // Technically we're emitting a "module" aka class with an empty name on our protocol
                        // Need to decide if the protocol needs modifying
                        if (knownFiles_.count(fileId) == 0) {
                            knownFiles_.insert(fileId);

                            data::ModuleInformation *moduleInfo = nameAgentEnvelope_.mutable_module_information();
                            moduleInfo->set_moduleid(fileId);
                            moduleInfo->set_filename(filename);
                            moduleInfo->set_modulename("");

                            recordWithSize(nameAgentEnvelope_);
                        }

                        if (knownMethods_.count(methodId) == 0) {
                            knownMethods_.insert(methodId);

                            data::MethodInformation* methodInfo = nameAgentEnvelope_.mutable_method_information();
                            methodInfo->set_methodid(methodId);
                            methodInfo->set_methodname(function_name);
                            methodInfo->set_moduleid(fileId);
                            recordWithSize(nameAgentEnvelope_);
                        }
                    }
                }
            }
        }
    }

    stackSample->set_has_max_frames(numFrames >= MAX_FRAMES);

    recordWithSize(frameAgentEnvelope_);

    debugLogger_ << "end record" << endl;
}

void LogWriter::setSampleType(int signum, data::StackSample *stackSample) const {
    if (signum == SIGPROF) {
        stackSample->set_type(data::PROCESS_TIME);
        stackSample->set_sample_rate_millis(controller_.processTimeStackSampleIntervalMillis());
    } else if (signum == SIGALRM) {
        stackSample->set_type(data::ELAPSED_TIME);
        stackSample->set_sample_rate_millis(controller_.elapsedTimeStackSampleIntervalMillis());
    } else {
        stackSample->set_type(data::ALLOCATION);
        stackSample->set_sample_rate_millis(0);
    }
}

void LogWriter::setSampleTime(const timespec &ts, data::StackSample *stackSample) const {
    const long time_epoch_millis = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    stackSample->set_time_epoch_millis(static_cast<google::protobuf::uint64>(time_epoch_millis));
}

// ----------------------------
//      Inspection code
// ----------------------------

void LogWriter::recordThread(
        int threadId,
        const string& name) {

    ThreadInformation threadInformation = {
        threadId,
        name
    };

    threadIdToInformation.insert({threadId, threadInformation});
}

void LogWriter::recordAllocation(uintptr_t allocationSize, bool outsideTlab, VMSymbol *symbol) {
    debugLogger_ << "LogWriter::recordAllocation" << endl;

    AllocationKey key = make_pair(symbol, outsideTlab);

    /*if ( !nameLookup_.isSymbolKnown(symbol) ) {
        data::SymbolInformation* symbolInformation = frameAgentEnvelope_.mutable_symbol_information();
        symbolInformation->set_symbol(reinterpret_cast<google::protobuf::uint64>(symbol));
        symbolInformation->set_name(symbol->body(), symbol->length());
        recordWithSize(frameAgentEnvelope_);
    }*/

    AllocationRow &info = allocationsTable[key];
    info.totalBytesAllocated += allocationSize;
    info.numberofAllocations++;
}

void LogWriter::recordAllocationTable() {
    debugLogger_ << "LogWriter::recordAllocationTable" << endl;

    // Stack allocated agent envelope used because this operation is triggered by a timer
    // and thus can overlap with other log writing operations
    data::AgentEnvelope agentEnvelope_;
    data::AllocationTable* allocationTable = agentEnvelope_.mutable_allocation_table();
    allocationTable->set_time_epoch_millis(0);

    AllocationsTable::iterator it = allocationsTable.begin();
    while (it != allocationsTable.end()) {
        const AllocationKey& key = it->first;
        AllocationRow& info = it->second;

        data::AllocationRow* row = allocationTable->add_rows();
        row->set_symbol(reinterpret_cast<google::protobuf::uint64>(key.first));
        row->set_number_of_allocations(info.numberofAllocations);
        row->set_total_bytes_allocated(info.totalBytesAllocated);
        row->set_outside_tlab(key.second);

        it++;
    }

    recordWithSize(agentEnvelope_);

    allocationsTable.clear();
}

void LogWriter::recordNotification(data::NotificationCategory category, const string &initialPayload, const int value) {
    string payload;

    if (value != 0) {
        std::ostringstream oss;
        oss << initialPayload << value;
        payload = oss.str();
    } else {
        payload = initialPayload;
    }

    const string& categoryName = data::NotificationCategory_Name(category);
    std::ostringstream oss;
    oss << categoryName << ": " << payload << value;
    string logMessage = oss.str();
    logError(logMessage.c_str());

    if (controller_.isConnected()) {
        data::Notification* notification = frameAgentEnvelope_.mutable_notification();
        notification->set_payload(payload);
        notification->set_category(category);

        recordWithSize(frameAgentEnvelope_);
    } else {
        // We may generate error messages before we have connected, so we can't send these error messages immediately.
        // We tell the collector controller so that it can push them out once we've connected.

        controller_.stashNotification(category, payload);
    }
}

data::MetricDataType mapDataType(MetricDataType in) {
    switch (in) {
        case MetricDataType::LONG:
            return data::MetricDataType::LONG;
        case MetricDataType::STRING:
            return data::MetricDataType::STRING;
        default:
            return data::MetricDataType::UNKNOWN_TYPE;
    }
}

data::MetricVariability mapVariability(MetricVariability in) {
    switch (in) {
        case MetricVariability::CONSTANT:
            return data::MetricVariability::CONSTANT;
        case MetricVariability::MONOTONIC:
            return data::MetricVariability::COUNTER;
        case MetricVariability::VARIABLE:
            return data::MetricVariability::VARIABLE;
        default:
            return data::MetricVariability::UNKNOWN_VARIABILITY;
    }
}

data::MetricUnit mapUnit(MetricUnit in) {
    switch (in) {
        case MetricUnit::NONE:
            return data::MetricUnit::NONE;
        case MetricUnit::BYTES:
            return data::MetricUnit::BYTES;
        case MetricUnit::TICKS:
            return data::MetricUnit::TICKS;
        case MetricUnit::EVENTS:
            return data::MetricUnit::EVENTS;
        case MetricUnit::STRING:
            return data::MetricUnit::UNIT_STRING;
        case MetricUnit::HERTZ:
            return data::MetricUnit::HERTZ;
        case MetricUnit::MILLISECONDS:
            return data::MetricUnit::MILLISECONDS;
        default:
            return data::MetricUnit::UNKNOWN_UNIT;
    }
}

// override
void LogWriter::recordMetricInformation(const MetricInformation& info) {
    auto infoEnvelope = frameAgentEnvelope_.mutable_metric_information();

    data::MetricDataType dataType = mapDataType(info.dataType);
    data::MetricVariability var = mapVariability(info.variability);
    data::MetricUnit unit = mapUnit(info.unit);

    infoEnvelope->set_metricid(info.id);
    infoEnvelope->set_name(info.name);
    infoEnvelope->set_datatype(dataType);
    infoEnvelope->set_variability(var);
    infoEnvelope->set_unit(unit);

    recordWithSize(frameAgentEnvelope_);
}

// override
void LogWriter::recordMetricSamples(const timespec& ts, const vector<MetricSample>& metricSamples) {
    auto samples_envelope = frameAgentEnvelope_.mutable_metric_samples();

    const long time_epoch_millis = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
    samples_envelope->set_time_epoch_millis(static_cast<google::protobuf::uint64>(time_epoch_millis));

    for (auto it = metricSamples.begin(); it != metricSamples.end(); ++it) {
        auto& sample = *it;
        auto sample_envelope = samples_envelope->add_samples();

        debugLogger_ << "Recorded metric sample for: " << sample.id << endl;

        sample_envelope->set_metricid(sample.id);
        if (sample.data.type == MetricDataType::STRING) {
            sample_envelope->set_stringvalue(sample.data.valueString);
        } else if (sample.data.type == MetricDataType::LONG) {
            sample_envelope->set_longvalue(sample.data.valueLong);
        }
    }

    recordWithSize(frameAgentEnvelope_);
}

void LogWriter::recordConstantMetricsComplete() {
    frameAgentEnvelope_.mutable_constant_metrics_complete();
    recordWithSize(frameAgentEnvelope_);
}
