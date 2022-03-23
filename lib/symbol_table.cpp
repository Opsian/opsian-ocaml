#include "symbol_table.h"

#include <dlfcn.h>
#include <link.h>
#include <unordered_set>
#include <unordered_map>

using std::unordered_set;
using std::unordered_map;
using std::string;

// ----------------
// BEGIN STATE
// ----------------

// Symbol information cache
// Each individual pc might actually correspond to multiple locations due to inlining
uint64_t nextId_ = 1;
unordered_map<uintptr_t, vector<Location>> knownAddrToLocations_;
unordered_map<string, uint64_t> knownMethodToIds_;
unordered_map<string, uint64_t> knownFileToIds_;

// used in lib backtrace callbacks
vector<Location>* currentLocations_;
string currentSymbolName_;

backtrace_error_callback error_callback_;
NewFileCallback newFileCallback_;
NewFunctionCallback newFunctionCallback_;
DebugLogger* debugLoggerSt_;
void* data_ = nullptr;

struct backtrace_state* btState_ = NULL;

// ----------------
// END STATE
// ----------------

// ----------------
// BEGIN CALLBACKS
// ----------------

uint64_t recordFile(const string& fileName) {

    // Technically we're emitting a "module" aka class with an empty name on our protocol
    // Need to decide if the protocol needs modifying
    uint64_t fileId = 0;
    auto it = knownFileToIds_.find(fileName);
    if (it != knownFileToIds_.end()) {
        fileId = it->second;
    } else {
        fileId = nextId_++;
        knownFileToIds_.insert({fileName, fileId});
        if (newFileCallback_ != nullptr) {
            newFileCallback_(data_, fileId, fileName);
        }
    }
    return fileId;
}

// In C code we get the file name, line number and function name via this callback
// In Ocaml code we get the line number and file name via this callback and the function name via symInfo
int handlePcInfo (
    void *data,
    uintptr_t pc,
    const char *btFileName,
    int lineNumber,
    const char *btFunctionName) {

    // Always copy these char* values if we want to use them beyond the duration of this callback

    uint64_t fileId = 0;
    string fileName;
    if (btFileName != NULL) {
        fileName = btFileName;
        fileId = recordFile(fileName);
    }

    string functionName;
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
        if (newFunctionCallback_ != nullptr) {
            newFunctionCallback_(data_, functionId, functionName, fileId);
        }
    }

    // Save the address information into the cache
    Location location = {
        functionId,
        lineNumber,
        fileName,
        functionName
    };
    currentLocations_->push_back(location);

    *debugLoggerSt_ << "PcInfo Lookup: pc=" << pc << ",func=" << functionName << ",file=" << fileName << endl;

    return 0;
}

void handleSyminfo (
    void *data,
    uintptr_t pc,
    const char *btSymbolName,
    uintptr_t symval,
    uintptr_t symsize) {

    // Always copy char* value
    if (btSymbolName != NULL) {
        currentSymbolName_ = btSymbolName;
        *debugLoggerSt_ << "SymInfo Lookup: pc=" << pc << ",sym=" << btSymbolName << endl;
    }
}

// ----------------
// END CALLBACKS
// ----------------

// ----------------
// BEGIN PUBLIC API
// ----------------

void init_symbols(
    backtrace_error_callback error_callback,
    NewFileCallback newFileCallback,
    NewFunctionCallback newFunctionCallback,
    DebugLogger* debugLoggerSt,
    void* data) {

    error_callback_ = error_callback;
    newFileCallback_ = newFileCallback;
    newFunctionCallback_ = newFunctionCallback;
    debugLoggerSt_ = debugLoggerSt;
    data_ = data;

    // Only init these once, but record the last set of initialized callbacks
    if (btState_ == nullptr) {
        btState_ = backtrace_create_state(nullptr, 0, error_callback_, data_);
    }
}

vector<Location>& lookup_locations(const uintptr_t pc, const bool isForeign) {
    auto it = knownAddrToLocations_.find(pc);
    if (it != knownAddrToLocations_.end()) {
        // If we've seen this address before, just add the compressed stack frame
        return it->second;
    } else {
        // Looked the symbol information from dwarf
        currentLocations_ = new vector<Location>();
        currentSymbolName_.clear();

        if (!isForeign) {
            // Ocaml's dwarf function names don't appear to identified using backtrace_pcinfo, not sure why
            // So we use backtrace_syminfo to identify them. NB: this only appears to provide a single symbol
            // in the case of inlined functions.
            backtrace_syminfo(btState_, pc, handleSyminfo, error_callback_, nullptr);
        }

        backtrace_pcinfo(btState_, pc, handlePcInfo, error_callback_, nullptr);

        knownAddrToLocations_.insert({pc, *currentLocations_});
        return *currentLocations_;
    }
}

void clear_symbols() {
    knownAddrToLocations_.clear();
    knownMethodToIds_.clear();
    knownFileToIds_.clear();
}

// ----------------
// END PUBLIC API
// ----------------
