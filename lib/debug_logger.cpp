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

void DebugLogger::open(const std::string& path) {
    if (path.empty()) {
        stream_ = NULL;
    } else {
        stream_ = new ofstream();
        stream_->open(path, std::ios::out | std::ios::trunc);
    }
}

void DebugLogger::on_fork(const std::string& path) {
    delete stream_;
    open(path);
    writeLogStart();
}
