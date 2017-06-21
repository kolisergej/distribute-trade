#include "Connection.h"

shared_ptr<Connection> Connection::createConnection(io_service& service)
{
    shared_ptr<Connection> connection(new Connection(service));
    return connection;
}

void Connection::start() {
    read();
}

void Connection::read() {
    shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
    async_read_until(m_socket, *(buffer.get()), '\n', bind(&Connection::onDatacenterRead,
                                                           shared_from_this(),
                                                           buffer,
                                                           _1,
                                                           _2
                                                           ));
}

void Connection::onDatacenterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytesTransfered) {
    mylog(DEBUG, "onDatacenterRead:", er.message());
    if (!er) {
        boost::asio::streambuf::const_buffers_type bufs = buffer->data();
        const string message(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytesTransfered);

        mylog(DEBUG, "Received from reserve datacenter:", message);
        read();
    } else {
        mylog(ERROR, "Reserve datacenter down:", er.message());
    }
}


void Connection::onDatacenterCommandWrite(const bs::error_code& er, string command) {
    mylog(DEBUG, "onDatacenterCommandWrite", command, er.message());
    if (er) {
        mylog(ERROR, "Reserve datacenter down:", er.message());
    }
}

boost::asio::ip::tcp::socket& Connection::socket() {
    return m_socket;
}

Connection::Connection(io_service& service):
    m_socket(service)
{

}

void Connection::sendCommandToReserve(const string& command) {
    m_socket.async_send(boost::asio::buffer(command), bind(&Connection::onDatacenterCommandWrite,
                                                                       shared_from_this(),
                                                                       _1,
                                                                       command));
}
