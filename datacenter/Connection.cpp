#include "Connection.h"

shared_ptr<Connection> Connection::createConnection(io_service& service)
{
    shared_ptr<Connection> connection(new Connection(service));
    return connection;
}

void Connection::start() {
    m_sendReserveDatacentersTimer.expires_from_now(boost::posix_time::seconds(1));
    m_sendReserveDatacentersTimer.async_wait(std::bind(&Connection::onSendTimer, shared_from_this(), _1));
    read();
}

void Connection::read() {
    shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
    async_read_until(m_socket, *(buffer.get()), '\n', bind(&Connection::onDatacenterRead,
                                                           shared_from_this(),
                                                           _1
                                                           ));
}

void Connection::onDatacenterRead(const bs::error_code& er) {
    mylog(DEBUG, "onDatacenterRead:", er.message());
    if (!er) {
        read();
    } else {
        m_sendReserveDatacentersTimer.cancel();
    }
}

boost::asio::ip::tcp::socket& Connection::socket() {
    return m_socket;
}

Connection::Connection(io_service& service):
    m_socket(service),
    m_sendReserveDatacentersTimer(service)
{
}

void Connection::onSendTimer(const bs::error_code& er) {
    if (!er) {
        m_sendReserveDatacentersTimer.cancel();
        lock_guard<mutex> lock(m_sendReserveDatacentersMutex);
        if (m_sendReserveDatacentersCommands.empty()) {
            m_sendReserveDatacentersTimer.expires_from_now(boost::posix_time::seconds(1));
            m_sendReserveDatacentersTimer.async_wait(std::bind(&Connection::onSendTimer, shared_from_this(), _1));
            return;
        }
        const string command = m_sendReserveDatacentersCommands.front();
        m_sendReserveDatacentersCommands.pop();
        mylog(DEBUG, "Sending", command, "to reserve datacenter");
        async_write(m_socket, boost::asio::buffer(command), std::bind(&Connection::onSendTimer,
                                                                               shared_from_this(),
                                                                               _1));
    }
}

void Connection::sendCommandToReserve(const string& command) {
    lock_guard<mutex> lock(m_sendReserveDatacentersMutex);
    m_sendReserveDatacentersCommands.push(command);
}
