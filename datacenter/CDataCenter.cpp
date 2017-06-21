#include "CDataCenter.h"
#include "Connection.h"

CDataCenter::CDataCenter(int selfId, map<int, DataCenterConfigInfo>&& dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort):
    m_selfId(selfId),
    m_dataCenters(std::move(dataCenters)),
    m_region(std::move(region)),
    m_balance(balance),
    m_lastTransactionId(0),
    m_service(),
    m_serverEndpoint(boost::asio::ip::address::from_string(serverAddress), serverPort),
    m_acceptor(m_service, network::endpoint(network::v4(), m_dataCenters[m_selfId].m_port)),
    m_serverReconnectTimer(m_service),
    m_datacentersConnectionsCheckTimer(m_service),
    m_sendServerCommandsTimer(m_service),
    m_connectedToServer(false),
    m_socket(make_unique<network::socket>(m_service))
{
    const auto masterDataCenter = std::find_if(m_dataCenters.begin(), m_dataCenters.end(), [](const auto& pair) {
        return pair.second.m_isMaster;
    });
    m_masterId = masterDataCenter->first;
}

void CDataCenter::start() {
    networkInit();

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

void CDataCenter::networkInit() {
    mylog(INFO, "master id:", m_masterId);
    const auto& masterNode = m_dataCenters[m_masterId];
    if (m_masterId == m_selfId) {
        mylog(INFO, "I'm master datacenter, listen", masterNode.m_port);
        shared_ptr<Connection> connection = Connection::createConnection(m_service);
        m_acceptor.async_accept(connection->socket(), std::bind(&CDataCenter::handleReserveDatacenterConnection,
                                                                this,
                                                                connection,
                                                                _1));
        m_socket->async_connect(m_serverEndpoint, std::bind(&CDataCenter::handleServerConnection,
                                                            this,
                                                            _1));

        m_datacentersConnectionsCheckTimer.expires_from_now(boost::posix_time::seconds(1));
        m_datacentersConnectionsCheckTimer.async_wait(std::bind(&CDataCenter::onConnectionsCheckTimer, this, _1));
        m_sendServerCommandsTimer.expires_from_now(boost::posix_time::seconds(1));
        m_sendServerCommandsTimer.async_wait(std::bind(&CDataCenter::onSendTimer, this, _1));
    } else {
        mylog(INFO, "I'm reserve datacenter, connecting to master", masterNode.m_address, ':', masterNode.m_port);
        const network::endpoint ep(boost::asio::ip::address::from_string(masterNode.m_address), masterNode.m_port);
        m_socket->async_connect(ep, std::bind(&CDataCenter::onMasterConnect,
                                              this,
                                              _1));
    }
}


////////////////////////****** Master part ******////////////////////////

void CDataCenter::handleReserveDatacenterConnection(shared_ptr<Connection> connection, const bs::error_code& er) {
    if (!er) {
        mylog(INFO, "Handle reserve datacenter connection");
        {
            lock_guard<mutex> lock(m_datacenterConnectionsMutex);
            m_datacentersConnection.push_back(connection);
        }
        connection->start();
        shared_ptr<Connection> newConnection = Connection::createConnection(m_service);
        m_acceptor.async_accept(newConnection->socket(), std::bind(&CDataCenter::handleReserveDatacenterConnection,
                                                                   this,
                                                                   newConnection,
                                                                   _1));
    }
}

void CDataCenter::handleServerConnection(const bs::error_code& er) {
    if (!er) {
        m_connectedToServer = true;
        mylog(INFO, "Handle server connection");
        serverRead();
        string initServerMessage("setRegion " + m_region + '\n');
        sendServerMessage(std::move(initServerMessage));
    } else {
        m_connectedToServer = false;
        m_socket = make_unique<network::socket>(m_service);
        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));
    }
}

void CDataCenter::serverRead() {
    shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
    async_read_until(*(m_socket.get()), *(buffer.get()), '\n', bind(&CDataCenter::onServerRead,
                                                                    this,
                                                                    buffer,
                                                                    _1
                                                                    ));
}

void CDataCenter::onServerReconnect(const bs::error_code& er) {
    if (!er) {
        m_socket->async_connect(m_serverEndpoint, std::bind(&CDataCenter::handleServerConnection,
                                                            this,
                                                            _1));
    }
}

