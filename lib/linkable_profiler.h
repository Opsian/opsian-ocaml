#ifndef LINKABLE_PROFILER_H
#define LINKABLE_PROFILER_H

#include <stdint.h>

typedef uint64_t CallFrame;
typedef struct {
    int num_frames;
    CallFrame* frames;
} CallTrace;
typedef uint64_t VMSymbol;

const int MAX_FRAMES = 256;

#endif // LINKABLE_PROFILER_H
