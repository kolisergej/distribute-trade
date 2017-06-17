#ifndef CONNECTION_H
#define CONNECTION_H

#include "Common.h"

class Connection: public std::enable_shared_from_this<Connection> {
public:
    static shared_ptr<Connection> createConnection(io_service& service);

    void start();
    network::socket& socket();

private:
    void read();
    void onClientRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytes_transfered);
    void onClientWrite(const bs::error_code& er);

    Connection(io_service& service);
    network::socket m_socket;
};

#endif // CONNECTION_H
