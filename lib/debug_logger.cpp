#include "debug_logger.h"

#include "unistd.h"

DebugLogger& endl(DebugLogger& debugLogger) {
    return debugLogger;
}

void DebugLogger::writeLogStart() {
    *this << "git: "
          << GIT_STR
          << ", AGENT_VERSION: "
          << AGENT_VERSION
          << ", PID: "
          << getpid()
          << ", API KEY: "
          << apiKey_
          << endl;
}

DebugLogger::~DebugLogger() {
    delete stream_;
}

bool DebugLogger::isEnabled() {
    return stream_ != nullptr;
}
