#include "Connection.h"

shared_ptr<Connection> Connection::createConnection(io_service& service, boost::asio::strand& strand)
{
    shared_ptr<Connection> connection(new Connection(service, strand));
    return connection;
}

void Connection::start() {
    read();
}

void Connection::read() {
    shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
    async_read_until(m_socket, *buffer, '\n', bind(&Connection::onDatacenterRead,
                                                   shared_from_this(),
                                                   buffer,
                                                   _1
                                                   ));
}

void Connection::onDatacenterRead(const shared_ptr<boost::asio::streambuf>& buffer, const bs::error_code& er) {
    mylog(DEBUG, "onDatacenterRead:", er.message());
    if (!er) {
        // Need this callback for handle reserve datacenter disconnect
        // But in case of reverse communication you can process here its' messages
        (void) buffer;
        read();
    }
}

boost::asio::ip::tcp::socket& Connection::socket() {
    return m_socket;
}

Connection::Connection(io_service& service, boost::asio::strand& strand):
    m_socket(service),
    m_strand(strand)
{
}

void Connection::pushCommandToReserve(const string& command) {
    const bool empty = m_sendReserveDatacentersCommands.empty();
    m_sendReserveDatacentersCommands.push(command);
    if (empty) {
        m_strand.dispatch(bind(&Connection::sendCommandToReserve, shared_from_this()));
    }
}

void Connection::sendCommandToReserve() {
    const string& command = m_sendReserveDatacentersCommands.front();
    std::shared_ptr<string> reserveDatacanterWriteBuffer = std::make_shared<string>(command);
    mylog(DEBUG, "Sending", command, "to reserve datacenter");
    m_sendReserveDatacentersCommands.pop();
    async_write(m_socket, boost::asio::buffer(*reserveDatacanterWriteBuffer), m_strand.wrap(bind(&Connection::onSendCommandToReserve,
                                                                                                 shared_from_this(),
                                                                                                 reserveDatacanterWriteBuffer,
                                                                                                 _1)));
}

void Connection::onSendCommandToReserve(const std::shared_ptr<string>& buffer, const bs::error_code& er) {
    (void)buffer;
    if (!er) {
        if (m_sendReserveDatacentersCommands.empty()) {
            return;
        }
        sendCommandToReserve();
    } else {
        mylog(DEBUG, "onSendCommandToReserve error:", er.message());
    }
}
