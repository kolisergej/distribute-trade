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

private:
    void handleRegionConnection(shared_ptr<Connection> connection, const bs::error_code& er);

    map<string, weak_ptr<Connection>> m_regionConnection;
    mutex m_regionConnectionsMutex;


    io_service m_service;
    network::acceptor m_acceptor;
};

#endif // CTRADESERVER_H
