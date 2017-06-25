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

        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));

        m_datacentersConnectionsCheckTimer.expires_from_now(boost::posix_time::seconds(1));
        m_datacentersConnectionsCheckTimer.async_wait(std::bind(&CDataCenter::onConnectionsCheckTimer, this, _1));
    } else {
        mylog(INFO, "I'm reserve datacenter, connecting to master", masterNode.m_address, ':', masterNode.m_port);
        const network::endpoint ep(boost::asio::ip::address::from_string(masterNode.m_address), masterNode.m_port);
        m_socket->async_connect(ep, std::bind(&CDataCenter::onMasterConnect,
                                              this,
                                              _1));
    }
}

int CDataCenter::tradeAnswerProcess(istringstream& iss) {
    size_t transactionId;
    int sum;
    bool success;
    iss >> transactionId;
    iss >> sum;
    iss >> success;
    {
        lock_guard<mutex> lock(m_balanceMutex);
        int transactionSum;
        auto it = m_transactionIdSum.find(transactionId);
        if (it != m_transactionIdSum.end()) {
            transactionSum = it->second;
        } else {
            //            If we here, it means that master datacenter was down
            //            and it had no time for send book command to reserve due to lots of reserved datacenters or network latency
            //            But send transaction to server. Such case we discard all our m_transactionIdSums and assign hard balance
            m_transactionIdSum.clear();
            transactionSum = sum;
            mylog(INFO, "Hard assign transaction sum", transactionSum);
        }
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
    return transactionId;
}

void CDataCenter::setSocketOptions() {
    network::socket::reuse_address reuseAddress(true);
    network::socket::keep_alive keepAlive(true);
    m_socket->set_option(reuseAddress);
    m_socket->set_option(keepAlive);
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
        setSocketOptions();
        string initServerMessage("setRegion " + m_region + '\n');
        pushServerMessage(std::move(initServerMessage));
        serverRead();
    } else {
        m_connectedToServer = false;
        m_socket = make_unique<network::socket>(m_service);
        m_serverReconnectTimer.cancel();
        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));
    }
}

void CDataCenter::serverRead() {
    shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
    async_read_until(*m_socket, *buffer, '\n', bind(&CDataCenter::onServerRead,
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
        std::istream is(&(*buffer));
        string message;
        while (std::getline(is, message)) {
            istringstream iss(message);
            string command;
            iss >> command;
            if (command == "tradeAnswer") {
                const int transactionId = tradeAnswerProcess(iss);
                sendReserveDatacentersCommand(message + '\n');
                string processedMessage("processed " + std::to_string(transactionId) + "\n");
                pushServerMessage(std::move(processedMessage));
            }
        }
        serverRead();
    } else {
        mylog(INFO, "Server was down. Reconnecting.");
        m_connectedToServer = false;
        m_serverReconnectTimer.cancel();
        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));
    }
}

void CDataCenter::pushServerMessage(string&& message) {
    lock_guard<mutex> lock(m_serverCommandsMutex);
    m_serverCommands.push(message);
    m_socket->get_io_service().post(std::bind(&CDataCenter::sendCommandToServer, this));
}

void CDataCenter::sendCommandToServer() {
    lock_guard<mutex> lock(m_serverCommandsMutex);
    const string& command = m_serverCommands.front();
    std::shared_ptr<string> serverWriteBuffer = std::make_shared<string>(command);
    mylog(DEBUG, "Sending", command, "to server");
    m_serverCommands.pop();
    m_socket->async_send(boost::asio::buffer(*serverWriteBuffer), bind(&CDataCenter::onSendCommandToServer,
                                                                       this,
                                                                       serverWriteBuffer,
                                                                       _1));
}

void CDataCenter::onSendCommandToServer(std::shared_ptr<string> buffer, const bs::error_code& er) {
    (void)buffer;
    if (er) {
        mylog(DEBUG, "onSendCommandToServer error:", er.message());
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
        setSocketOptions();
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
    async_read_until(*m_socket, *buffer, '\n', std::bind(&CDataCenter::onMasterRead,
                                                         this,
                                                         buffer,
                                                         _1));
}

void CDataCenter::onMasterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er) {
    mylog(DEBUG, "onMasterRead:", er.message());
    if (!er) {
        std::istream is(&(*buffer));
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
                tradeAnswerProcess(iss);
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
            strongConnection->pushCommandToReserve(command);
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
    string makeTradeCommand("makeTrade " + std::to_string(m_lastTransactionId) + " " + std::to_string(sum) + '\n');
    pushServerMessage(std::move(makeTradeCommand));
    const string bookBalanceCommand("book " + std::to_string(m_lastTransactionId) + " " + std::to_string(sum) + '\n');
    sendReserveDatacentersCommand(bookBalanceCommand);
}

/////////////////////////////////////////////////////////////////////////
