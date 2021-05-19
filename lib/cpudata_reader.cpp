#include "cpudata_reader.h"
#include "gen/data.pb.h"

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <string>
#include <stdexcept>

static const int KILO_TO_BYTES = 1024;

using boost::format;

std::atomic<uint32_t> CPUDataReader::errors(0);
std::atomic<uint32_t> CPUDataReader::warnings(0);

static const string MEM_CORE_NAME_PREFIX = "mem.system.core.";
static const string MEM_EXTENDED_NAME_PREFIX = "mem.system.extended.";

void addLongEntry(
    vector<MetricListenerEntry>& entries,
    const char* name,
    const MetricUnit unit,
    const MetricVariability variability,
    const int64_t value) {

    struct MetricListenerEntry entry;

    entry.name = string(name);
    entry.unit = unit;
    entry.variability = variability;
    entry.data.type = MetricDataType::LONG;
    entry.data.valueLong = value;

    entries.push_back(entry);
}

void addLongVar(
    vector<MetricListenerEntry>& entries,
    const char* name,
    const MetricUnit unit,
    const int64_t value) {

    addLongEntry(entries, name, unit, MetricVariability::VARIABLE, value);
}

int64_t timevalToMs(timeval ts) {
    return (ts.tv_sec * 1000) + (ts.tv_usec / 1000);
}

#define ticksAt(X) (boost::lexical_cast<int64_t>(result.at(X)) * 1000) / clockTicksPerSecond
#define int64At(X) boost::lexical_cast<int64_t>(result.at(X))
#define kbAt(X) boost::lexical_cast<int64_t>(result.at(X)) * KILO_TO_BYTES

void CPUDataReader::read(MetricDataListener& listener) {
    if (cpuNCoresEnabled && !readNCores) {
        vector<MetricListenerEntry> entries;

        // So technically this could change with for example virtual machines, etc.
        int ncores = get_nprocs();

        addLongEntry(entries, "cpu.ncores", MetricUnit::NONE, MetricVariability::CONSTANT, ncores);

        listener.recordEntries(entries);

        readNCores = true;
    }

    if (cpuProcessEnabled) {
        vector<MetricListenerEntry> entries;

        struct rusage usage;

        int err = getrusage(RUSAGE_SELF, &usage);

        if (!err) {
            addLongVar(entries, "cpu.process.time.user", MetricUnit::MILLISECONDS, timevalToMs(usage.ru_utime));
            addLongVar(entries, "cpu.process.time.system", MetricUnit::MILLISECONDS, timevalToMs(usage.ru_stime));
            addLongVar(entries, "cpu.process.maxrss", MetricUnit::BYTES, usage.ru_maxrss * KILO_TO_BYTES);
            addLongVar(entries, "cpu.process.page_faults.soft", MetricUnit::EVENTS, usage.ru_minflt);
            addLongVar(entries, "cpu.process.page_faults.hard", MetricUnit::EVENTS, usage.ru_majflt);
            addLongVar(entries, "cpu.process.blocks.in", MetricUnit::EVENTS, usage.ru_inblock);
            addLongVar(entries, "cpu.process.blocks.out", MetricUnit::EVENTS, usage.ru_oublock);
            addLongVar(entries, "cpu.process.csw.voluntary", MetricUnit::EVENTS, usage.ru_nvcsw);
            addLongVar(entries, "cpu.process.csw.involuntary", MetricUnit::EVENTS, usage.ru_nivcsw);

            listener.recordEntries(entries);
        } else {
            error(listener, (boost::format("Got getrusage error of %d") % err).str().c_str(), RUSAGE_FAILURE);
        }
    }

    if (cpuSystemEnabled) {

        if (!readClockTicksPerSecond) {
            clockTicksPerSecond = sysconf(_SC_CLK_TCK);
            readClockTicksPerSecond = true;
        }

        if (procStat.reset()) {
            vector<MetricListenerEntry> entries;
            string line;
            vector<string> result;

            while (getline(procStat.file, line)) {
                result.clear();
                boost::split(result, line, boost::is_any_of(" "), boost::algorithm::token_compress_on);

                try {
                    if (result.at(0) == "cpu") {
                        addLongVar(entries, "cpu.system.user", MetricUnit::MILLISECONDS, ticksAt(1));
                        addLongVar(entries, "cpu.system.nice", MetricUnit::MILLISECONDS, ticksAt(2));
                        addLongVar(entries, "cpu.system.system", MetricUnit::MILLISECONDS, ticksAt(3));
                        addLongVar(entries, "cpu.system.idle", MetricUnit::MILLISECONDS, ticksAt(4));
                        addLongVar(entries, "cpu.system.iowait", MetricUnit::MILLISECONDS, ticksAt(5));
                        addLongVar(entries, "cpu.system.irq", MetricUnit::MILLISECONDS, ticksAt(6));
                        addLongVar(entries, "cpu.system.softirq", MetricUnit::MILLISECONDS, ticksAt(7));
                        addLongVar(entries, "cpu.system.steal", MetricUnit::MILLISECONDS, ticksAt(8));
                        addLongVar(entries, "cpu.system.guest", MetricUnit::MILLISECONDS, ticksAt(9));
                        addLongVar(entries, "cpu.system.guest_nice", MetricUnit::MILLISECONDS, ticksAt(10));
                    } else if (result.at(0) == "page") {
                        addLongVar(entries, "cpu.system.pages.in", MetricUnit::EVENTS, int64At(1));
                        addLongVar(entries, "cpu.system.pages.out", MetricUnit::EVENTS, int64At(2));
                    } else if (result.at(0) == "swap") {
                        addLongVar(entries, "cpu.system.swap.in", MetricUnit::EVENTS, int64At(1));
                        addLongVar(entries, "cpu.system.swap.out", MetricUnit::EVENTS, int64At(2));
                    } else if (result.at(0) == "ctxt") {
                        addLongVar(entries, "cpu.system.csw", MetricUnit::EVENTS, int64At(1));
                    } else if (result.at(0) == "procs_running") {
                        addLongVar(entries, "cpu.system.procs.running", MetricUnit::NONE, int64At(1));
                    } else if (result.at(0) == "procs_blocked") {
                        addLongVar(entries, "cpu.system.procs.blocked", MetricUnit::NONE, int64At(1));
                    }
                }
                catch (const boost::bad_lexical_cast& ex) {
                    error(listener,
                          (boost::format("Got bad_lexical_cast exception while parsing /proc/stat: %s") % ex.what()).str().c_str(),
                          PROC_STAT_PARSE_ERROR);
                    continue;
                }
                catch (const std::out_of_range& oor) {
                    continue; // This can happen in really old kernels. Older than even Redhat support.
                }
            }

            listener.recordEntries(entries);
        } else if (procStat.hasOpenError()) {
            error(listener, procStat.getError().c_str(), PROC_STAT_FILE_ERROR);
        }
    }

    if (memCoreEnabled) {
        if (procMemInfo.reset()) {
            vector<MetricListenerEntry> entries;
            string line;
            vector<string> result;

            while (getline(procMemInfo.file, line)) {
                result.clear();
                boost::split(result, line, boost::is_any_of(" "), boost::algorithm::token_compress_on);

                try {
                    string& entryName = result.at(0);
                    const bool isCoreMetric = procMemNames.count(entryName) == 1;
                    if (memExtendedEnabled || isCoreMetric) {
                        entryName.erase(entryName.size() - 1);
                        const string& prefix = isCoreMetric ? MEM_CORE_NAME_PREFIX : MEM_EXTENDED_NAME_PREFIX;
                        string name = prefix + entryName;
                        // Entries either end in kB for kilobytes or nothing
                        if (result.size() > 2) {
                            addLongVar(entries, name.c_str(), MetricUnit::BYTES, kbAt(1));
                        } else {
                            addLongVar(entries, name.c_str(), MetricUnit::NONE, int64At(1));
                        }
                    }
                }
                catch (const boost::bad_lexical_cast& ex) {
                    error(listener,
                          (boost::format("Got bad_lexical_cast exception while parsing /proc/meminfo: %s") %
                           ex.what()).str().c_str(),
                          PROC_MEMINFO_PARSE_ERROR);
                    continue;
                }
                catch (const std::out_of_range& oor) {
                    continue; // This can happen in really old kernels. Older than even Redhat support.
                }
            }

            listener.recordEntries(entries);
        } else if (procMemInfo.hasOpenError()) {
            error(listener, procMemInfo.getError().c_str(), PROC_MEMINFO_FILE_ERROR);
        }
    }

    // Always get emitted on the first run
    if (!hasEmittedConstantMetrics_) {
        hasEmittedConstantMetrics_ = true;
    }
}

