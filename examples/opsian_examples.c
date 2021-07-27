// For writing examples that test interaction between the profiling agent and native.

#include <caml/mlvalues.h>
#include <caml/callback.h>

__attribute__((noinline)) void a_c_function();

__attribute__((noinline)) void do_work_wrapper();

__attribute__((noinline)) void do_work_wrapper() {
    static const value * do_work_callback_closure = NULL;
    if (do_work_callback_closure == NULL) do_work_callback_closure = caml_named_value("do_work_callback");
    caml_callback(*do_work_callback_closure, Val_int(7));
}

__attribute__((noinline)) void a_c_function() {
    do_work_wrapper();
}
