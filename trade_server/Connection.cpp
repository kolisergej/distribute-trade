#include "CTradeServer.h"

#include "Connection.h"

shared_ptr<Connection> Connection::createConnection(io_service& service, CTradeServer* pTradeServer)
{
    shared_ptr<Connection> connection(new Connection(service, pTradeServer));
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
    async_read_until(m_socket, *(buffer.get()), '\n', bind(&Connection::onRegionRead,
                                                           shared_from_this(),
                                                           buffer,
                                                           _1,
                                                           _2
                                                           ));
}

void Connection::onRegionRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytesTransfered) {
    mylog(DEBUG, "onRegionRead:", er.message());
    if (!er) {
        boost::asio::streambuf::const_buffers_type bufs = buffer->data();
        const string message(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytesTransfered);

        mylog(INFO, "Received from region:", message);
        istringstream iss(message);
        string command;
        iss >> command;
        if (command == "setRegion") {
            string region;
            iss >> region;
            m_pTradeServer->addRegionConnection(region, weak_ptr<Connection>(shared_from_this()));
        } else if (command == "makeTrade") {
            size_t bookId;
            int sum;
            iss >> bookId;
            iss >> sum;
            const string tradeResultMessage("tradeAnswer " +
                                            std::to_string(bookId) + " " +
                                            std::to_string(m_tradeLogic()) + '\n');
            m_socket.async_send(boost::asio::buffer(tradeResultMessage), bind(&Connection::onDatacenterWrite,
                                                                          shared_from_this(),
                                                                          _1));
        }
        read();
    } else {
        mylog(ERROR, "Region down when try to read:", er.message());
    }
}

void Connection::onDatacenterWrite(const bs::error_code& er) {
    if (!er) {
        mylog(DEBUG, "Answered to datacenter");
    }
}


void Connection::onRegionWrite(const bs::error_code& er) {
    mylog(DEBUG, "onRegionWrite", er.message());
    if (er) {
        mylog(ERROR, "Region down when try to write:", er.message());
    }
}

boost::asio::ip::tcp::socket& Connection::socket() {
    return m_socket;
}

Connection::Connection(io_service& service, CTradeServer* pTradeServer):
    m_socket(service),
    m_pTradeServer(pTradeServer),
    m_tradeLogic(std::bind(std::uniform_int_distribution<>(0,1), std::default_random_engine()))
{

}
