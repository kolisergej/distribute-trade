#include "CTradeServer.h"

#include "Connection.h"

shared_ptr<Connection> Connection::createConnection(io_service& service, CTradeServer* pTradeServer)
{
    shared_ptr<Connection> connection(new Connection(service, pTradeServer));
    return connection;
}

void Connection::start() {
    m_sendCommandsTimer.expires_from_now(boost::posix_time::seconds(1));
    m_sendCommandsTimer.async_wait(std::bind(&Connection::onSendTimer, shared_from_this(), _1));
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
                                                           _1
                                                           ));
}

void Connection::onRegionRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er) {
    mylog(DEBUG, "onRegionRead:", er.message());
    if (!er) {
        std::istream is(&(*buffer.get()));
        string message;
        while (std::getline(is, message)) {
            mylog(INFO, "Received from region:", message);
            istringstream iss(message);
            string command;
            iss >> command;
            if (command == "setRegion") {
                string region;
                iss >> region;
                m_pTradeServer->addRegionConnection(region, weak_ptr<Connection>(shared_from_this()));
                {
                    lock_guard<mutex> lock(m_sendCommandsMutex);
                    auto activeTransactions = m_pTradeServer->getTransactionsForRegion(m_region);
                    for (auto& transaction: activeTransactions) {
                        mylog(INFO, "Send active transaction:",
                              m_region,
                              transaction.first,
                              transaction.second.first,
                              transaction.second.second);
                        m_sendCommands.push(string("tradeAnswer " +
                                                   std::to_string(transaction.first) + " " +
                                                   std::to_string(transaction.second.first) + " " +
                                                   std::to_string(transaction.second.second) +'\n'));
                    }
                }
            } else if (command == "makeTrade") {
                size_t transactionId;
                int sum;
                iss >> transactionId;
                iss >> sum;
                const bool succeed = m_tradeLogic();
                const string tradeResultMessage("tradeAnswer " +
                                                std::to_string(transactionId) + " " +
                                                std::to_string(sum) + " " +
                                                std::to_string(succeed) + '\n');
                {
                    lock_guard<mutex> lock(m_sendCommandsMutex);
                    m_sendCommands.push(tradeResultMessage);
                }
                m_pTradeServer->addActiveTransaction(m_region, transactionId, sum, succeed);
            } else if (command == "processed") {
                size_t transactionId;
                iss >> transactionId;
                m_pTradeServer->transactionCompleted(m_region, transactionId);
            }
        }
        read();
    } else {

        mylog(ERROR, "Region down:", er.message());
    }
}

boost::asio::ip::tcp::socket& Connection::socket() {
    return m_socket;
}

Connection::Connection(io_service& service, CTradeServer* pTradeServer):
    m_socket(service),
    m_pTradeServer(pTradeServer),
    m_tradeLogic(std::bind(std::uniform_int_distribution<>(0,1), std::default_random_engine())),
    m_sendCommandsTimer(service)
{
}

void Connection::onSendTimer(const bs::error_code& er) {
    if (!er) {
        m_sendCommandsTimer.cancel();
        lock_guard<mutex> lock(m_sendCommandsMutex);
        if (m_sendCommands.empty()) {
            m_sendCommandsTimer.expires_from_now(boost::posix_time::seconds(1));
            m_sendCommandsTimer.async_wait(std::bind(&Connection::onSendTimer, shared_from_this(), _1));
            return;
        }
        const string command = m_sendCommands.front();
        m_sendCommands.pop();
        async_write(m_socket, boost::asio::buffer(command), std::bind(&Connection::onSendTimer,
                                                                      shared_from_this(),
                                                                      _1));
    }
}
