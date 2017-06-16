#include "CDataCenter.h"

CDataCenter::CDataCenter(size_t selfId, map<int, DataCenterInfo> &&dataCenters, string&& region, int balance, string&& serverAddress, size_t serverPort):
    m_selfId(selfId),
    m_dataCenters(std::move(dataCenters)),
    m_region(std::move(region)),
    m_balance(balance),
    m_serverAddress(std::move(serverAddress)),
    m_serverPort(serverPort),
    m_isMaster(dataCenters[m_selfId].m_isMaster)
{

}

