#include "CDataCenter.h"

CDataCenter::CDataCenter(vector<DataCenterInfo>&& dataCenters, string&& region, int balance, bool isMaster):
    m_dataCenters(std::move(dataCenters)),
    m_region
{

}

