#include "Connection.h"

shared_ptr<Connection> Connection::createConnection(io_service& service)
{
    shared_ptr<Connection> connection(new Connection(service));
    return connection;
}

void Connection::start() {
    read();
}

string Connection::region() const {
    return m_region;
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
        mylog(DEBUG, "Received from datacenter:", message);

    } else {
        mylog(ERROR, "Client down:", er.message());
    }
}


void Connection::onDatacenterWrite(const bs::error_code& er) {
    mylog(DEBUG, "onClientAnswerWrite", er.message());
    if (er) {
        mylog(ERROR, "Client down:", er.message());
    }
}

boost::asio::ip::tcp::socket& Connection::socket()
{
    return m_socket;
}

Connection::Connection(io_service& service):
    m_socket(service)
{

}
