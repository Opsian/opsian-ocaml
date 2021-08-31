#include "log_writer.h"
#include "collector_controller.h"
#include <cstdlib>
#include "unistd.h"
#include <google/protobuf/util/delimited_message_util.h>
#include <google/protobuf/stubs/common.h>
#include "network.h"
#include <cstdint>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <dlfcn.h>
#include <link.h>
#include "deps/libbacktrace/backtrace.h"

using google::protobuf::util::SerializeDelimitedToZeroCopyStream;
using google::protobuf::internal::IsStructurallyValidUTF8;

using std::copy;
using data::SampleTimeType;

#define THREAD_NAME_BUFFER_SIZE 16

// NB: if at any point we add or remove methods that are invoked in the signal handler this index
// needs to change.
// For some reason we can't discover the symbol of the restore trap of the libc signal handling mechanism.
// I think https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67365#c3
// might explain what's going on, but shifting the pc by 1 didn't seem to help. This is why we're using a frame count
#define NUMBER_OF_SIGNAL_HANDLER_FRAMES 3

struct backtrace_state* btState = NULL;

// bootstrap functions for callbacks
void _handleBtError(void* data, const char* errorMessage, int errorNumber) {
    LogWriter* self = (LogWriter*) data;
    self->handleBtError(errorMessage, errorNumber);
}

int _handlePcInfo (
    void *data,
    uintptr_t pc,
    const char *filename,
    int lineno,
    const char *function) {
    LogWriter* self = (LogWriter*) data;
    return self->handlePcInfo(pc, filename, lineno, function);
}

void _handleSyminfo (
    void *data,
    uintptr_t pc,
    const char *symname,
    uintptr_t symval,
    uintptr_t symsize) {
    LogWriter* self = (LogWriter*) data;
    return self->handleSyminfo(pc, symname, symval, symsize);
}

void LogWriter::handleBtError(const char* errorMessage, int errorNumber) {
    const char* format_string = "lib_bt error: errorNumber=%d, %s";
    int size = snprintf(NULL, 0, format_string, errorNumber, errorMessage);
    char buf[size + 1];
    snprintf(buf, sizeof buf, format_string, errorMessage);

    buffer_.pushNotification(data::NotificationCategory::USER_ERROR, buf);
}

void LogWriter::initLibBackTrace() {
    btState = backtrace_create_state(NULL, 0, _handleBtError, this);
}

// In C code we get the file name, line number and function name via this callback
// In Ocaml code we get the line number and file name via this callback and the function name via symInfo
int LogWriter::handlePcInfo(uintptr_t pc, const char* btFileName, int lineNumber, const char* btFunctionName) {
    // Always copy these char* values if we want to use them beyond the duration of this callback

    uint64_t fileId = 0;
    std::string fileName;
    if (btFileName != NULL) {
        fileName = btFileName;
        fileId = recordFile(fileName);
    }

    std::string functionName;
    if (btFunctionName != NULL) {
        functionName = btFunctionName;
    }

    // Ocaml fallback: use the function name from syminfo
    if (functionName.empty() && !currentSymbolName_.empty()) {
        functionName = currentSymbolName_;
    }

    // Other fallback: use dl - this can provide the binary file name for functions that don't have any debug symbols
    if (functionName.empty() || fileId == 0) {
        Dl_info dlInfo;
        const int ret = dladdr((void*) pc, &dlInfo);
        // not a typo - 0 means failure unlike everything else
        if (ret != 0) {
            if (functionName.empty()) {
                if (dlInfo.dli_sname != NULL) {
                    functionName = dlInfo.dli_sname;
                } else if (dlInfo.dli_fname != NULL) {
                    functionName.append("In ").append(dlInfo.dli_fname);
                }
            }

            // Use the binary name as the file name if it's missing
            if (fileId == 0 && dlInfo.dli_fname != NULL) {
                fileName = dlInfo.dli_fname;
                fileId = recordFile(fileName);
            }
        }
    }

    // We don't know the function name
    if (functionName.empty()) {
        functionName = "Unknown";
    }

    uint64_t functionId = 0;
    auto it = knownMethodToIds_.find(functionName);
    if (it != knownMethodToIds_.end()) {
        functionId = it->second;
    } else {
        functionId = nextId_++;
        knownMethodToIds_.insert({functionName, functionId});

        data::MethodInformation* methodInfo = nameAgentEnvelope_.mutable_method_information();
        methodInfo->set_methodid(functionId);
        methodInfo->set_methodname(functionName);
        methodInfo->set_moduleid(fileId);
        recordWithSize(nameAgentEnvelope_);
    }

    // Save the address information into the cache
    Location location = {
        functionId,
        lineNumber
    };
    currentLocations_->push_back(location);

    debugLogger_ << "PcInfo Lookup: pc=" << pc << ",func=" << functionName << ",file=" << fileName << endl;

    return 0;
}

uint64_t LogWriter::recordFile(const std::string& fileName) {

    // Technically we're emitting a "module" aka class with an empty name on our protocol
    // Need to decide if the protocol needs modifying
    uint64_t fileId = 0;
    auto it = knownFileToIds_.find(fileName);
    if (it != knownFileToIds_.end()) {
        fileId = it->second;
    } else {
        fileId = nextId_++;
        knownFileToIds_.insert({fileName, fileId});

        data::ModuleInformation *moduleInfo = nameAgentEnvelope_.mutable_module_information();
        moduleInfo->set_moduleid(fileId);
        moduleInfo->set_filename(fileName);
        moduleInfo->set_modulename("");

        recordWithSize(nameAgentEnvelope_);
    }
    return fileId;
}

