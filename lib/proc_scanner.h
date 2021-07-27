#ifndef OPSIAN_OCAML_PROC_SCANNER_H
#define OPSIAN_OCAML_PROC_SCANNER_H

#include <atomic>
#include <sys/types.h>

// --------------------
//   Processor Thread
// --------------------

bool is_scanning_threads();
void update_scanning_threads_interval(const uint64_t interval_in_ms);
void stop_scanning_threads();

void on_processor_thread_start();

// -------------------
//   Metrics Thread
// -------------------

void on_metrics_thread_start();
void scan_threads();

// -------------------
//   Fork Thread
// -------------------

void reset_scan_threads();

#endif //OPSIAN_OCAML_PROC_SCANNER_H
