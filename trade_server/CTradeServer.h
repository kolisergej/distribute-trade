#ifndef CTRADESERVER_H
#define CTRADESERVER_H

#include "Connection.h"
#include "Common.h"

class CTradeServer
{
public:
    explicit CTradeServer(size_t port);
    void start();
    void addRegionConnection(const string& region, weak_ptr<Connection> connection);
    void addActiveTransaction(const string& region, int transactionId, bool succeed);
    void transactionCompleted(const string& region, int transactionId);
    std::unordered_map<int, bool> getTransactionsForRegion(const string& region) const;

private:
    void handleRegionConnection(shared_ptr<Connection> connection, const bs::error_code& er);

    std::unordered_map<string, std::unordered_map<int, bool>> m_regionActiveTransactions;
    std::unordered_map<string, weak_ptr<Connection>> m_regionConnection;
    mutex m_regionConnectionsMutex;
    mutable mutex m_regionActiveTransactionsMutex;


    io_service m_service;
    network::acceptor m_acceptor;
};

#endif // CTRADESERVER_H
