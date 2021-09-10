#ifndef CPUDATA_READER_H
#define CPUDATA_READER_H

#include <unordered_set>
#include "metric_types.h"
#include "globals.h"

using std::string;
using std::vector;
using std::unordered_set;

class SeekableFile {
public:
    SeekableFile(const char* name)
      : file(name, std::ifstream::in),
        readable(file.good()),
        openError(!readable),
        error(readable ? "" : strerror(errno)) {
    }

    bool reset() {
        if (readable) {
            // getline() will set the badbit when it reaches eof so we need to clear them
            file.clear();
            // seek to the beginning of the file
            file.seekg(0);
        }

        return readable;
    }

    bool hasOpenError() {
        if (openError) {
            openError = false;
            return true;
        }

        return false;
    }

    string& getError() {
        return error;
    }

    std::ifstream file;

private:
    bool readable;
    bool openError;
    string error;
};

class CPUDataReader {
public:
    static std::atomic<uint32_t> errors;
    static std::atomic<uint32_t> warnings;

    CPUDataReader(vector<string>& disabledPrefixes)
      : cpuProcessEnabled(false),
        cpuSystemEnabled(false),
        cpuNCoresEnabled(false),
        memCoreEnabled(false),
        memExtendedEnabled(false),
        readNCores(false),
        readClockTicksPerSecond(false),
        sentErrors_(),
        clockTicksPerSecond(0),
        procMemNames(),
        procMemInfo("/proc/meminfo"),
        procStat("/proc/stat"),
        hasEmittedConstantMetrics_(false)  {

        updateEntryPrefixes(disabledPrefixes);

        procMemNames.insert("MemTotal:");
        procMemNames.insert("MemFree:");
        procMemNames.insert("MemAvailable:");
        procMemNames.insert("SwapTotal:");
        procMemNames.insert("SwapFree:");
        procMemNames.insert("Shmem:");
        procMemNames.insert("Dirty:");
        procMemNames.insert("Writeback:");
    }

    enum Error {
        RUSAGE_FAILURE,
        PROC_STAT_FILE_ERROR,
        PROC_STAT_PARSE_ERROR,
        PROC_MEMINFO_FILE_ERROR,
        PROC_MEMINFO_PARSE_ERROR,
    };

    void read(MetricDataListener& listener);

    void updateEntryPrefixes(vector<string>& disabledPrefixes);

    const bool hasEmittedConstantMetrics();

    void error(MetricDataListener& listener, const char* payload, const Error status);

private:
    bool cpuProcessEnabled;
    bool cpuSystemEnabled;
    bool cpuNCoresEnabled;
    bool memCoreEnabled;
    bool memExtendedEnabled;

    bool readNCores;
    bool readClockTicksPerSecond;
    unordered_set<Error, std::hash<int>> sentErrors_;
    int64_t clockTicksPerSecond;

    unordered_set<string> procMemNames;
    SeekableFile procMemInfo;
    SeekableFile procStat;
    bool hasEmittedConstantMetrics_;

    DISALLOW_COPY_AND_ASSIGN(CPUDataReader);
};

#endif // CPUDATA_READER_H
