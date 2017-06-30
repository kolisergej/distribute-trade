#ifndef CONNECTION_H
#define CONNECTION_H

#include "Common.h"

class Connection: public std::enable_shared_from_this<Connection> {
public:
    static shared_ptr<Connection> createConnection(io_service& service);

    void start();
    network::socket& socket();
    void pushCommandToReserve(const string& command);

private:
    void read();
    void onDatacenterRead(const shared_ptr<boost::asio::streambuf>& buffer, const bs::error_code& er);
    void pushCommandToReserve();
    void onPushCommandToReserve();
    void sendCommandToReserve();
    void onSendCommandToReserve(const std::shared_ptr<std::string>& buffer, const bs::error_code& er);

    explicit Connection(io_service& service);

    network::socket m_socket;
    queue<string> m_sendReserveDatacentersCommands;
    mutex m_sendReserveDatacentersMutex;
};

#endif // CONNECTION_H
