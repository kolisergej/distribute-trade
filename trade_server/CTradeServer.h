#ifndef CTRADESERVER_H
#define CTRADESERVER_H

#include "Connection.h"
#include "Common.h"

class CTradeServer
{
public:
    explicit CTradeServer(size_t port);
    void start();

private:
    void handleMasterDataCenterConnection(shared_ptr<Connection> connection, const bs::error_code& er);

    vector<weak_ptr<Connection>> m_datacentersConnection;
    mutex m_datacenterConnectionsMutex;

    io_service m_service;
    network::acceptor m_acceptor;
};

#endif // CTRADESERVER_H
