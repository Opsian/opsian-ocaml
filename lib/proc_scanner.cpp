//
// Created by richard on 21/06/2021.
//

#include "proc_scanner.h"
#include "globals.h"
#include <dirent.h>
#include <stdio.h>
#include <unordered_set>
#include <stdlib.h>

/**
Biased profiling example:

14 140233673197440
27 140233648789248
14 140233673197440
14 140233673197440
27 140233648789248
14 140233673197440
27 140233648789248
14 140233673197440
27 140233648789248
14 140233673197440
14 140233673197440
27 140233648789248
 */

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
    start_timer(tid, CLOCK_THREAD_CPUTIME_ID, SIGPROF, 1);
}

void scan_threads() {
    if (metrics_thread_started.load() && processor_thread_started.load()) {
        // Do it C-style to anticipate our migration over to C.

        printf("/proc scan\n");

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
                printf("New Thread from /proc scanner: %d\n", pid);

                start_profiling_thread(pid);
            }
        }

        lastScanThreads = currentThreads;

        closedir(task);
    }
}
