#ifndef LINKABLE_PROFILER_H
#define LINKABLE_PROFILER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // true iff we've unwound the frame using dwarf - so it's C, C++, Rust or something.
    // false means its Ocaml code
    bool isForeign;
    uint64_t frame;
}  CallFrame;

typedef struct {
    int num_frames;
    CallFrame* frames;
} CallTrace;

typedef uint64_t VMSymbol;

const int MAX_FRAMES = 256;

#endif // LINKABLE_PROFILER_H
