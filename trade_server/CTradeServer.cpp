#include "CTradeServer.h"

CTradeServer::CTradeServer(size_t port):
    m_service(),
    m_acceptor(m_service, network::endpoint(network::v4(), port))
{

}

void CTradeServer::start()
{
    mylog(INFO, "I'm server, listen", m_acceptor.local_endpoint().port());
    shared_ptr<Connection> connection = Connection::createConnection(m_service, this);
    m_acceptor.async_accept(connection->socket(), std::bind(&CTradeServer::handleRegionConnection,
                                                            this,
                                                            connection,
                                                            _1));

    vector<thread> thread_group;
    for (size_t i = 0; i < g_machineCoreCount; ++i) {
        thread_group.push_back(thread([this] {
            setThreadAfinity();
            m_service.run();
        }));
    }

    for (auto& thread: thread_group) {
        thread.join();
    }
}

void CTradeServer::handleRegionConnection(const shared_ptr<Connection>& connection, const bs::error_code& er) {
    if (!er) {
        mylog(INFO, "Handle region connection");
        connection->start();
        shared_ptr<Connection> newConnection = Connection::createConnection(m_service, this);
        m_acceptor.async_accept(newConnection->socket(), std::bind(&CTradeServer::handleRegionConnection,
                                                                this,
                                                                newConnection,
                                                                _1));
    }
}

void CTradeServer::addRegionConnection(const string& region, const weak_ptr<Connection>& connection) {
    lock_guard<mutex> lock(m_regionConnectionsMutex);
    m_regionConnection[region] = connection;
}

void CTradeServer::addActiveTransaction(const string& region, int transactionId, int sum, bool succeed) {
    lock_guard<mutex> lock(m_regionActiveTransactionsMutex);
    m_regionActiveTransactions[region][transactionId] = std::make_pair(sum, succeed);
}

void CTradeServer::transactionCompleted(const string& region, int transactionId) {
    lock_guard<mutex> lock(m_regionActiveTransactionsMutex);
    m_regionActiveTransactions[region].erase(transactionId);
}

std::unordered_map<int, std::pair<int, bool> > CTradeServer::getTransactionsForRegion(const string& region) const {
    lock_guard<mutex> lock(m_regionActiveTransactionsMutex);
    if (m_regionActiveTransactions.find(region) != m_regionActiveTransactions.end()) {
        return m_regionActiveTransactions.at(region);
    }
    return std::unordered_map<int, std::pair<int, bool> >();
}
