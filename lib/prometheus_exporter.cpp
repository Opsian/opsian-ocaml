#include "prometheus_exporter.h"
#include "network.h"
#include "symbol_table.h"
#include <boost/asio.hpp>
#include <boost/system/system_error.hpp>
#include <unordered_map>
#include <vector>

namespace errc = boost::system::errc;
namespace asio = boost::asio;
namespace error = asio::error;

using std::string;
using asio::ip::tcp;
using std::unordered_map;

// ------------------
// BEGIN Common State
// ------------------

struct ProfileNode {
    vector<Location> locations;
    unordered_map<uintptr_t, ProfileNode*> pcToNode;
    // true if we need to walk the subtree when printing, ie when there's >= 1 child with a count >= 1
    bool seenInPhase;

    int& count(const bool isCpuSample) {
        return isCpuSample ? cpuCount : wallclockCount;
    }

    void reset() {
        seenInPhase = false;
        cpuCount = 0;
        wallclockCount = 0;
    }

private:
    int cpuCount;
    int wallclockCount;
};

ProfileNode* root = nullptr;

void end_phase_node(ProfileNode* node) {
    if (node == nullptr) {
        return;
    }

    node->reset();

    for (auto& it: node->pcToNode) {
        end_phase_node(it.second);
    }
}

// ----------------
// END Common State
// ----------------

// ----------------
// BEGIN IO
// ----------------

const string prefix_200 =
    "HTTP/1.1 200 OK\n"
    "Content-Type: text/html; charset=utf-8\n\n"
    "# HELP promfiler_cpu_profile CPU stack trace samples\n"
    "# TYPE promfiler_cpu_profile gauge\n"
    "promfiler_cpu_profile{signature=\"(root)\"} 0\n";

const string response_404 =
    "HTTP/1.1 404 Not Found\n"
    "Content-Type: text/html; charset=utf-8\n\n"
    "<html><body><i>Not Found!</i></body></html>";

const string rootCpuPrefix = "promfiler_cpu_profile{type=\"cpu\",signature=\"";
const string rootWallclockPrefix = "promfiler_cpu_profile{type=\"wallclock\",signature=\"";

const int max_length = 1024;

tcp::acceptor* acceptor_ = NULL;
tcp::socket* socket_ = NULL;
DebugLogger* debugLogger_ = NULL;

// Performs an asynchronous read so as not to block the processor thread, but does do a single-threaded synchronous
// write for simplicity. This isn't an issue with prometheus scraping the data out, but is a limitation to be aware of.
// NB: socket is closed by the destructor
class session : public std::enable_shared_from_this<session> {
public:
    session(tcp::socket socket)
            : socket_(std::move(socket)) {
    }

    void start() {
        do_read();
    }

private:
    void write(const string& data) {
        boost::system::error_code write_ec;
        asio::write(socket_, asio::buffer(data), asio::transfer_all(), write_ec);
        if (write_ec) {
            Network::logNetError(write_ec, {"Prometheus write reply error"}, *debugLogger_);
        }
    }

    // Eg: promfiler_cpu_profile{signature=\"(root)#parserOnHeadersComplete\"} 1;
    void write_profile_node(ProfileNode* node, const string& prefix, const bool isCpuSample) {
        if (!node->seenInPhase) {
            return;
        }

        vector<string> functions;
        if (node == root) {
            functions.emplace_back("(root)");
        } else {
            for (auto& location : node->locations) {
                functions.emplace_back("#" + location.functionName);
            }
        }

        string commonStr = prefix;
        // (root) or #parserOnHeadersComplete
        for (string& functionName : functions) {
            // promfiler_cpu_profile{signature="(root)#parserOnHeadersComplete
            commonStr += functionName;

            // promfiler_cpu_profile{signature="(root)#parserOnHeadersComplete"} 1\n
            const string line = commonStr + "\"} " + std::to_string(node->count(isCpuSample)) + '\n';

            write(line);
        }

        for (auto& it: node->pcToNode) {
            write_profile_node(it.second, commonStr, isCpuSample);
        }
    }

    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (ec && ec != error::eof) {
                    Network::logNetError(ec, {"Prometheus read error"}, *debugLogger_);
                    return;
                }

                // Expecting HTTP request like:
                // GET /metrics HTTP/1.1
                // Host: localhost:9100
                // User-Agent: Prometheus/2.22.0+ds
                // Accept: application/openmetrics-text; version=0.0.1,text/plain;version=0.0.4;q=0.5,*/*;q=0.1
                // Accept-Encoding: gzip
                // X-Prometheus-Scrape-Timeout-Seconds: 10.000000

                string str(data_, length);
                const bool is_metrics = str.find("GET /metrics HTTP/1.1") != string::npos;

