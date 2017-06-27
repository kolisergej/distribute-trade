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

void Connection::read() {
    shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
    async_read_until(m_socket, *buffer, '\n', bind(&Connection::onRegionRead,
                                                   shared_from_this(),
                                                   buffer,
                                                   _1
                                                   ));
}

void Connection::onRegionRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er) {
    mylog(DEBUG, "onRegionRead:", er.message());
    if (!er) {
        std::istream is(&(*buffer));
        string message;
        while (std::getline(is, message)) {
            mylog(INFO, "Received from region:", message);
            istringstream iss(message);
            string command;
            iss >> command;
            if (command == "setRegion") {
                iss >> m_region;
                m_pTradeServer->addRegionConnection(m_region, weak_ptr<Connection>(shared_from_this()));
                {
                    lock_guard<mutex> lock(m_sendCommandsMutex);
                    auto activeTransactions = m_pTradeServer->getTransactionsForRegion(m_region);
                    for (auto& transaction: activeTransactions) {
                        mylog(INFO, "Send active transaction:",
                              m_region,
                              transaction.first,
                              transaction.second.first,
                              transaction.second.second);
                        const bool empty = m_sendCommands.empty();
                        m_sendCommands.push(string("tradeAnswer " +
                                                   std::to_string(transaction.first) + " " +
                                                   std::to_string(transaction.second.first) + " " +
                                                   std::to_string(transaction.second.second) +'\n'));
                        if (empty) {
                            m_socket.get_io_service().post(bind(&Connection::onPushCommandToMaster, shared_from_this()));
                        }
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
                    const bool empty = m_sendCommands.empty();
                    m_sendCommands.push(tradeResultMessage);
                    if (empty) {
                        m_socket.get_io_service().post(bind(&Connection::onPushCommandToMaster, shared_from_this()));
                    }
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
    m_tradeLogic(std::bind(std::uniform_int_distribution<>(0,1), std::default_random_engine()))
{
}

void Connection::onPushCommandToMaster() {
    lock_guard<mutex> lock(m_sendCommandsMutex);
    sendCommandToMaster();
}

void Connection::sendCommandToMaster() {
    const string& command = m_sendCommands.front();
    std::shared_ptr<string> masterDatacanterWriteBuffer = std::make_shared<string>(command);
    mylog(DEBUG, "Sending", command, "to master datacenter");
    m_sendCommands.pop();
    async_write(m_socket, boost::asio::buffer(*masterDatacanterWriteBuffer), bind(&Connection::onSendCommandToMaster,
                                                                                  shared_from_this(),
                                                                                  masterDatacanterWriteBuffer,
                                                                                  _1));
}

void Connection::onSendCommandToMaster(std::shared_ptr<string> buffer, const bs::error_code& er) {
    (void)buffer;
    if (!er) {
        lock_guard<mutex> lock(m_sendCommandsMutex);
        if (m_sendCommands.empty()) {
            return;
        }
        sendCommandToMaster();
    } else {
        mylog(DEBUG, "onSendCommandToMaster error:", er.message());
    }
}
