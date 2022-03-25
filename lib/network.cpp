#include "network.h"

#include "collector_controller.h"
#include "prometheus_exporter.h"

#include <boost/chrono.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/foreach.hpp>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

using google::protobuf::util::SerializeDelimitedToZeroCopyStream;
using google::protobuf::io::ArrayOutputStream;

using boost::chrono::milliseconds;
using boost::lambda::var;
namespace errc = boost::system::errc;

const int CONNECT_TIMEOUT_IN_MS = 1000;

asio::io_service* ios = new asio::io_service();

asio::io_service& getIos() {
    return *ios;
}

void network_prepare_fork() {
    ios->notify_fork(asio::io_service::fork_prepare);
}

void network_parent_fork() {
    ios->notify_fork(asio::io_service::fork_parent);
}

void network_child_fork() {
    ios->notify_fork(asio::io_service::fork_child);
}

Network::Network(
    const std::string &host,
    const std::string &port,
    const std::string& customCertificateFile,
    DebugLogger &debugLogger,
    const bool onPremHost,
    const bool prometheusEnabled,
    const int prometheusPort,
    const std::string& prometheusHost)
    : ctx(ssl::context::sslv23),
      isConnected_(false),
      isSending_(false),
      sock_(NULL),
      host_(host),
      port_(port),
      onPremHost(onPremHost),
      debugLogger_(debugLogger),
      prometheusEnabled_(prometheusEnabled) {

    if (prometheusEnabled) {
        bind_prometheus(prometheusPort, prometheusHost, debugLogger);
    } else {
        // Use our custom certs
        error_code ec;
        ctx.add_certificate_authority(asio::buffer(CERTIFICATE_1.data(), CERTIFICATE_1.size()), ec);
        if (ec) {
            logNetError(ec, { "Error setting certificate 1" }, debugLogger_);
        }

        ctx.add_certificate_authority(asio::buffer(CERTIFICATE_2.data(), CERTIFICATE_2.size()), ec);
        if (ec) {
            logNetError(ec, { "Error setting certificate 2" }, debugLogger_);
        }

        if (!customCertificateFile.empty()) {
            ctx.load_verify_file(customCertificateFile, ec);
            if (ec) {
                logNetError(ec, {"Error setting custom certificate file, please review the customCertificateFile option"}, debugLogger_);
            }
        }
    }
}

void Network::logNetError(
    const error_code &ec,
    const std::initializer_list<const char *> message,
    DebugLogger& debugLogger) {

    *ERROR_FILE << "Opsian ("
                << GIT_STR
                << ") @ ";

    putFormattedTime(*ERROR_FILE);

    *ERROR_FILE << " Err: "
                << ec.message()
                << ". " ;

    debugLogger << "Err: "
                << ec.message()
                << ", "
                << ec.value()
                << ". " ;

    // Cannot use foreach, GCC 4.4
    for (auto it = message.begin(); it != message.end(); ++it) {
	    auto elem = *it;

        *ERROR_FILE << elem;
        debugLogger << elem;
    }

    *ERROR_FILE << std::endl;
    debugLogger << endl;
}

void awaitCompletionOrTimeout(
    error_code& ec,
    steady_timer& timer) {

    while (ec == asio::error::would_block) {
        // reset used here to avoid the stop() call from breaking ios 
        if (ios->stopped()) {
            ios->reset();
        }
        ios->run_one();
    }

    timer.cancel();

    // Try to stop the io service to avoid the issue noted in https://redmine.named-data.net/issues/2534
    ios->stop();
    ios->reset();
}

void Network::setupTimeout(
    error_code& ec,
    steady_timer& timer,
    bool doClose) {

    timer.expires_from_now(milliseconds(CONNECT_TIMEOUT_IN_MS));
    timer.async_wait(
        boost::bind(
            &Network::onTimerRun,
            this,
            ec,
            doClose,
            asio::placeholders::error));
}

void Network::onTimerRun(error_code& ec, bool doClose, const error_code& error) {
    // if the timer hasn't been cancelled, it must be a timeout
    if (error != asio::error::operation_aborted) {
        ec = asio::error::operation_aborted;
        if (doClose) {
            // don't drain or delete the socket here because we are in the
            // middle of an io_service run_one call.
            closeSocket();
        }
    }
}

