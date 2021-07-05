//
//

#ifndef OPSIAN_OCAML_PROC_SCANNER_H
#define OPSIAN_OCAML_PROC_SCANNER_H

#include <atomic>
#include <sys/types.h>

extern pid_t metrics_thread_id;
extern pid_t processor_thread_id;

// TODO: review the ordering constraints on store and load to ensure appropriate barriers
extern std::atomic_bool metrics_thread_started;
extern std::atomic_bool processor_thread_started;

void scan_threads();

#endif //OPSIAN_OCAML_PROC_SCANNER_H
