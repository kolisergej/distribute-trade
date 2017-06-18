#ifndef COMMON_H
#define COMMON_H

#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_array.hpp>

using std::cout;
using std::endl;
using std::lock_guard;
using std::make_shared;
using std::make_unique;
using std::map;
using std::mutex;
using std::placeholders::_1;
using std::placeholders::_2;
using std::shared_ptr;
using std::string;
using std::thread;
using std::vector;
using std::weak_ptr;

namespace bs = boost::system;
using io_service = boost::asio::io_service;
using network = boost::asio::ip::tcp;

struct DataCenterConfigInfo {
    DataCenterConfigInfo() {}
    DataCenterConfigInfo(int id, std::string&& address, size_t port, bool isMaster):
	m_id(id),
        m_address(std::move(address)),
	m_port(port),
	m_isMaster(isMaster)
    {}

    int m_id;
    std::string m_address;
    size_t m_port;
    bool m_isMaster;
};

const size_t g_machineCoreCount(thread::hardware_concurrency());
bool setThreadAfinity();

enum logLevel {
    NOLOG,
    ERROR,
    INFO,
    DEBUG,
};

// to avoid extern
logLevel getLoggerLevel();

void setLoggerLevel(logLevel loggerLevel);

void mylog(logLevel loggerLevel);
template <typename T, typename...Ts>
void mylog(logLevel loggerLevel, T&& first, Ts&&... rest) {
    if (getLoggerLevel() >= loggerLevel) {
        cout << first << " ";
        mylog(loggerLevel, std::forward<Ts>(rest)...);
    }
}

#endif // COMMON_H

