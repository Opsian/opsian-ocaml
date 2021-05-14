#ifndef OPSIAN_INTERNALQUEUE_H
#define OPSIAN_INTERNALQUEUE_H

#include "globals.h"
#include <unistd.h>
#include <cstddef>

const int COMMITTED = 1;
const int UNCOMMITTED = 0;

template <typename T, size_t Size>
class InternalQueue {
public:
    // Capacity is 1 larger than size to make sure
    // we can use input = output as our "can't read" invariant
    // and advance(output) = input as our "can't write" invariant
    // effective the gap acts as a sentinel
    static const size_t Capacity = Size + 1;

    explicit InternalQueue() : input(0), output(0) {
    }

    bool acquireWrite(size_t& currentInput) {
        size_t nextInput;
        do {
            currentInput = input.load(std::memory_order_relaxed);
            nextInput = advance(currentInput);
            if (output.load(std::memory_order_relaxed) == nextInput) {
                return false;
            }
            // TODO: have someone review the memory ordering constraints
        } while (!input.compare_exchange_strong(currentInput, nextInput, std::memory_order_relaxed));

        return true;
    }

    bool acquireRead(size_t& currentOutput) {
        currentOutput = output.load(std::memory_order_relaxed);

        // queue is empty
        if (currentOutput == input.load(std::memory_order_relaxed)) {
            return false;
        }

        // wait until we've finished writing to the buffer
        while (buffer[currentOutput].is_committed.load(std::memory_order_acquire) != COMMITTED) {
            usleep(1);
        }

        return true;
    }

    void commitWrite(T& t) {
        t.is_committed.store(COMMITTED, std::memory_order_release);
    }

    void commitRead(size_t position) {
        // ensure that the record is ready to be written to
        buffer[position].is_committed.store(UNCOMMITTED, std::memory_order_release);
        // Signal that you've finished reading the record
        output.store(advance(position), std::memory_order_relaxed);
    }

    T& get(size_t position) {
        return buffer[position];
    }

private:
    std::atomic<size_t> input;
    std::atomic<size_t> output;

    T buffer[Capacity];

    size_t advance(size_t index) const {
        return (index + 1) % Capacity;
    }
};

#endif //OPSIAN_INTERNALQUEUE_H
