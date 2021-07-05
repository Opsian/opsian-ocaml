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

int linkable_handle(CallFrame* frames, ErrorHolder* holder) {
    int num_frames = 0;
    int ret;
    unw_context_t ucp;
    unw_cursor_t cursor;
    unw_word_t uw_ip, uw_sp;

    ret = unw_getcontext(&ucp);

    if (ret < 0) {
        holder->type = GET_CONTEXT_FAIL;
        holder->errorCode = ret;
        return ret;
    }

    ret = unw_init_local(&cursor, &ucp);

    if (ret < 0) {
        holder->type = INIT_LOCAL_FAIL;
        holder->errorCode = ret;
        return ret;
    }

    while (num_frames < MAX_FRAMES) {
        ret = unw_step(&cursor);

        if (ret == 0) {
            break;
        }

        if (ret < 0) {
            holder->type = STEP_FAIL;
            holder->errorCode = ret;
            break;
        }

        struct code_fragment* frag;

        unw_get_reg(&cursor, UNW_REG_IP, &uw_ip);
        unw_get_reg(&cursor, UNW_REG_SP, &uw_sp);

        frames[num_frames].frame = (uint64_t) uw_ip;
        frag = caml_find_code_fragment_by_pc((char*) uw_ip);
        frames[num_frames].isForeign = frag == NULL;

//        printf("%lu \n", (uint64_t) uw_ip);

        num_frames += 1;

        // Sadiq: Comment out this block in order to see libunwind equivalent.
        if (frag != NULL) {
            bool first_ocaml_frame = true;
            uint64_t pc;
            char* sp;

            pc = (uint64_t) uw_ip;
            sp = (char*) uw_sp;

            while (num_frames < MAX_FRAMES) {
                frame_descr* fd = caml_next_frame_descriptor(&pc, &sp);

//                printf("%lu %lu \n", (uint64_t) uw_ip, (uintptr_t) fd);

                if (fd == NULL) {
                    break;
                }

                first_ocaml_frame = false;

                frames[num_frames].frame = pc;
                frames[num_frames].isForeign = false;
                num_frames += 1;
            }

            // The first ocaml frame descriptor attempt may fail because Ocaml doesn't generate frame descriptors at
            // all points in the program. So we just let lib_unwind have another go.
            if (!first_ocaml_frame) {
                break;
            }
        }
    }

    return num_frames;
}
