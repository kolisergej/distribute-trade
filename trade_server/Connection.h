#ifndef CONNECTION_H
#define CONNECTION_H

#include "Common.h"

class Connection: public std::enable_shared_from_this<Connection> {
public:
    static shared_ptr<Connection> createConnection(io_service& service);

    void start();
    network::socket& socket();
    string region() const;

private:
    void read();
    void onDatacenterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytesTransfered);
    void onDatacenterWrite(const bs::error_code& er);

    Connection(io_service& service);
    network::socket m_socket;
    string m_region;
};

#endif // CONNECTION_H
