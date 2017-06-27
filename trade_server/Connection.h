#ifndef CONNECTION_H
#define CONNECTION_H

#include "Common.h"

class CTradeServer;

class Connection: public std::enable_shared_from_this<Connection> {
public:
    static shared_ptr<Connection> createConnection(io_service& service, CTradeServer* pTradeServer);

    void start();
    network::socket& socket();

private:
    void read();
    void onRegionRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er);
    void onPushCommandToMaster();
    void sendCommandToMaster();
    void onSendCommandToMaster(std::shared_ptr<std::string> buffer, const bs::error_code& er);

    Connection(io_service& service, CTradeServer* pTradeServer);
    network::socket m_socket;
    string m_region;
    CTradeServer* m_pTradeServer;
    std::function<bool()> m_tradeLogic;
    mutex m_sendCommandsMutex;
    queue<string> m_sendCommands;
};

#endif // CONNECTION_H