void CDataCenter::onServerRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er) {
    mylog(DEBUG, "onServerRead:", er.message());
    if (!er) {
        std::istream is(&(*buffer.get()));
        string message;
        while (std::getline(is, message)) {
            istringstream iss(message);
            string command;
            iss >> command;
            if (command == "tradeAnswer") {
                size_t transactionId;
                bool success;
                iss >> transactionId;
                iss >> success;
                {
                    lock_guard<mutex> lock(m_balanceMutex);
                    const int& transactionSum = m_transactionIdSum[transactionId];
                    if (success) {
                        m_balance += 2 * transactionSum;
                    } else {
                        m_balance -= transactionSum;
                    }
                    m_transactionIdSum.erase(transactionId);
                    const int totalTransactionSum = std::accumulate(std::begin(m_transactionIdSum),
                                                             std::end(m_transactionIdSum),
                                                             0,
                                                             [] (int value, auto p) { return value + p.second; }
                    );
                    mylog(INFO, "Transaction id:", transactionId,
                          success ? "success." : "failed.",
                          "Your current balance:", m_balance, ". In processing:", totalTransactionSum);
                }

                sendReserveDatacentersCommand(message + '\n');
                string processedMessage("processed " + std::to_string(transactionId) + "\n");
                sendServerMessage(std::move(processedMessage));
            }
        }
        serverRead();
    } else {
        m_connectedToServer = false;
        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));
    }
}

void CDataCenter::sendServerMessage(string&& message) {
    lock_guard<mutex> lock(m_serverCommandsMutex);
    m_serverCommands.push(message);
}

void CDataCenter::onSendTimer(const bs::error_code& er) {
    if (!er) {
        m_sendServerCommandsTimer.cancel();
        lock_guard<mutex> lock(m_serverCommandsMutex);
        if (m_serverCommands.empty()) {
            m_sendServerCommandsTimer.expires_from_now(boost::posix_time::seconds(1));
            m_sendServerCommandsTimer.async_wait(std::bind(&CDataCenter::onSendTimer, this, _1));
            return;
        }
        const string command = m_serverCommands.front();
        m_serverCommands.pop();
        mylog(DEBUG, "Sending", command, "to server");
        async_write(*(m_socket.get()), boost::asio::buffer(command), std::bind(&CDataCenter::onSendTimer,
                                                                               this,
                                                                               _1));
    }
}

void CDataCenter::onConnectionsCheckTimer(const bs::error_code& er) {
    if (!er) {
        {
            lock_guard<mutex> lock(m_datacenterConnectionsMutex);
            auto removeExpiredConnections = std::remove_if(m_datacentersConnection.begin(), m_datacentersConnection.end(), [](auto& connection) {
                return connection.expired();
            });
            auto removed = std::distance(removeExpiredConnections, m_datacentersConnection.end());
            if (removed > 0) {
                mylog(INFO, "Removing", removed, "expired connections");
            }
            m_datacentersConnection.erase(removeExpiredConnections, m_datacentersConnection.end());
        }
        m_datacentersConnectionsCheckTimer.expires_from_now(boost::posix_time::seconds(1));
        m_datacentersConnectionsCheckTimer.async_wait(std::bind(&CDataCenter::onConnectionsCheckTimer, this, _1));
    }
}

////////////////////////////////////////////////////////////////////////




////////////////////////****** Reserve part ******////////////////////////

void CDataCenter::onMasterConnect(const bs::error_code& er) {
    if (!er) {
        mylog(INFO, "Connected with master");
        readMaster();
    } else {
        mylog(ERROR, "Master connection error:", er.message());
        //        If potential next master was down before current master was down,
        //        then just connectNextMaster
        connectNextMaster();
    }
}

void CDataCenter::readMaster() {
    shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
    async_read_until(*(m_socket.get()), *(buffer.get()), '\n', std::bind(&CDataCenter::onMasterRead,
                                                                         this,
                                                                         buffer,
                                                                         _1));
}

