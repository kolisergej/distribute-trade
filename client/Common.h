#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <string>
#include <thread>
#include <map>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <boost/shared_array.hpp>

#include <boost/asio.hpp>

using std::map;
using std::mutex;
using std::lock_guard;
using std::string;
using std::vector;
using std::thread;
using std::weak_ptr;
using std::shared_ptr;
using std::placeholders::_1;
using std::placeholders::_2;
using std::make_shared;
using std::cout;
using std::endl;

using network = boost::asio::ip::tcp;
using io_service = boost::asio::io_service;
namespace bs = boost::system;

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

struct SharedBuffer
{
    boost::shared_array<char> buff;
    int size;
    SharedBuffer(size_t size) : buff(new char[size]), size(size)
    {}
    boost::asio::mutable_buffers_1 asio_buff() const {
	return boost::asio::buffer(buff.get(), size);
    }
};

#endif // COMMON_H

