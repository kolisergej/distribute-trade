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
    void addActiveTransaction(const string& region, int transactionId, int sum, bool succeed);
    void transactionCompleted(const string& region, int transactionId);
    std::unordered_map<int, std::pair<int, bool> > getTransactionsForRegion(const string& region) const;

private:
    void handleRegionConnection(shared_ptr<Connection> connection, const bs::error_code& er);

    // <Region <---> <TransactionId <---> <Sum, Succeed> > >
    std::unordered_map<string, std::unordered_map<int, std::pair<int, bool>>> m_regionActiveTransactions;
    std::unordered_map<string, weak_ptr<Connection>> m_regionConnection;
    mutex m_regionConnectionsMutex;
    mutable mutex m_regionActiveTransactionsMutex;


    io_service m_service;
    network::acceptor m_acceptor;
};

#endif // CTRADESERVER_H
