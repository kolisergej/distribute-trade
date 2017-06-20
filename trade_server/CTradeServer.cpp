#include "CTradeServer.h"

CTradeServer::CTradeServer(size_t port):
    m_service(),
    m_acceptor(m_service, network::endpoint(network::v4(), port))
{

}

void CTradeServer::start()
{
    mylog(INFO, "I'm' server, listen", m_acceptor.local_endpoint().port());
    shared_ptr<Connection> connection = Connection::createConnection(m_service);
    m_acceptor.async_accept(connection->socket(), std::bind(&CTradeServer::handleMasterDataCenterConnection,
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

void CTradeServer::handleMasterDataCenterConnection(shared_ptr<Connection> connection, const bs::error_code& er) {
    if (!er) {
        mylog(INFO, "Handle master datacenter connection");
        {
            lock_guard<mutex> lock(m_datacenterConnectionsMutex);
            m_datacentersConnection.push_back(connection);
        }
        connection->start();
        shared_ptr<Connection> newConnection = Connection::createConnection(m_service);
        m_acceptor.async_accept(newConnection->socket(), std::bind(&CTradeServer::handleMasterDataCenterConnection,
                                                                this,
                                                                newConnection,
                                                                _1));
    }
}