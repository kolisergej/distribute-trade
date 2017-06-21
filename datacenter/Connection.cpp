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

        {
            lock_guard<mutex> lock(m_commandsMutex);
            while(!m_commandsForReserve.empty()) {
                const string command = m_commandsForReserve.front();
                mylog(DEBUG, "Sending", command, "to reserve datacenter");
                m_socket.async_send(boost::asio::buffer(command), bind(&Connection::onDatacenterCommandWrite,
                                                                       shared_from_this(),
                                                                       _1));
                m_commandsForReserve.pop();
            }
        }
        mylog(DEBUG, "Received from reserve datacenter:", message);
        if (message == "payload\n") {
            mylog(DEBUG, "Sending payload to reserve datacenter");
            m_socket.async_send(boost::asio::buffer(message), bind(&Connection::onDatacenterPayloadWrite,
                                                                   shared_from_this(),
                                                                   _1));
        }
    } else {
        mylog(ERROR, "Reserve datacenter down:", er.message());
    }
}

void Connection::onDatacenterPayloadWrite(const bs::error_code& er) {
    mylog(DEBUG, "onDatacenterPayloadWrite", er.message());
    if (!er) {
        read();
    } else {
        mylog(ERROR, "Reserve datacenter down:", er.message());
    }
}

void Connection::onDatacenterCommandWrite(const bs::error_code& er) {
    mylog(DEBUG, "onDatacenterCommandWrite", er.message());
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
    lock_guard<mutex> lock(m_commandsMutex);
    m_commandsForReserve.push(command);
}
