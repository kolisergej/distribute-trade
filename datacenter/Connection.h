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
    void onDatacenterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytesTransfered);
    void onDatacenterCommandWrite(const bs::error_code& er, std::string command);

    explicit Connection(io_service& service);
    network::socket m_socket;
};

#endif // CONNECTION_H
