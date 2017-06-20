#include "CDataCenter.h"
#include "Connection.h"


CDataCenter::CDataCenter(int selfId, map<int, DataCenterConfigInfo>&& dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort):
    m_selfId(selfId),
    m_dataCenters(std::move(dataCenters)),
    m_region(std::move(region)),
    m_balance(balance),
    m_lastBookId(0),
    m_service(),
    m_serverEndpoint(boost::asio::ip::address::from_string(serverAddress), serverPort),
    m_acceptor(m_service, network::endpoint(network::v4(), m_dataCenters[m_selfId].m_port)),
    m_serverReconnectTimer(m_service),
    m_socket(make_unique<network::socket>(m_service)),
    m_payloadTimer(m_service)
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
        mylog(INFO, "I'm master, listen", masterNode.m_port);
        shared_ptr<Connection> connection = Connection::createConnection(m_service);
        m_acceptor.async_accept(connection->socket(), std::bind(&CDataCenter::handleClientConnection,
                                                                this,
                                                                connection,
                                                                _1));
        m_socket->async_connect(m_serverEndpoint, std::bind(&CDataCenter::handleServerConnection,
                                             this,
                                             _1));
    } else {
        mylog(INFO, "I'm reserve, connecting to master", masterNode.m_address, ':', masterNode.m_port);
        const network::endpoint ep(boost::asio::ip::address::from_string(masterNode.m_address), masterNode.m_port);
        m_socket->async_connect(ep, std::bind(&CDataCenter::onMasterConnect,
                                             this,
                                             _1));
    }
}


////////////////////////****** Master part ******////////////////////////

void CDataCenter::handleClientConnection(shared_ptr<Connection> connection, const bs::error_code& er) {
    if (!er) {
        mylog(INFO, "Handle reserve client connection");
        {
            lock_guard<mutex> lock(m_clientConnectionsMutex);
            m_clientsConnection.push_back(connection);
        }
        connection->start();
        shared_ptr<Connection> newConnection = Connection::createConnection(m_service);
        m_acceptor.async_accept(newConnection->socket(), std::bind(&CDataCenter::handleClientConnection,
                                                                this,
                                                                newConnection,
                                                                _1));
    }
}

void CDataCenter::handleServerConnection(const bs::error_code& er) {
    if (!er) {
        m_serverReconnectTimer.cancel();
    } else {
        m_socket = make_unique<network::socket>(m_service);
        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));
    }
}

void CDataCenter::onServerReconnect(const bs::error_code& er) {
    if (!er) {
        m_socket->async_connect(m_serverEndpoint, std::bind(&CDataCenter::handleServerConnection,
                                                           this,
                                                           _1));
    }
}

void CDataCenter::sendServerMessage(string&& message) {
    m_socket->async_send(boost::asio::buffer(message), std::bind(&CDataCenter::onServerWrite,
                                                                    this,
                                                                    _1,
                                                                 message));
}

void CDataCenter::onServerWrite(const bs::error_code& er, string message) {
    if (!er) {
        mylog(DEBUG, "Command", message, "sent to server");
        istringstream iss(message);
        string command;
        iss >> command;
        if (command == "makeTrade") {
            shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
            async_read_until(*(m_socket.get()), *(buffer.get()), '\n', std::bind(&CDataCenter::onServerRead,
                                                                                 this,
                                                                                 buffer,
                                                                                 _1,
                                                                                 _2));
        }
    } else {
        m_serverReconnectTimer.cancel();
        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));
    }
}

void CDataCenter::onServerRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytes_transfered) {
    mylog(DEBUG, "onServerRead:", er.message());
    if (!er) {
        boost::asio::streambuf::const_buffers_type bufs = buffer->data();
        const string message(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_transfered);
        istringstream iss(message);
        string command;
        iss >> command;
        if (command == "tradeAnswer") {
            int bookId;
            bool success;
            iss >> bookId;
            iss >> success;
            {
                lock_guard<mutex> lock(m_balanceMutex);
                if (success) {
                    m_balance += 2 * m_bookIdSum[bookId];
                }
                m_bookIdSum.erase(bookId);
                mylog(INFO, "Trade answer.", success ? "Success" : "Failed", "Your current balance:", m_balance);
            }

            sendReserveClientsCommand(message);
            string ackMessage("processed " + std::to_string(bookId));
            sendServerMessage(std::move(ackMessage));
        }
    } else {
        m_serverReconnectTimer.cancel();
        m_serverReconnectTimer.expires_from_now(boost::posix_time::seconds(1));
        m_serverReconnectTimer.async_wait(std::bind(&CDataCenter::onServerReconnect, this, _1));
    }
}

////////////////////////////////////////////////////////////////////////




////////////////////////****** Reserve part ******////////////////////////

void CDataCenter::onMasterConnect(const bs::error_code& er) {
    if (!er) {
        mylog(INFO, "Connected with master");
        writePayloadToMaster();
    } else {
        mylog(ERROR, "Master connection error:", er.message());
//        If potential next master was down before current master was down,
//        then just connectNextMaster
        connectNextMaster();
    }
}

