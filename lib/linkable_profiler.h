#ifndef LINKABLE_PROFILER_H
#define LINKABLE_PROFILER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    // true iff we've unwound the frame using dwarf - so it's C, C++, Rust or something.
    // false means its Ocaml code
    bool isForeign;
    uint64_t frame;
}  CallFrame;

typedef struct {
    int num_frames;
    pthread_t threadId;
    CallFrame* frames;
} CallTrace;

typedef uint64_t VMSymbol;

const int MAX_FRAMES = 256;

typedef enum ErrorType_t {
    SUCCESS = 0,
    GET_CONTEXT_FAIL = 1,
    INIT_LOCAL_FAIL = 2,
    STEP_FAIL = 3,
} ErrorType;

typedef struct {
    int errorCode;
    ErrorType type;
} ErrorHolder;

#endif // LINKABLE_PROFILER_H
