#ifndef OPSIAN_DEBUG_LOGGER_H
#define OPSIAN_DEBUG_LOGGER_H

#include "globals.h"
#include <iostream>
#include <fstream>

using std::ofstream;

class DebugLogger {

public:
    explicit DebugLogger(
        const std::string& path,
        const std::string& apiKey)
        : apiKey_(apiKey) {

        if (path.empty()) {
            stream_ = NULL;
        } else {
            stream_ = new ofstream();
            stream_->open(path, std::ios::out | std::ios::trunc);
        }
    }

    template <typename T>
    DebugLogger& operator<<(T const & value) {
        if (stream_ != nullptr) {
            *stream_ << value;
        }
        return *this;
    }

    // Simulate std::endl
    DebugLogger& operator<<( DebugLogger& (*pf)(DebugLogger&) ) {
        if (stream_ != nullptr) {
            *stream_ << ' ';
            putFormattedTime(*stream_);
            *stream_ << std::endl;
        }
        return *this;
    }

    bool isEnabled();

    void writeLogStart();

    ~DebugLogger();

private:
    ofstream* stream_;

    const std::string apiKey_;

    DISALLOW_COPY_AND_ASSIGN(DebugLogger);
};

DebugLogger& endl(DebugLogger&);

#endif //OPSIAN_DEBUG_LOGGER_H