void CDataCenter::writePayloadToMaster() {
    m_payloadTimer.expires_from_now(boost::posix_time::seconds(1));
    m_payloadTimer.async_wait(std::bind(&CDataCenter::onWriteTimer, this, _1));
}

void CDataCenter::onWriteTimer(const bs::error_code& er) {
    if (!er) {
        m_socket->async_send(boost::asio::buffer("payload\n"), std::bind(&CDataCenter::onMasterPayloadWrite,
                                                                        this,
                                                                        _1));
    }
}

void CDataCenter::onMasterPayloadWrite(const bs::error_code& er) {
    mylog(DEBUG, "onMasterPayloadWrite:", er.message());
    if (!er) {
        shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
        async_read_until(*(m_socket.get()), *(buffer.get()), '\n', std::bind(&CDataCenter::onMasterRead,
                                                                    this,
                                                                    buffer,
                                                                    _1,
                                                                    _2));
    } else {
        // Assume master was down. Don't check concrete error codes
        mylog(ERROR, "Master was down:", er.message());
        connectNextMaster();
    }
}

void CDataCenter::onMasterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytes_transfered) {
    mylog(DEBUG, "onMasterRead:", er.message());
    if (!er) {
        boost::asio::streambuf::const_buffers_type bufs = buffer->data();
        const string message(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_transfered);
        mylog(DEBUG, "Received from master:", message);
        if (message == "payload\n") {
            writePayloadToMaster();
        } else {
            istringstream iss(message);
            string command;
            iss >> command;
            if (command == "setBalance") {
                int sum;
                iss >> sum;
                {
                    lock_guard<mutex> lock(m_balanceMutex);
                    m_balance += sum;
                    mylog(INFO, "Your current balance:", m_balance);
                }
                writePayloadToMaster();
            } else if (command == "book") {
                int bookId;
                int sum;
                iss >> bookId;
                iss >> sum;
                {
                    lock_guard<mutex> lock(m_balanceMutex);
                    m_bookIdSum[bookId] = sum;
                    mylog(INFO, "During trade operation", sum, "booked");
                }
                writePayloadToMaster();
            } else if (command == "tradeAnswer") {
                int bookId;
                bool success;
                iss >> bookId;
                iss >> success;
                {
                    lock_guard<mutex> lock(m_balanceMutex);
                    if (success) {
                        m_balance += 2 * m_bookIdSum[bookId];
                    }
                    m_bookIdSum.erase(bookId);
                    mylog(INFO, "Trade answer.", success ? "Success" : "Failed", "Your current balance:", m_balance);
                }
            } else {
                mylog(INFO, "Unknown command");
                writePayloadToMaster();
            }
        }
    } else {
        // Assume master was down. Don't check concrety error codes
        mylog(ERROR, "Master was down:", er.message());
        connectNextMaster();
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
        const string setBalanceCommand("setBalance " + std::to_string(sum));
        {
            lock_guard<mutex> lock(m_balanceMutex);
            m_balance += sum;
            mylog(INFO, "Your current balance:", m_balance);
        }
        sendReserveClientsCommand(setBalanceCommand);
    }

    void CDataCenter::makeTrade(int sum) {
        {
            lock_guard<mutex> lock(m_balanceMutex);
            const int bookSum = std::accumulate(std::begin(m_bookIdSum),
                                            std::end(m_bookIdSum),
                                            0,
                                            [] (int value, auto p) { return value + p.second; }
            );
            if (m_balance < sum + bookSum) {
                mylog(INFO, "Your have no enough money");
                return;
            }
        }
        string makeTradeCommand("makeTrade " + std::to_string(sum));
        m_bookIdSum[m_lastBookId++] = sum;
        const string bookBalanceCommand("book " + std::to_string(m_lastBookId) + " " + std::to_string(sum));
        sendReserveClientsCommand(bookBalanceCommand);
        sendServerMessage(std::move(makeTradeCommand));
    }

    void CDataCenter::sendReserveClientsCommand(const string& command) {
        lock_guard<mutex> lock(m_clientConnectionsMutex);
        auto removeExpiredConnections = std::remove_if(m_clientsConnection.begin(), m_clientsConnection.end(), [](auto& connection) {
            return connection.expired();
        });
        mylog(INFO, "Removing", std::distance(removeExpiredConnections, m_clientsConnection.end()), "expired connections");
        m_clientsConnection.erase(removeExpiredConnections, m_clientsConnection.end());
        for (auto& clientConnection: m_clientsConnection) {
            std::shared_ptr<Connection> strongConnection = clientConnection.lock();
            // Ensure connection is valid, because in another thread it can be disconnected
            if (strongConnection) {
                strongConnection->sendCommandToReserve(command);
            }
        }
    }

/////////////////////////////////////////////////////////////////////////
