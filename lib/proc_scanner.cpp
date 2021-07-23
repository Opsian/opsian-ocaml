#include "proc_scanner.h"
#include "globals.h"
#include "debug_logger.h"
#include <dirent.h>
#include <stdio.h>
#include <unordered_set>
#include <stdlib.h>

std::unordered_set<pid_t> lastScanThreads{};

pid_t metrics_thread_id(0);
pid_t processor_thread_id(0);

std::atomic_bool metrics_thread_started(false);
std::atomic_bool processor_thread_started(false);

void start_timer(const pid_t tid, const clockid_t clock_type, const int signal_number, const long tv_nsec) {
    int ret;
    struct sigevent sevp;
    timer_t timerid;
    memset(&sevp, 0, sizeof(sevp));
    sevp.sigev_notify = SIGEV_THREAD_ID; // for per-thread
//    sevp.sigev_notify = SIGEV_SIGNAL; // for process-wide
// NB: this is a workaround for sigev_notify_thread_id not being defined in signal.h despite the internal linux kernel data structures having that field.
// See https://sourceware.org/bugzilla/show_bug.cgi?id=27417 for portable solution
    sevp._sigev_un._tid = tid; // for per-thread
    sevp.sigev_signo = signal_number;
    ret = timer_create(clock_type, &sevp, &timerid);

    if (ret) {
        logError("aborting due to timer_create error: %s", strerror(errno));
    }

    struct itimerspec timerSpec;
    timerSpec.it_interval.tv_sec = 0;
    timerSpec.it_interval.tv_nsec = tv_nsec;
    timerSpec.it_value = timerSpec.it_interval;
    ret = timer_settime(timerid, 0, &timerSpec, 0);
    if (ret) {
        logError("aborting due to timer_settime error: %s", strerror(errno));
    }
}

void start_profiling_thread(const pid_t tid) {
    start_timer(tid, CLOCK_MONOTONIC, SIGALRM, 50000000);
    // We can use CLOCK_THREAD_CPUTIME_ID if we want to to do per thread CPU timers
}

void scan_threads() {
    if (metrics_thread_started.load() && processor_thread_started.load()) {
        // Do it C-style to anticipate our migration over to C.

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
            if (pid == metrics_thread_id || pid == processor_thread_id) {
                continue;
            }

            currentThreads.insert(pid);

            if (lastScanThreads.count(pid) == 0) {
                *_DEBUG_LOGGER <<  "New Thread from /proc scanner: " << pid << endl;

                start_profiling_thread(pid);
            }
        }

        lastScanThreads = currentThreads;

        closedir(task);
    }
}

void on_processor_thread_start() {
    processor_thread_id = getTid();
    processor_thread_started.store(true);
}

void on_metrics_thread_start() {
    metrics_thread_id = getTid();
    metrics_thread_started.store(true);
}