void CDataCenter::onMasterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er) {
    mylog(DEBUG, "onMasterRead:", er.message());
    if (!er) {
        std::istream is(&(*buffer.get()));
        string message;
        while (std::getline(is, message)) {
            mylog(DEBUG, "Received from master:", message);
            istringstream iss(message);
            string command;
            iss >> command;
            if (command == "setBalance") {
                int sum;
                iss >> sum;
                {
                    lock_guard<mutex> lock(m_balanceMutex);
                    m_balance += sum;
                    const int totalTransactionSum = std::accumulate(std::begin(m_transactionIdSum),
                                                             std::end(m_transactionIdSum),
                                                             0,
                                                             [] (int value, auto p) { return value + p.second; }
                    );
                    mylog(INFO, "Your current balance:", m_balance, ". In processing: ", totalTransactionSum);
                }
            } else if (command == "book") {
                int sum;
                iss >> m_lastTransactionId;
                iss >> sum;
                {
                    lock_guard<mutex> lock(m_balanceMutex);
                    m_transactionIdSum[m_lastTransactionId] = sum;
                    mylog(INFO, "During trade operation", sum, "booked. Transaction id =", m_lastTransactionId);
                }
            } else if (command == "tradeAnswer") {
                size_t transactionId;
                bool success;
                iss >> transactionId;
                iss >> success;
                {
                    lock_guard<mutex> lock(m_balanceMutex);
                    const int& transactionSum = m_transactionIdSum[transactionId];
                    if (success) {
                        m_balance += 2 * transactionSum;
                    } else {
                        m_balance -= transactionSum;
                    }
                    m_transactionIdSum.erase(transactionId);
                    const int totalTransactionSum = std::accumulate(std::begin(m_transactionIdSum),
                                                             std::end(m_transactionIdSum),
                                                             0,
                                                             [] (int value, auto p) { return value + p.second; }
                    );
                    mylog(INFO, "Transaction id:", transactionId,
                          success ? "success." : "failed.",
                          "Your current balance:", m_balance, ". In processing:", totalTransactionSum);
                }
            } else {
                mylog(INFO, "Unknown command");
            }
        }
        readMaster();
    } else {
        // Assume master was down. Don't check concrety error codes
        mylog(ERROR, "Master was down:", er.message());
        connectNextMaster();
    }
}

void CDataCenter::sendReserveDatacentersCommand(const string& command) {
    lock_guard<mutex> lock(m_datacenterConnectionsMutex);
    for (auto& datacenterConnection: m_datacentersConnection) {
        std::shared_ptr<Connection> strongConnection = datacenterConnection.lock();
        // Ensure connection is valid, because in another thread it can be disconnected
        if (strongConnection) {
            strongConnection->sendCommandToReserve(command);
        }
    }
}

void CDataCenter::connectNextMaster() {
    m_dataCenters.erase(m_masterId);
    for (auto it = m_dataCenters.begin(); it != m_dataCenters.end(); ++it) {
        mylog(DEBUG, "Map:", it->first);
    }
    m_socket = make_unique<network::socket>(m_service);
    m_masterId = m_dataCenters.begin()->first;

    networkInit();
}

////////////////////////////////////////////////////////////////////////


////////////////////////****** UI commands ******////////////////////////
void CDataCenter::changeBalance(int sum) {
    const string setBalanceCommand("setBalance " + std::to_string(sum) + '\n');
    {
        lock_guard<mutex> lock(m_balanceMutex);
        m_balance += sum;
        const int totalTransactionSum = std::accumulate(std::begin(m_transactionIdSum),
                                            std::end(m_transactionIdSum),
                                            0,
                                            [] (int value, auto p) { return value + p.second; }
        );
        mylog(INFO, "Your current balance:", m_balance, ". In processing:", totalTransactionSum);
    }
    sendReserveDatacentersCommand(setBalanceCommand);
}

void CDataCenter::makeTrade(int sum) {
    if (!m_connectedToServer) {
        mylog(INFO, "No server connection");
        return;
    }
    {
        lock_guard<mutex> lock(m_balanceMutex);
        const int totalTransactionSum = std::accumulate(std::begin(m_transactionIdSum),
                                            std::end(m_transactionIdSum),
                                            0,
                                            [] (int value, auto p) { return value + p.second; }
        );
        if (m_balance < sum + totalTransactionSum) {
            mylog(INFO, "Your have no enough money");
            return;
        }
        m_lastTransactionId++;
        m_transactionIdSum[m_lastTransactionId] = sum;
    }
    mylog(INFO, "During trade operation", sum, "booked. Transaction id =", m_lastTransactionId);
    const string bookBalanceCommand("book " + std::to_string(m_lastTransactionId) + " " + std::to_string(sum) + '\n');
    sendReserveDatacentersCommand(bookBalanceCommand);
    string makeTradeCommand("makeTrade " + std::to_string(m_lastTransactionId) + " " + std::to_string(sum) + '\n');
    sendServerMessage(std::move(makeTradeCommand));
}

/////////////////////////////////////////////////////////////////////////
