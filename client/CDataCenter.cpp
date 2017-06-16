#include "CDataCenter.h"

CDataCenter::CDataCenter(vector<DataCenterInfo>&& dataCenters, string&& region, int balance, bool isMaster):
    m_dataCenters(std::move(dataCenters)),
    m_region(std::move(region)),
    m_balance(balance),
    m_isMaster(isMaster)
{

}

