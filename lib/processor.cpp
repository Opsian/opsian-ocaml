#include <thread>
#include <iostream>
#include "processor.h"
#include "globals.h"
#include "proc_scanner.h"

extern "C" {

// Terribly hack
#define _Atomic 
#include "caml/misc.h"
#include <caml/threads.h>
}

const int MAX_POLLS = 10;

const uint64_t MIN_SLEEP_IN_MS = 2;
const uint64_t MAX_SLEEP_IN_MS = 200;

std::atomic_bool processorRunning{};
pthread_t processorThread = 0;

void Processor::run() {
    on_processor_thread_start();

    collectorController_.onStart();

    uint64_t sleepDurationInMs = MIN_SLEEP_IN_MS;

    // Want to check isRunning after every sleep_ms
    while (collectorController_.isOn() && processorRunning) {
        bool doneWork = collectorController_.poll();
        // poll() can sleep_ms as part of a reconnect backoff

        doneWork |= network_.poll();

        if (collectorController_.isActive()) {
            int i;
            for (i = 0; i < MAX_POLLS && buffer_.pop(queueListener_); i++) {
            }
            doneWork |= (i > 0);
        }

        if (!doneWork && processorRunning) {
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

void onProcessorExit() {
    processorRunning = false;

    int result = pthread_join(processorThread, nullptr);
    if (result) {
        logError("ERROR: failed to join processor thread %d\n", result);
    }
}

void Processor::start() {
    debugLogger_ << "Starting Processor Thread" << endl;

    atexit(onProcessorExit);

    processorRunning = true;
    int result = pthread_create(&processorThread, nullptr, &callbackToRunProcessor, this);
    if (result) {
        logError("ERROR: failed to start processor thread %d\n", result);
    }

    pthread_setname_np(processorThread, PROCESSOR_THREAD_NAME);
}

void Processor::on_fork() {
    processorThread = 0;
}
