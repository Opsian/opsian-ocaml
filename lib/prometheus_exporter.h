#ifndef OPSIAN_OCAML_PROMETHEUS_EXPORTER_H
#define OPSIAN_OCAML_PROMETHEUS_EXPORTER_H

#include "debug_logger.h"

bool init_prometheus(const int port, DebugLogger& debugLogger);

#endif //OPSIAN_OCAML_PROMETHEUS_EXPORTER_H