                if (is_metrics) {
                    write(prefix_200);
                    write_profile_node(root, rootCpuPrefix, true);
                    write_profile_node(root, rootWallclockPrefix, false);
                    end_phase_node(root);
                    root->seenInPhase = true;
                } else {
                    write(response_404);
                }
            });
    }

    tcp::socket socket_;
    char data_[max_length];
};

void do_accept() {
    acceptor_->async_accept(*socket_, [](boost::system::error_code ec) {
           if (!ec) {
               std::make_shared<session>(std::move(*socket_))->start();
           } else {
               Network::logNetError(ec, {"Prometheus accept error"}, *debugLogger_);
           }

           do_accept();
   });
}

bool bind_prometheus(const ConfigurationOptions& configuration, DebugLogger& debugLogger) {
    if (acceptor_ != NULL) {
        debugLogger << "failed to init_prometheus" << endl;
        return false;
    }

    const int port = configuration.prometheusPort;
    const std::string& host = configuration.prometheusHost;

    debugLogger_ = &debugLogger;
    asio::io_service &ios = getIos();
    try {
        tcp::endpoint endpoint;
        if (host.empty()) {
            endpoint = tcp::endpoint(tcp::v4(), port);
        } else {
            boost::system::error_code ec;
            if (ec.value() == 0) {
                endpoint = tcp::endpoint(asio::ip::address::from_string(host, ec), port);
            } else {
                Network::logNetError(ec, { "Prometheus IP address error" }, debugLogger);
                return false;
            }
        }
        acceptor_ = new tcp::acceptor(ios, endpoint);
        socket_ = new tcp::socket(ios);
        do_accept();

        debugLogger << "init_prometheus on " << host << ":" << port << endl;

        return true;
    } catch (boost::system::system_error& e) {
        Network::logNetError(e.code(), { "Prometheus bind error" }, debugLogger);
        return false;
    }
}

// ----------------
// END IO
// ----------------

// --------------------
// BEGIN Queue Listener
// --------------------

void handleBtError(void* data, const char* errorMessage, int errorNumber) {
    logError("libbacktrace error: %s %n", errorMessage, errorNumber);
}

class PrometheusQueueListener : public QueueListener {
public:
    explicit PrometheusQueueListener() {
        init_symbols(handleBtError, nullptr, nullptr, debugLogger_, nullptr);
    }

    // override
    virtual void
    recordStackTrace(
        const timespec &ts,
        const CallTrace &trace,
        int signum,
        int threadState,
        uint64_t time_tsc) {

        const bool isCpuSample = signum == SIGPROF;
        int numFrames = trace.num_frames;
        const bool isError = numFrames < 0;
        CallFrame* frames = trace.frames;
        // We print out an error traces for the missing frames
        if (isError) {
            numFrames = -1 * numFrames;
            logError("Broken stack trace error=%n", numFrames);
            return;
        }

        ProfileNode* node = root;
        for (int frameIndex = numFrames - 1; frameIndex >= NUMBER_OF_SIGNAL_HANDLER_FRAMES; frameIndex--) {
            const uintptr_t pc = frames[frameIndex].frame;

            auto it = node->pcToNode.find(pc);
            if (it != node->pcToNode.end()) {
                node = it->second;
            } else {
                const bool isForeign = frames[frameIndex].isForeign;

                ProfileNode* newNode = new ProfileNode();
                newNode->locations = lookup_locations(pc, isForeign);
                newNode->count(isCpuSample) = 0;

                node->pcToNode.insert({pc, newNode});
                node = newNode;
            }
            node->seenInPhase = true;
        }
        node->count(isCpuSample)++;
    }

    // override
    virtual void recordThread(
        int threadId,
        const string& name) {
        // Deliberately Unused
    }

    // override
    virtual void
    recordAllocation(uintptr_t allocationSize, bool outsideTlab, VMSymbol* symbol) {
        // Deliberately Unused
    }

    // override
    virtual void
    recordNotification(data::NotificationCategory category, const string &payload, int value) {
        // Deliberately Unused
    }

    // override
    virtual void recordMetricInformation(const MetricInformation& metricInformation) {
        // Deliberately Unused
    }

    // override
    virtual void recordMetricSamples(const long time_epoch_millis, const vector<MetricSample>& metricSamples) {
        // Deliberately Unused
    }

    // override
    virtual void recordAllocationTable() {
        // Deliberately Unused
    }

    // override
    virtual void recordConstantMetricsComplete() {
        // Deliberately Unused
    }

    DISALLOW_COPY_AND_ASSIGN(PrometheusQueueListener);
};

QueueListener* prometheus_queue_listener() {
    root = new ProfileNode();
    root->reset();

    return new PrometheusQueueListener();
}

// --------------------
// END Queue Listener
// --------------------
