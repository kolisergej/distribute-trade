#include "CDataCenter.h"
#include "Connection.h"

using std::thread;

CDataCenter::CDataCenter(int selfId, map<int, DataCenterConfigInfo>&& dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort):
    m_selfId(selfId),
    m_dataCenters(std::move(dataCenters)),
    m_region(std::move(region)),
    m_balance(balance),
    m_isMaster(m_dataCenters[m_selfId].m_isMaster),
    m_serverEndpoint(boost::asio::ip::address::from_string(serverAddress), serverPort),
    m_acceptor(m_service, network::endpoint(network::v4(), m_dataCenters[m_selfId].m_port)),
    m_socket(m_service),
    m_payloadTimer(m_service) {

}

void CDataCenter::start() {
    const auto masterDataCenter = std::find_if(m_dataCenters.begin(), m_dataCenters.end(), [](const auto& pair) {
       return pair.second.m_isMaster;
    });
    networkInit(masterDataCenter->first);

    const size_t thread_count = std::thread::hardware_concurrency();
    vector<thread> thread_group;
    for (size_t i = 0; i < thread_count; ++i) {
        thread_group.push_back(thread([this] {
            m_service.run();
        }));
    }

    for (auto& thread: thread_group) {
        thread.join();
    }
}

void CDataCenter::networkInit(int master_id) {
    cout << "networkInit:: Master id = " << master_id << endl;
    if (m_isMaster) {
        cout << "I'm master, listen " << m_dataCenters[m_selfId].m_port << " port" << endl;
        shared_ptr<Connection> connection = Connection::createConnection(m_service);
        cout << "Before accept clients\n";
        m_acceptor.async_accept(connection->socket(), std::bind(&CDataCenter::handleClientConnection,
                                                                this,
                                                                connection,
                                                                _1));
//        cout << "Before connect with server\n";
//        m_socket.async_connect(m_serverEndpoint, std::bind(&CDataCenter::handleServerConnection,
//                                             this,
//                                             _1));
    } else {
        const auto masterNode = m_dataCenters[master_id];
        cout << "I'm reserve, connecting to " << masterNode.m_address << ":" << masterNode.m_port << endl;
        network::endpoint ep(boost::asio::ip::address::from_string(masterNode.m_address), masterNode.m_port);
        m_socket.async_connect(ep, std::bind(&CDataCenter::onMasterConnect,
                                             this,
                                             _1));
    }
}

void CDataCenter::handleClientConnection(shared_ptr<Connection> connection, const bs::error_code& er) {
    if (!er) {
        cout << "Handle reserve client connection" << endl;
        {
            lock_guard<mutex> lock(m_mutex);
            m_clients_connection.push_back(connection);
        }
        connection->start();
        shared_ptr<Connection> newConnection = Connection::createConnection(m_service);
        m_acceptor.async_accept(newConnection->socket(), std::bind(&CDataCenter::handleClientConnection,
                                                                this,
                                                                newConnection,
                                                                _1));
    } else {
        cout << "Handle reserve client connection: " << er.message() << endl;
    }
}

void CDataCenter::handleServerConnection(const bs::error_code& er) {
    if (!er) {
    } else {
        m_socket.async_connect(m_serverEndpoint, std::bind(&CDataCenter::handleServerConnection,
                                                           this,
                                                           _1));
    }
}



void CDataCenter::onMasterConnect(const bs::error_code& er) {
    if (!er) {
        cout << "Connected with master" << endl;
        writeMaster();
    }
}

void CDataCenter::writeMaster() {
    m_payloadTimer.expires_from_now(boost::posix_time::seconds(1));
    m_payloadTimer.async_wait(std::bind(&CDataCenter::onWriteTimer, this, _1));
}

void CDataCenter::onWriteTimer(const bs::error_code& er) {
    if (!er) {
        m_socket.async_send(boost::asio::buffer("payload\n"), std::bind(&CDataCenter::onMasterWrite,
                                                                        this,
                                                                        _1));
    }
}

void CDataCenter::onMasterWrite(const bs::error_code& er) {
    cout << "onMasterWrite: " << er.message() << endl;
    if (!er) {
        shared_ptr<boost::asio::streambuf> buffer = make_shared<boost::asio::streambuf>();
        async_read_until(m_socket, *(buffer.get()), '\n', std::bind(&CDataCenter::onMasterRead,
                                                                    this,
                                                                    buffer,
                                                                    _1,
                                                                    _2));
    }
}

void CDataCenter::onMasterRead(shared_ptr<boost::asio::streambuf> buffer, const bs::error_code& er, size_t bytes_transfered) {
    cout << "onMasterRead: " << er.message() << endl;
    if (!er) {
        boost::asio::streambuf::const_buffers_type bufs = buffer->data();
        const string message(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_transfered);
        const string payload("payload\n");
        cout << "Received from master: " << message << endl;
        if (message == payload) {
            writeMaster();
        }
    }
}
