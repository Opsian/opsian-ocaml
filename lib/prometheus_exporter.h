#ifndef OPSIAN_OCAML_PROMETHEUS_EXPORTER_H
#define OPSIAN_OCAML_PROMETHEUS_EXPORTER_H

#include "debug_logger.h"
#include "circular_queue.h"

QueueListener* prometheus_queue_listener();

bool bind_prometheus(const int port, DebugLogger& debugLogger);

#endif //OPSIAN_OCAML_PROMETHEUS_EXPORTER_H
