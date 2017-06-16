#ifndef WORKER_H
#define WORKER_H

#include <map>

#include "Common.h"

using std::map;

class CDataCenter
{
public:
    CDataCenter(size_t selfId, map<int, DataCenterConfigInfo>&& dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort);

    size_t m_selfId;
    map<int, DataCenterConfigInfo> m_dataCenters;
    const string m_region;
    int m_balance;

    const string m_serverAddress;
    const size_t m_serverPort;

    bool m_isMaster;
};

#endif // WORKER_H
