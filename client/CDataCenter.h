#ifndef WORKER_H
#define WORKER_H

#include <vector>

#include "Common.h"

using std::vector;


class CDataCenter
{
public:
    CDataCenter(vector<DataCenterInfo>&& dataCenters, string&& region, int balance, bool isMaster);

    vector<DataCenterInfo> m_dataCenters;
    string m_region;
    int m_balance;
    bool m_isMaster;
};

#endif // WORKER_H
