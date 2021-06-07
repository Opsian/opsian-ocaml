#define TARGET_amd64
#define CAML_INTERNALS
#define _GNU_SOURCE

#include "linkable_profiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

#include "caml/backtrace.h"
#include "caml/backtrace_prim.h"
#include "caml/codefrag.h"
#include "caml/misc.h"
#include "caml/mlvalues.h"
#include "caml/stack.h"

#define UNW_LOCAL_ONLY
#include <libunwind.h>

// Code is split out into a separate C file rather than C++ in order to work around Dune / Ocaml linking issue
// between Ocaml internals and C++.

int linkable_handle(CallFrame* frames) {
    int num_frames = 0;
    int ret;
    unw_context_t ucp;
    unw_cursor_t cursor;
    unw_word_t uw_ip, uw_sp;

    ret = unw_getcontext(&ucp);

    if (ret == -1) {
        // TODO: Increment error stat here
        printf("unw_getcontext failed\n");
        return ret;
    }

    ret = unw_init_local(&cursor, &ucp);

    if (ret < 0) {
        // TODO: Increment error stat here. Use error codes from unw_init_local
        printf("unw_init_local failed\n");
        return ret;
    }

    // TODO: Get the return value from unw_step and increase the error codes
    while (num_frames < MAX_FRAMES) {
        ret = unw_step(&cursor);

        if (ret <= 0) {
            // Use error codes for this and log
            break;
        }

        struct code_fragment* frag;

        unw_get_reg(&cursor, UNW_REG_IP, &uw_ip);
        unw_get_reg(&cursor, UNW_REG_SP, &uw_sp);

        frames[num_frames].frame = (uint64_t) uw_ip;
        frames[num_frames].isForeign = true;
        num_frames += 1;

        frag = caml_find_code_fragment_by_pc((char*) uw_ip);
        if (frag != NULL) {
            uint64_t pc;
            char* sp;

            pc = (uint64_t) uw_ip;
            sp = (char*) uw_sp;

            while (num_frames < MAX_FRAMES) {
                frame_descr* fd = caml_next_frame_descriptor(&pc, &sp);

                if (fd == NULL) {
                    break;
                }

                frames[num_frames].frame = pc;
                frames[num_frames].isForeign = false;
                num_frames += 1;
            }

            break;
        }
    }

    return num_frames;
}