// Use an async connect and a run loop in order to connect with a timeout
bool Network::connect() {
    if (ios->stopped()) {
        ios->reset();
    }

    if (prometheusEnabled_) {
        // Don't connect to a server if we're in prometheus exporter mode
        return true;
    }

    tcp::resolver::query query(host_, port_);
    tcp::resolver resolver(*ios);
    debugLogger_ << "Attempt DNS resolution" << endl;
    
    tcp::resolver::iterator iter;
    error_code resolveErrorCode = asio::error::would_block;
    steady_timer resolveTimer(*ios);
    setupTimeout(resolveErrorCode, resolveTimer, false);

    resolver.async_resolve(query, (var(resolveErrorCode) = boost::lambda::_1, var(iter) = boost::lambda::_2));
    debugLogger_ << "Starting DNS resolution" << endl;

    awaitCompletionOrTimeout(resolveErrorCode, resolveTimer);

    if (resolveErrorCode) {
        logNetError(resolveErrorCode, {"Failed to resolve server to connect to: ", host_.c_str()}, debugLogger_);
        resolver.cancel();
        ios->run_one();
        close();
        return false;
    }

    debugLogger_ << "connecting to " << host_ << endl;

    sock_ = new ssl_socket(*ios, ctx);

    // Connect to the TCP socket
    error_code connectErrorCode = asio::error::would_block;
    steady_timer connectTimer(*ios);
    setupTimeout(connectErrorCode, connectTimer, true);

    auto& lowestLayerSock = sock_->lowest_layer();
    asio::async_connect(lowestLayerSock, iter, var(connectErrorCode) = boost::lambda::_1);
    lowestLayerSock.set_option(tcp::no_delay(true));

    awaitCompletionOrTimeout(connectErrorCode, connectTimer);

    if (connectErrorCode || !lowestLayerSock.is_open()) {
        logNetError(connectErrorCode, {"Failed to connect to server: ", host_.c_str()}, debugLogger_);
        close();
        return false;
    }

    // Perform the SSL handshake
    error_code sslErrorCode;
    sock_->set_verify_mode(ssl::verify_peer);
    if (!onPremHost) {
        sock_->set_verify_callback(ssl::rfc2818_verification(host_));
    }
    sock_->handshake(ssl_socket::client, sslErrorCode);

    if (sslErrorCode || !lowestLayerSock.is_open()) {
        logNetError(sslErrorCode, {"Failed TLS handshake with: ", host_.c_str()}, debugLogger_);
        close();
        return false;
    }

    isConnected_ = true;

    return true;
}

bool Network::sendWithSize(
    CollectorController& controller,
    data::AgentEnvelope& agentEnvelope) {
    // don't send anything if you haven't connected
    if (isConnected()) {
        if (isSending_) {
            debugLogger_ << "blocked sendWithSize " << agentEnvelope.downstream_message_type_case() << endl;
            return false;
        }

        isSending_ = true;
        const int size = agentEnvelope.ByteSize();

        const int maxSize = MAX_HEADER_SIZE + size;
        vector<char> buffer(maxSize);

        // std::cout << agentEnvelope->DebugString() << std::endl;

        ArrayOutputStream aos(buffer.data(), maxSize);
        if (SerializeDelimitedToZeroCopyStream(agentEnvelope, &aos)) {

            error_code ec = asio::error::would_block;
            steady_timer writeTimer(*ios);
            setupTimeout(ec, writeTimer, true);

            // Important to take the byte count from aos, as the header is
            // a varint and thus may not take MAX_HEADER_SIZE bytes
            asio::async_write(
                    *sock_,
                    asio::buffer(buffer.data(), aos.ByteCount()),
                    var(ec) = boost::lambda::_1);

            debugLogger_ << "start awaitCompletionOrTimeout " << agentEnvelope.downstream_message_type_case() << endl;
            awaitCompletionOrTimeout(ec, writeTimer);
            debugLogger_ << "end awaitCompletionOrTimeout" << endl;

            if (ec) {
                logNetError(ec, {"Failed to send message"}, debugLogger_);
                switch (ec.value()) {
                    case errc::connection_reset:
                    case errc::broken_pipe:
                    case errc::io_error:
                    case errc::network_down:
                    case errc::network_reset:
                    case errc::network_unreachable:
                        close();
                        controller.onDisconnect();
                        break;

                    default:
                        // Don't detect a disconnect
                        break;
                }
            }

            isSending_ = false;
            return ec.value() == 0;
        } else {
            logError("Failed to serialize message\n");
        }
    }

    isSending_ = false;
    return false;
}

const int MAX_POLLS = 10;

bool Network::poll() {
    error_code ec;
    size_t work = 0;
    for (int i = 0; i < MAX_POLLS;i++) {
        ios->reset();
        work += ios->poll_one(ec);
        if (ec) {
            logNetError(ec, {"Poll error"}, debugLogger_);
            break;
        }
    }
    return work > 0;
}

bool Network::isConnected() {
    return isConnected_;
}

// sock is exposed to avoid figuring out crack boost templates
ssl_socket& Network::sock() {
    return *sock_;
}

void Network::close() {
    closeSocket();

    if (sock_ != NULL) {
        // we drain the outstanding async operations until they are all
        // cancelled otherwise when we delete we may get a use-after-free on the socket
        size_t work;
        do {
            ios->reset();
            error_code ec;
            work = ios->poll(ec);
            if (ec) {
                logNetError(ec, {"Poll error"}, debugLogger_);
            }
        } while (work > 0);

        delete sock_;
        sock_ = NULL;
    }
}

void Network::closeSocket() {
    if (sock_ != NULL) {
        error_code ignored;

        auto& lowestLayerSock = sock_->lowest_layer();
        lowestLayerSock.cancel(ignored);
        sock_->shutdown(ignored);
        lowestLayerSock.close(ignored);
        isConnected_ = false;
    }
}

void Network::on_fork() {
    close();
}

Network::~Network() = default;
