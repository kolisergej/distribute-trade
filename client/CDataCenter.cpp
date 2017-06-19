#include "CDataCenter.h"
#include "Connection.h"


CDataCenter::CDataCenter(int selfId, map<int, DataCenterConfigInfo>&& dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort):
    m_selfId(selfId),
    m_dataCenters(std::move(dataCenters)),
    m_region(std::move(region)),
    m_balance(balance),
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
            lock_guard<mutex> lock(m_connectionMutex);
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

////////////////////////////////////////////////////////////////////////




////////////////////////****** Reserve part ******////////////////////////

void CDataCenter::onMasterConnect(const bs::error_code& er) {
    if (!er) {
        mylog(INFO, "Connected with master");
        writeMaster();
    } else {
        mylog(ERROR, "Master connection error:", er.message());
//        If potential next master was down before current master was down,
//        then just connectNextMaster
        connectNextMaster();
    }
}

void CDataCenter::writeMaster() {
    m_payloadTimer.expires_from_now(boost::posix_time::seconds(1));
    m_payloadTimer.async_wait(std::bind(&CDataCenter::onWriteTimer, this, _1));
}

void CDataCenter::onWriteTimer(const bs::error_code& er) {
    if (!er) {
        m_socket->async_send(boost::asio::buffer("payload\n"), std::bind(&CDataCenter::onMasterWrite,
                                                                        this,
                                                                        _1));
    }
}

void CDataCenter::onMasterWrite(const bs::error_code& er) {
    mylog(DEBUG, "onMasterWrite:", er.message());
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
        const string payload("payload\n");
        mylog(DEBUG, "Received from master:", message);
        if (message == payload) {
            writeMaster();
        } else {
            istringstream iss(message);
            string command;
            int sum;
            iss >> command;
            iss >> sum;
            if (command == "changeBalance") {
                m_balance.fetch_add(sum);
                mylog(INFO, "Your current balance:", m_balance.load(std::memory_order_release));
            } else {
                mylog(INFO, "Unknown command");
                writeMaster();
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
        m_balance.fetch_add(sum);
        {
            lock_guard<mutex> lock(m_connectionMutex);
            auto removeExpiredConnections = std::remove_if(m_clientsConnection.begin(), m_clientsConnection.end(), [](auto& connection) {
                return connection.expired();
            });
            mylog(DEBUG, "Removing", std::distance(m_clientsConnection.end(), removeExpiredConnections), "connections");
            m_clientsConnection.erase(removeExpiredConnections, m_clientsConnection.end());
            for (auto& clientConnection: m_clientsConnection) {
                string setBalanceCommand("setBalance " + std::to_string(sum));
                std::shared_ptr<Connection> strongConnection = clientConnection.lock();
                // Ensure connection is valid, because in another thread it can be disconnected
                if (strongConnection) {
                    strongConnection->sendCommand(std::move(setBalanceCommand));
                }
            }
        }
        mylog(INFO, "Your current balance:", m_balance.load(std::memory_order_release));
    }

    void CDataCenter::makeTrade(int sum) {
        if (m_balance.load(std::memory_order_acquire) < sum) {
            mylog(INFO, "Your have no enough money");
            return;
        }
    }

/////////////////////////////////////////////////////////////////////////