void LogWriter::handleSyminfo(uintptr_t pc, const char* btSymbolName, uintptr_t symval, uintptr_t symsize) {
    // Always copy char* value
    if (btSymbolName != NULL) {
        currentSymbolName_ = btSymbolName;
        debugLogger_ << "SymInfo Lookup: pc=" << pc << ",sym=" << btSymbolName << endl;
    }
}

void addFrames(vector<Location>& locations, data::StackSample* stackSample) {
    for (auto it = locations.begin(); it != locations.end(); ++it) {
        data::CompressedFrameEntry *frameEntry = stackSample->add_compressedframes();
        frameEntry->set_methodid(it->methodId);
        frameEntry->set_line(it->lineNumber);
    }
}

void LogWriter::recordStackTrace(
        const timespec& ts,
        const CallTrace& trace,
        int signum,
        int threadState,
        uint64_t time_tsc) {
    debugLogger_ << "start record, capture time = (" << time_tsc << ")" << endl;

    data::StackSample* stackSample = frameAgentEnvelope_.mutable_stack_sample();
    setSampleType(signum, stackSample);
    setSampleTime(ts, stackSample);

    threadName(trace.threadId, stackSample);

    stackSample->set_thread_state(threadState);

    int numFrames = trace.num_frames;
    const bool isError = numFrames < 0;
    stackSample->clear_compressedframes();
    stackSample->set_error_code(isError ? numFrames : 0);

    // Lookup symbols for frames
    CallFrame* frames = trace.frames;
    if (!isError) {
        for (int frameIndex = NUMBER_OF_SIGNAL_HANDLER_FRAMES; frameIndex < numFrames; frameIndex++) {
            uintptr_t pc = frames[frameIndex].frame;

            auto it = knownAddrToLocations_.find(pc);
            if (it != knownAddrToLocations_.end()) {
                // If we've seen this address before, just add the compressed stack frame
                vector<Location>& addresses = it->second;
                addFrames(addresses, stackSample);
            } else {
                // Looked the symbol information from dwarf
                vector<Location> locations;
                currentLocations_ = &locations;
                currentSymbolName_.clear();

                if (!frames[frameIndex].isForeign) {
                    // Ocaml's dwarf function names don't appear to identified using backtrace_pcinfo, not sure why
                    // So we use backtrace_syminfo to identify them. NB: this only appears to provide a single symbol
                    // in the case of inlined functions.
                    backtrace_syminfo(btState, pc, _handleSyminfo, _handleBtError, this);
                }

                backtrace_pcinfo(btState, pc, _handlePcInfo, _handleBtError, this);

                knownAddrToLocations_.insert({pc, locations});
                addFrames(locations, stackSample);
            }
        }
    }

    stackSample->set_has_max_frames(numFrames >= MAX_FRAMES);

    recordWithSize(frameAgentEnvelope_);

    debugLogger_ << "end record" << endl;
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

void LogWriter::threadName(pthread_t threadId, data::StackSample* stackSample) {
    // TODO: widen the protocol type to 64bit integers
    stackSample->set_thread_id(threadId);

    auto it = threadIdToInformation.find(threadId);
    if (it != threadIdToInformation.end()) {
        // If we've seen this address before, just add the compressed stack frame
        ThreadInformation& info = it->second;
        stackSample->set_thread_name(info.name);
    } else {
        const char* thread_name;

        char buf[THREAD_NAME_BUFFER_SIZE];
        size_t len;
        int ret = pthread_getname_np(threadId, buf, THREAD_NAME_BUFFER_SIZE);
        if (ret != 0) {
            // NB: error codes are ERANGE if buffer is too big or anything from open() - see errno-base.h
            // We special case ENOENT as that just means that the thread has died when we tried to lookup the name,
            // Which isn't really an error
            if (ret != ENOENT) {
                buffer_.pushNotification(data::NotificationCategory::USER_ERROR,
                                         "Error from pthread_getname_np: ", ret);
            }

            thread_name = "Unknown";
            len = 7;
        } else {
            thread_name = buf;
            len = strnlen(thread_name, THREAD_NAME_BUFFER_SIZE);
        }

        if (!IsStructurallyValidUTF8(thread_name, len)) {
            logError("Invalid thread name returned from pthread_getname_np '%s'\n", thread_name);

            thread_name = "Invalid";
            len = 7;
        }

        stackSample->set_thread_name(thread_name, len);
        ThreadInformation threadInformation = {
            threadId,
            thread_name
        };

        threadIdToInformation.insert({threadId, threadInformation});
    }
}

// ----------------------------
//      Inspection code
// ----------------------------

void LogWriter::recordThread(
        int threadId,
        const string& name) {

    // TODO: either re-add this when we hook thread creation or remove it.
    /*ThreadInformation threadInformation = {
        threadId,
        name
    };

    threadIdToInformation.insert({threadId, threadInformation});*/
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
    oss << categoryName << ": " << payload;
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

void LogWriter::onSocketConnected() {
    knownAddrToLocations_.clear();
    knownMethodToIds_.clear();
    knownFileToIds_.clear();
}
