#include <exception>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>

#include "CDataCenter.h"
#include "Common.h"

namespace pt = boost::property_tree;

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) {
            throw std::invalid_argument("Specify self id");
        }
        const int selfId = atoi(argv[1]);

        pt::ptree pt;
        read_json("./datacenters.json", pt);

        const logLevel loggerLevel = static_cast<logLevel>(pt.get<size_t>("logLevel"));
        setLoggerLevel(loggerLevel);

        string serverAddress = pt.get<string>("serverAddress");
        const size_t serverPort = pt.get<size_t>("serverPort");
        string region = pt.get<string>("region");
        const int balance = pt.get<int>("balance");

        // Map for ordering by asc id
        map<int, DataCenterConfigInfo> dataCenters;
        bool wasMaster = false;
        auto childs = pt.get_child("dataCenters");
        if (childs.empty()) {
            throw std::invalid_argument("Specify client datacenters");
        }
        bool wasSelfFound = false;
        for (pt::ptree::value_type& dataCenter: childs)
        {
            const int id = dataCenter.second.get<int>("id");
            const bool isMaster = dataCenter.second.get<bool>("master", false);
            if (id == selfId) {
                wasSelfFound = true;
            }
            if (isMaster) {
                if (wasMaster) {
                    throw std::invalid_argument("Invalid config. Specify only one master client");
                }
                wasMaster = true;
            }
            dataCenters[id] = DataCenterConfigInfo(
                    id,
                    dataCenter.second.get<string>("address"),
                    dataCenter.second.get<int>("port"),
                    isMaster
                    );
        }
        if (!wasSelfFound) {
            throw std::invalid_argument("Self id wasn't found in data centers");
        }
        if (!wasMaster) {
            dataCenters.begin()->second.m_isMaster = true;
            mylog(INFO, "Master client is set for id:", dataCenters.begin()->second.m_id);
        }
        if (dataCenters.find(selfId) == dataCenters.end()) {
            throw std::invalid_argument("Invalid config. Specify correct self id");
        }

        // Make one thread for our client working
        CDataCenter dataCenter(selfId, std::move(dataCenters), std::move(region), balance, std::move(serverAddress), serverPort);
        thread workerThread([&](){
            setThreadAfinity();
            dataCenter.start();
        });
        workerThread.detach();

        // And main thread will be for user simulation. For simplify don't validate commands
        while (true) {
            string command;
            int sum;
            std::cin >> command;
            std::cin >> sum;
            if (command == "changeBalance" || command == "c") {
                dataCenter.changeBalance(sum);
            } else if (command == "makeTrade" || command == "m") {
                dataCenter.makeTrade(sum);
            } else {
                mylog(INFO, "No such command");
            }
        }


    } catch(const std::exception& ex) {
        mylog(ERROR, ex.what());
    }

    return 0;
}

