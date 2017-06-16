#ifndef COMMON_H
#define COMMON_H

#include <string>

using std::string;

struct DataCenterInfo {
    DataCenterInfo(int id, std::string address, size_t port, bool isMaster):
	m_id(id),
	m_address(address),
	m_port(port),
	m_isMaster(isMaster)
    {}

    int m_id;
    std::string m_address;
    size_t m_port;
    bool m_isMaster;
};

#endif // COMMON_H

