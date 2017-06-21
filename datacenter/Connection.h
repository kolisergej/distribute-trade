#ifndef CONNECTION_H
#define CONNECTION_H

#include "Common.h"

class Connection: public std::enable_shared_from_this<Connection> {
public:
    static shared_ptr<Connection> createConnection(io_service& service);

    void start();
    network::socket& socket();
    void sendCommandToReserve(const string& command);

private:
    void read();
    void onDatacenterRead(const bs::error_code& er);
    void onDatacenterCommandWrite(const bs::error_code& er, std::string command);
    void onSendTimer(const bs::error_code& er);

    explicit Connection(io_service& service);
    network::socket m_socket;
    queue<string> m_sendReserveDatacentersCommands;
    boost::asio::deadline_timer m_sendReserveDatacentersTimer;
    mutex m_sendReserveDatacentersMutex;
};

#endif // CONNECTION_H
