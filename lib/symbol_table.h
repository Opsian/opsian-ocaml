
#ifndef OPSIAN_OCAML_SYMBOL_TABLE_H
#define OPSIAN_OCAML_SYMBOL_TABLE_H

#include "deps/libbacktrace/backtrace.h"
#include "debug_logger.h"

#include <vector>
#include <string>

using std::vector;

// NB: if at any point we add or remove methods that are invoked in the signal handler this index
// needs to change.
// For some reason we can't discover the symbol of the restore trap of the libc signal handling mechanism.
// I think https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67365#c3
// might explain what's going on, but shifting the pc by 1 didn't seem to help. This is why we're using a frame count
#define NUMBER_OF_SIGNAL_HANDLER_FRAMES 3

struct Location {
    uint64_t methodId;
    int lineNumber;
    std::string fileName;
    std::string functionName;
};

typedef void (*NewFileCallback) (void *data, const uint64_t fileId, const std::string& fileName);
typedef void (*NewFunctionCallback) (void *data, const uint64_t functionId, const std::string& functionName,
    const uint64_t fileId);

void init_symbols(
    backtrace_error_callback error_callback,
    NewFileCallback newFileCallback,
    NewFunctionCallback newFunctionCallback,
    DebugLogger* debugLoggerSt,
    void* data);

// init_symbols must be called before this function
// return value only valid until next call to lookup_locations
vector<Location>& lookup_locations(const uintptr_t pc, const bool isForeign);

void clear_symbols();

#endif //OPSIAN_OCAML_SYMBOL_TABLE_H
