#include "prometheus_exporter.h"
#include "network.h"
#include <boost/asio.hpp>

namespace errc = boost::system::errc;
namespace asio = boost::asio;
namespace error = asio::error;

using std::string;
using asio::ip::tcp;

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

const int max_length = 1024;

tcp::acceptor* acceptor_ = NULL;
tcp::socket* socket_ = NULL;
DebugLogger* debugLogger_ = NULL;

// TODO: setup the sample rate from commandline parameters not from remote server
// TODO: hook to receive data and put it into a data structure

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

                boost::system::error_code write_ec;
                #define write(data) asio::write(socket_, asio::buffer(data), asio::transfer_all(), write_ec)
                if (is_metrics) {
                    // Example promfiler response
                    // promfiler_cpu_profile{signature="(root)#parserOnHeadersComplete"} 1
                    // promfiler_cpu_profile{signature="(root)#parserOnHeadersComplete#parserOnIncoming"} 1
                    write(prefix_200);
                    write("promfiler_cpu_profile{signature=\"(root)#parserOnHeadersComplete\"} 1\n");
                } else {
                    write(response_404);
                }

                if (write_ec) {
                    Network::logNetError(ec, {"Prometheus write reply error"}, *debugLogger_);
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

bool init_prometheus(const int port, DebugLogger& debugLogger) {
    if (acceptor_ != NULL) {
        debugLogger << "failed to init_prometheus" << endl;
        return false;
    }

    debugLogger_ = &debugLogger;
    asio::io_service &ios = getIos();
    acceptor_ = new tcp::acceptor(ios, tcp::endpoint(tcp::v4(), port));
    socket_ = new tcp::socket(ios);
    do_accept();

    debugLogger << "init_prometheus on " << port << endl;

    return true;
}
