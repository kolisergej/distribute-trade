#ifndef WORKER_H
#define WORKER_H

#include "Common.h"
#include "Connection.h"

class CDataCenter
{
public:
    CDataCenter(int selfId, map<int, DataCenterConfigInfo>&& dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort);
    void start();

private:
    int m_selfId;
    map<int, DataCenterConfigInfo> m_dataCenters;
    const string m_region;
    int m_balance;
    bool m_isMaster;
    io_service m_service;

    void networkInit(int master_id);

    // Master client part
    network::endpoint m_serverEndpoint;
    network::acceptor m_acceptor;
    vector<weak_ptr<Connection>> m_clients_connection;
    mutex m_mutex;

    void handleClientConnection(shared_ptr<Connection> connection, const bs::error_code& er);
    void handleServerConnection(const bs::error_code& er);
    // Master client part


    // in case of master client this socket for server
    // in case of reserved client this socket for master client
    network::socket m_socket;


    // Reserve client part
    boost::asio::deadline_timer m_payloadTimer;
    void onMasterConnect(const bs::error_code& er);
    void writeMaster();
    void onWriteTimer(const bs::error_code& er);
    void onMasterWrite(const bs::error_code& er);
    void onMasterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytes_transfered);
    // Reserve client part
};

#endif // WORKER_H
