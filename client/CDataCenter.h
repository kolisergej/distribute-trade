#ifndef WORKER_H
#define WORKER_H

#include "Common.h"
#include "Connection.h"

class CDataCenter
{
public:
    CDataCenter(int selfId, map<int, DataCenterConfigInfo>&& dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort);
    void start();

////////////////////////****** UI commands ******////////////////////////
    void changeBalance(int sum);
    void makeTrade(int sum);

/////////////////////////////////////////////////////////////////////////

private:
    const int m_selfId;
    map<int, DataCenterConfigInfo> m_dataCenters;
    const string m_region;
//    Make balance atomic for UI commands
    std::atomic<int> m_balance;

    int m_masterId;
    io_service m_service;

    void networkInit();


////////////////////////****** Master part ******////////////////////////

    network::endpoint m_serverEndpoint;
    network::acceptor m_acceptor;
    boost::asio::deadline_timer m_serverReconnectTimer;
    vector<weak_ptr<Connection>> m_clientsConnection;
    mutex m_connectionMutex;
    void handleClientConnection(shared_ptr<Connection> connection, const bs::error_code& er);
    void handleServerConnection(const bs::error_code& er);
    void onServerReconnect(const bs::error_code& er);

////////////////////////////////////////////////////////////////////////


    // in case of master client this socket for server
    // in case of reserved client this socket for master client
    std::unique_ptr<network::socket> m_socket;


////////////////////////****** Reserve part ******////////////////////////

    boost::asio::deadline_timer m_payloadTimer;
    void onMasterConnect(const bs::error_code& er);
    void writePayloadToMaster();
    void onWriteTimer(const bs::error_code& er);
    void onMasterPayloadWrite(const bs::error_code& er);
    void onMasterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytes_transfered);

    void connectNextMaster();

////////////////////////////////////////////////////////////////////////
};

#endif // WORKER_H
