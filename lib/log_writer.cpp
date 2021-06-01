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

#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <elfutils/libdwfl.h>

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

    /*if (isApplicationThread) {
        auto it = threadIdToInformation.find(trace.env_id);
        if (it != threadIdToInformation.end()) {
            ThreadInformation& threadInformation = it->second;
            stackSample->set_thread_name(threadInformation.name);
            stackSample->set_thread_id(threadInformation.threadId);
        } else {
            setFakeThreadId(trace, stackSample);
        }
    } else {
        setFakeThreadId(trace, stackSample);
    }*/
    stackSample->set_thread_state(threadState);

    stackSample->clear_compressedframes();
    const int32_t errorCode = 0; // TODO
    stackSample->set_error_code(errorCode);
    stackSample->set_wallclockscanid(wallclockScanId);

    // Lookup symbols for frames
    CallFrame* frames = trace.frames;
    if (trace.num_frames > 0) {
        for (int frame_idx = 0; frame_idx < trace.num_frames; frame_idx++) {
            Dwarf_Addr addr = (uintptr_t) frames[frame_idx];

            Dwfl_Module* module = dwfl_addrmodule(dwfl, addr);
            const char* function_name = dwfl_module_addrname(module, addr);

            Dwfl_Line* line = dwfl_getsrc(dwfl, addr);

            if (line != NULL) {
                int nline;
                Dwarf_Addr addr;
                const char* filename = dwfl_lineinfo(line, &addr, &nline, NULL, NULL, NULL);
                printf("%s (%s:%d)\n", function_name, filename, nline);
            } else {
                if (function_name != NULL) {
                    printf("%s\n", function_name);
                }
            }
        }
    }

    /*if (!isError) {
        for (int i = 0; i < numFrames; i++) {
            CallFrame frame = trace.frames[i];
            jmethodID methodId = frame.method_id;
            // lineno is in fact byte code index, needs converting to lineno
            jint bci = frame.lineno;

            debugLogger_ << "start lookupMethod" << endl;
            nameLookup_.lookupMethod(methodId, *this);
            debugLogger_ << "end lookupMethod" << endl;

            data::CompressedFrameEntry *frameEntry = stackSample->add_compressedframes();

            debugLogger_ << "start recordFrame" << endl;
            recordFrame(bci, methodId, frameEntry);
            debugLogger_ << "end recordFrame" << endl;
        }
    }

    stackSample->set_has_max_frames(numFrames >= maxFramesToCapture_);*/

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

/*void LogWriter::recordNewMethod(
    jmethodID methodId,
    jclass classId,
    const char *methodName) {

    if (methodToLineNumbers.count(methodId) == 0) {
        jvmtiLineNumberEntry* jvmti_table = nullptr;
        jint entry_count;

        debugLogger_ << "start jvmti_->GetLineNumberTable() " << methodId << endl;

        int err = jvmti_->GetLineNumberTable((jmethodID) methodId, &entry_count, &jvmti_table);
        if (err != JVMTI_ERROR_NONE)
        {
            jvmti_table = nullptr;
        }
        else
        {
            debugLogger_ << "JVMTI error when looking up the line number table for "
                         << classId << "." << methodName << endl;
        }

        LineNumbers lineNumbers = {
                jvmti_table,
                entry_count
        };

        methodToLineNumbers.insert( { methodId, lineNumbers } );
    }

    data::MethodInformation* methodInfo = nameAgentEnvelope_.mutable_method_information();
    methodInfo->set_methodid(toMethodId(methodId));
    methodInfo->set_methodname(methodName);
    methodInfo->set_moduleid(toModuleId(classId));

    recordWithSize(nameAgentEnvelope_);
}

void LogWriter::recordNewClass(
    jclass classId,
    const char *fileName,
    const char *className) {

    data::ModuleInformation *classInfo = nameAgentEnvelope_.mutable_module_information();
    classInfo->set_moduleid(toModuleId(classId));
    classInfo->set_filename(fileName);
    classInfo->set_modulename(className);

    recordWithSize(nameAgentEnvelope_);
}

void LogWriter::recordFrame(
    const jint bci,
    jmethodID methodId,
    data::CompressedFrameEntry* frameEntry) {

    frameEntry->set_methodid(toMethodId(methodId));
    auto it = methodToLineNumbers.find(methodId);
    if (it != methodToLineNumbers.end()) {
        LineNumbers methodInformation = it->second;
        bool noTable = methodInformation.lineNumberTable == nullptr;
    
        const jint lineNumber = bci <= 0 || noTable ? bci : bci2line(
            bci, methodInformation.lineNumberTable, methodInformation.entryCount);
        frameEntry->set_line(static_cast<uint32_t>(lineNumber));
    } else {
        frameEntry->set_line(0);
        debugLogger_ << "Missing method information: " << methodId << endl;
    }
}*/

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
