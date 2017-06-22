#ifndef CDATACENTER_H
#define CDATACENTER_H

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
    int m_balance;
    mutex m_balanceMutex;
    size_t m_lastTransactionId;
    map<size_t, int> m_transactionIdSum;

    int m_masterId;
    io_service m_service;

    void networkInit();
    int tradeAnswerProcess(istringstream& iss);


////////////////////////****** Master part ******////////////////////////

    network::endpoint m_serverEndpoint;
    network::acceptor m_acceptor;
    boost::asio::deadline_timer m_serverReconnectTimer;
    boost::asio::deadline_timer m_datacentersConnectionsCheckTimer;
    boost::asio::deadline_timer m_sendServerCommandsTimer;
    vector<weak_ptr<Connection>> m_datacentersConnection;
    mutex m_datacenterConnectionsMutex;
    mutex m_serverCommandsMutex;
    queue<string> m_serverCommands;
    bool m_connectedToServer;

    void handleReserveDatacenterConnection(shared_ptr<Connection> connection, const bs::error_code& er);
    void handleServerConnection(const bs::error_code& er);
    void onServerReconnect(const bs::error_code& er);

    void serverRead();
    void sendServerMessage(string&& message);
    void onSendTimer(const bs::error_code& er);
    void onServerRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er);
    void onConnectionsCheckTimer(const bs::error_code& er);

////////////////////////////////////////////////////////////////////////


    // in case of master datacenter this socket for server
    // in case of reserved datacenter this socket for master datacenter
    std::unique_ptr<network::socket> m_socket;


////////////////////////****** Reserve part ******////////////////////////

    void onMasterConnect(const bs::error_code& er);
    void readMaster();
    void onMasterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er);
    void connectNextMaster();
    void sendReserveDatacentersCommand(const string& command);

////////////////////////////////////////////////////////////////////////

};

#endif // CDATACENTER_H
