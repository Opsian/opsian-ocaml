#include "proc_scanner.h"
#include "globals.h"
#include "debug_logger.h"
#include "limits.h"
#include <dirent.h>
#include <stdio.h>
#include <unordered_set>
#include <stdlib.h>

#define MS_TO_NS 1000000
#define NOT_SCANNING ULONG_MAX

std::unordered_set<pid_t> last_scan_threads_{};
std::unordered_set<timer_t> timers_{};

pid_t metrics_thread_id_(0);
pid_t processor_thread_id_(0);

std::atomic_bool metrics_thread_started_(false);
std::atomic_bool processor_thread_started_(false);

std::atomic_uint64_t atomic_interval_in_ns_(NOT_SCANNING);
uint64_t local_interval_in_ns_(NOT_SCANNING);

// --------------------
//   Processor Thread
// --------------------

void on_processor_thread_start() {
    processor_thread_id_ = getTid();
    processor_thread_started_.store(true);
}

bool is_scanning_threads() {
    return atomic_interval_in_ns_.load() != NOT_SCANNING;
}

void update_scanning_threads_interval(const uint64_t interval_in_ms) {
    atomic_interval_in_ns_.store(interval_in_ms * MS_TO_NS);
}

void stop_scanning_threads() {
    update_scanning_threads_interval(NOT_SCANNING);
}

// -------------------
//   Metrics Thread
// -------------------

void on_metrics_thread_start() {
    metrics_thread_id_ = getTid();
    metrics_thread_started_.store(true);
}

void set_timer_interval(const long interval_ns, const timer_t& timer_id) {
    struct itimerspec timerSpec;
    timerSpec.it_interval.tv_sec = 0;
    timerSpec.it_interval.tv_nsec = interval_ns;
    timerSpec.it_value = timerSpec.it_interval;
    const int ret = timer_settime(timer_id, 0, &timerSpec, 0);
    if (ret) {
        logError("aborting due to timer_settime error: %s", strerror(errno));
    }
}

void start_timer(
    const pid_t tid,
    const clockid_t clock_type,
    const int signal_number,
    const long interval_ns) {

    struct sigevent sevp;
    timer_t timer_id;
    memset(&sevp, 0, sizeof(sevp));
    sevp.sigev_notify = SIGEV_THREAD_ID; // for per-thread
    //    sevp.sigev_notify = SIGEV_SIGNAL; // for process-wide
    // NB: this is a workaround for sigev_notify_thread_id not being defined in signal.h despite
    // the internal linux kernel data structures having that field.
    // See https://sourceware.org/bugzilla/show_bug.cgi?id=27417 for portable solution
    sevp._sigev_un._tid = tid; // for per-thread
    sevp.sigev_signo = signal_number;
    const int ret = timer_create(clock_type, &sevp, &timer_id);
    if (ret != 0) {
        logError("aborting due to timer_create error: %s", strerror(errno));
        return;
    }

    timers_.insert(timer_id);

    set_timer_interval(interval_ns, timer_id);
}

void start_profiling_thread(const pid_t& tid) {
    start_timer(tid, CLOCK_MONOTONIC, SIGALRM, local_interval_in_ns_);
    // We can use CLOCK_THREAD_CPUTIME_ID if we want to to do per thread CPU timers
}

void on_interval_change(const uint64_t atomic_interval_in_ns) {
    const uint64_t old_local_interval_in_ns = local_interval_in_ns_;
    local_interval_in_ns_ = atomic_interval_in_ns;

    if (atomic_interval_in_ns == NOT_SCANNING) {
        // printf("stop scanning, disabled and delete existing timers\n");
        for (const timer_t& timer: timers_) {
            timer_delete(timer);
        }

        timers_.clear();
    } else if (old_local_interval_in_ns == NOT_SCANNING) {
        // printf("start scanning, create timers\n");
        for (const pid_t& thread: last_scan_threads_) {
            start_profiling_thread(thread);
        }
    } else {
        // printf("update timer intervals\n");
        for (const timer_t& timer: timers_) {
            set_timer_interval(local_interval_in_ns_, timer);
        }
    }
}

void scan_threads() {
    if (metrics_thread_started_.load() && processor_thread_started_.load()) {
        // Do it C-style to anticipate our migration over to C.

        const uint64_t atomic_interval_in_ms = atomic_interval_in_ns_.load();
        if (atomic_interval_in_ms != local_interval_in_ns_) {
            on_interval_change(atomic_interval_in_ms);
        }

        if (local_interval_in_ns_ == NOT_SCANNING) {
            return;
        }

        DIR* task = opendir("/proc/self/task");
        if (!task) {
            logError("task opendir failed: %d\n", task);
            return;
        }

        std::unordered_set<pid_t> currentThreads{};
        struct dirent *procThread;
        while ((procThread = readdir(task)) != NULL) {
            // filter self and parent entries
            if (procThread->d_name[0] == '.') {
                continue;
            }

            pid_t pid = strtol(procThread->d_name, NULL, 0);
            if (pid == metrics_thread_id_ || pid == processor_thread_id_) {
                continue;
            }

            currentThreads.insert(pid);

            if (last_scan_threads_.count(pid) == 0) {
                *_DEBUG_LOGGER <<  "New Thread from /proc scanner: " << pid << endl;

                start_profiling_thread(pid);
            }
        }

        last_scan_threads_ = currentThreads;

        closedir(task);
    }
}

// -------------------
//   Fork Thread
// -------------------

void reset_scan_threads() {
    last_scan_threads_.clear();
    timers_.clear();
    metrics_thread_id_ = 0;
    processor_thread_id_ = 0;
    metrics_thread_started_.store(false);
    processor_thread_started_.store(false);
    atomic_interval_in_ns_.store(NOT_SCANNING);
    local_interval_in_ns_ = NOT_SCANNING;
}
