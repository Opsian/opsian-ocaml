#include <thread>
#include <iostream>
#include "processor.h"
#include "globals.h"
#include "proc_scanner.h"

extern "C" {
#include "caml/misc.h"
#include <caml/threads.h>
}

const int MAX_POLLS = 10;

const uint64_t MIN_SLEEP_IN_MS = 2;
const uint64_t MAX_SLEEP_IN_MS = 200;

void Processor::run() {
    on_processor_thread_start();

    collectorController_.onStart();

    uint64_t sleepDurationInMs = MIN_SLEEP_IN_MS;

    // Want to check isRunning after every sleep_ms
    while (collectorController_.isOn()) {
        bool doneWork = collectorController_.poll();
        // poll() can sleep_ms as part of a reconnect backoff

        doneWork |= network_.poll();

        if (collectorController_.isConnected()) {
            int i;
            for (i = 0; i < MAX_POLLS && buffer_.pop(logWriter_); i++) {
            }
            doneWork |= (i > 0);
        }

        if (!doneWork) {
            sleep_ms(sleepDurationInMs);
            sleepDurationInMs = std::min(MAX_SLEEP_IN_MS, sleepDurationInMs * 2);
        } else {
            sleepDurationInMs = MIN_SLEEP_IN_MS;
        }
    }

    collectorController_.onEnd();
}

void* callbackToRunProcessor(void *arg) {
    //Avoid having the processor thread also receive the PROF signals
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPROF);
    sigaddset(&mask, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) < 0) {
        logError("ERROR: failed to set processor thread signal mask\n");
    }

    int res = caml_c_thread_register();
    if (res != 1) {
        logError("Unable to register metrics thread with ocaml");
    }

    Processor *processor = (Processor *) arg;
    processor->run();

    res = caml_c_thread_unregister();
    if (res != 1) {
        logError("Unable to unregister metrics thread with ocaml");
    }

    return nullptr;
}

void Processor::start() {
    debugLogger_ << "Starting Processor Thread" << endl;

    int result = pthread_create(&thread, nullptr, &callbackToRunProcessor, this);
    if (result) {
        logError("ERROR: failed to start processor thread %d\n", result);
    }

    pthread_setname_np(thread, PROCESSOR_THREAD_NAME);
}

void Processor::on_fork() {
    thread = 0;
}
