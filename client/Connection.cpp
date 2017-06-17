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
    async_read_until(m_socket, *(buffer.get()), '\n', bind(&Connection::onClientRead,
                                                           shared_from_this(),
                                                           buffer,
                                                           _1,
                                                           _2
                                                              ));
}

void Connection::onClientRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytes_transfered) {
    cout << "onClientRead: " << er.message() << endl;
    if (!er) {
        boost::asio::streambuf::const_buffers_type bufs = buffer->data();
        const string message(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_transfered);

        cout << "Received from client: " << message << endl;
        const string payload("payload\n");
        if (message == payload) {
            cout << "Sending payload to reserve client" << endl;
            m_socket.async_send(boost::asio::buffer(payload), bind(&Connection::onClientWrite,
                                                                          shared_from_this(),
                                                                          _1));
        }
    }
}

void Connection::onClientWrite(const bs::error_code& er) {
    cout << "onClientWrite: " << er.message() << endl;
    if (!er) {
        read();
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

