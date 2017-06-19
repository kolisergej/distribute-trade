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
    mylog(DEBUG, "onClientRead:", er.message());
    if (!er) {
        boost::asio::streambuf::const_buffers_type bufs = buffer->data();
        const string message(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_transfered);

        // TODO if queue not empty -> send command. (setBalance, make_trade)
        // else payload
        mylog(DEBUG, "Received from client:", message);
        const string payload("payload\n");
        if (message == payload) {
            mylog(DEBUG, "Sending payload to reserve client");
            m_socket.async_send(boost::asio::buffer(payload), bind(&Connection::onClientWrite,
                                                                          shared_from_this(),
                                                                          _1));
        }
    } else {
      mylog(ERROR, "Client down:", er.message());
    }
}

void Connection::onClientWrite(const bs::error_code& er) {
    mylog(DEBUG, "onClientWrite", er.message());
    if (!er) {
        read();
    } else {
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

void Connection::sendCommand(string&& command) {
    lock_guard<mutex> lock(m_commandsMutex);
    m_commands.push(std::move(command));
}
