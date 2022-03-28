#ifndef NETWORK_H
#define NETWORK_H

#include "globals.h"
#include "debug_logger.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <initializer_list>
#include <iostream>
#include <vector>

#include "data.pb.h"
#include <boost/asio/steady_timer.hpp>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
using asio::ip::tcp;
using boost::system::error_code;
using asio::steady_timer;
using std::vector;

typedef ssl::stream<tcp::socket> ssl_socket;

asio::io_service& getIos();

void network_prepare_fork();
void network_parent_fork();
void network_child_fork();

class Network {
public:

    explicit Network(
        const std::string& host,
        const std::string& port,
        const std::string& customCertificateFile,
        DebugLogger& debugLogger,
        const bool onPremHost,
        const bool prometheusEnabled);

    bool sendWithSize(
        CollectorController& controller,
        data::AgentEnvelope& agentEnvelope);

    bool connect();

    bool isConnected();

    ssl_socket& sock();

    bool poll();

    void close();

    static void logNetError(
        const error_code &ec,
        const std::initializer_list<const char *> message,
        DebugLogger& debugLogger);

    void on_fork();

    ~Network();

private:
    void setupTimeout(error_code& ec, steady_timer& timer, bool doClose);

    void onTimerRun(error_code& ec, bool doClose, const error_code& error);

    void closeSocket();

    ssl::context ctx;

    bool isConnected_;

    // designed to stop timer triggered messages (the heartbeat) from accidentally poll based messages
    bool isSending_;

    ssl_socket* sock_;

    const std::string& host_;

    const std::string& port_;

    const bool onPremHost;

    DebugLogger& debugLogger_;

    const bool prometheusEnabled_;

    DISALLOW_COPY_AND_ASSIGN(Network);
};

#endif // NETWORK_H
