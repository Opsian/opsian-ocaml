#ifndef OPSIAN_OCAML_PROC_SCANNER_H
#define OPSIAN_OCAML_PROC_SCANNER_H

#include <atomic>
#include <sys/types.h>

void on_processor_thread_start();

void on_metrics_thread_start();

void scan_threads();

#endif //OPSIAN_OCAML_PROC_SCANNER_H