static const string cpuProcessName = string("cpu.process");
static const string cpuSystemName = string("cpu.system");
static const string cpuNCoresName = string("cpu.ncores");
static const string memCoreName = string("mem.system.core");
static const string memExtendedName = string("mem.system.extended");

void CPUDataReader::updateEntryPrefixes(vector<string>& disabledPrefixes) {
    cpuProcessEnabled = !isDisabled(cpuProcessName, disabledPrefixes);
    cpuSystemEnabled = !isDisabled(cpuSystemName, disabledPrefixes);
    cpuNCoresEnabled = !isDisabled(cpuNCoresName, disabledPrefixes);
    memCoreEnabled = !isDisabled(memCoreName, disabledPrefixes);
    memExtendedEnabled = !isDisabled(memExtendedName, disabledPrefixes);
}

bool CPUDataReader::isDisabled(const string& entryName, vector<string>& disabledPrefixes) {
    for (auto it = disabledPrefixes.begin(); it != disabledPrefixes.end(); ++it) {
        const auto& enabledPrefix = *it;
        if (entryName.compare(0, enabledPrefix.length(), enabledPrefix) == 0) {
            return true;
        }
    }

    return false;
}

void CPUDataReader::error(
    MetricDataListener& listener, const char* payload, const Error status) {

    if (sentErrors_.insert(status).second) {
        // User-level readable error message
        listener.recordNotification(data::NotificationCategory::USER_ERROR, "Unable to record CPU Metrics");
        // Detailed information for us about why the error happened.
        listener.recordNotification(data::NotificationCategory::INFO_LOGGING, payload);
    }

    errors++;
}

const bool CPUDataReader::hasEmittedConstantMetrics() {
    return hasEmittedConstantMetrics_;
}
