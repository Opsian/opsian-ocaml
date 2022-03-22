#include "protocol_handler.h"

void ProtocolHandler::attemptRead() {
    if (network_.isConnected()) {
        debugLogger_ << "attemptRead" << endl;
        asio::async_read(
            network_.sock(),
            asio::buffer(buffer.data(), MAX_HEADER_SIZE),
            boost::bind(
                &ProtocolHandler::onReadHeader,
                this,
                asio::placeholders::error));
    }
}

void ProtocolHandler::onReadHeader(const error_code& ec) {
    if (ec) {
        if (ec.value() == asio::error::eof) {
            logError("Disconnected\n");
        // We don't print operation cancelled, as it happens on shutdown
        } else if (ec != asio::error::operation_aborted) {
            Network::logNetError(ec, {"onReadHeader"}, debugLogger_);
        }
    } else {
        const int bytesRead = MAX_HEADER_SIZE;
        CodedInputStream cis(buffer.data(), bytesRead);
        if (cis.ReadVarint32(&size)) {
            offset = cis.CurrentPosition();
            debugLogger_ << "read header: " << size << ", offset: " << offset << endl;
            const int messagePrefixRead = bytesRead - offset;
            const size_t remaining = size - messagePrefixRead;
            if (remaining < 0) {
                logError("message smaller than MAX_HEADER_SIZE, fail");
                // TODO: support this case later
            }
            // Errors called on handler
            if (network_.isConnected()) {
                asio::async_read(
                    network_.sock(),
                    asio::buffer(buffer.data() + bytesRead, remaining),
                    boost::bind(
                        &ProtocolHandler::onReadBody,
                        this,
                        asio::placeholders::error));
            }
        } else {
            logError("Couldn't parse header length ");
            attemptRead();
        }
    }
}

void ProtocolHandler::onReadBody(const error_code& ec) {
    if (ec) {
        // We don't print operation cancelled, as it happens on shutdown
        if (ec != asio::error::operation_aborted) {
            Network::logNetError(ec, {"onReadBody"}, debugLogger_);
        }
        attemptRead();
    } else {
        if (!collectorEnvelope.ParseFromArray(buffer.data() + offset, size)) {
            logError("Failed to parse message");
        } else {
            // TODO: don't always make the debug string
            debugLogger_ << collectorEnvelope.DebugString() << endl;

            switch(collectorEnvelope.upstream_message_type_case())
            {
                case CollectorEnvelope::kTerminate:
                {
                    listener_.onTerminate();
                    break;
                }

                case CollectorEnvelope::kSampleRate:
                {
                    auto sampleRate = collectorEnvelope.sample_rate();

                    vector<string> metricsPrefixes = vector<string>(sampleRate.metrics_prefixes().begin(), sampleRate.metrics_prefixes().end());

                    listener_.onSampleRate(
                            sampleRate.process_time_stack_sample_rate_millis(),
                            sampleRate.elapsed_time_stack_sample_rate_millis(),
                            sampleRate.switch_process_time_profiling_on(),
                            sampleRate.switch_elapsed_time_profiling_on(),
                            sampleRate.thread_state_on(),
                            sampleRate.switch_memory_profiling_on(),
                            sampleRate.memory_profiling_push_rate_millis(),
                            sampleRate.switch_memory_profiling_stacktrace_on(),
                            sampleRate.memory_profiling_stack_sample_rate_samples(),
                            sampleRate.switch_metrics_on(),
                            sampleRate.metrics_sample_rate_millis(),
                            metricsPrefixes);
                    break;
                }

                case CollectorEnvelope::kHeartbeat:
                {
                    listener_.onHeartbeat();
                    break;
                }

                default:
                {
                    //kGetSourceRequest
                    //UPSTREAM_MESSAGE_TYPE_NOT_SET
                    debugLogger_ << "unknown message" << collectorEnvelope.upstream_message_type_case() << endl;
                    break;
                }
            }
        }
        attemptRead();
    }
}
