#include <iostream>
#include <map>
#include <exception>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>

#include "CDataCenter.h"
#include "Common.h"

namespace pt = boost::property_tree;
using std::map;

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) {
            throw std::invalid_argument("Specify self id");
        }
        const int selfId = atoi(argv[1]);

        pt::ptree pt;
        read_json("./client.json", pt);

        string serverAddress = pt.get<string>("server_address");
        const size_t serverPort = pt.get<size_t>("server_port");
        string region = pt.get<string>("region");
        const int balance = pt.get<int>("balance");

        // Map for ordering by asc id
        map<int, DataCenterInfo> dataCenters;
        for (pt::ptree::value_type& dataCenter: pt.get_child("data_centers"))
        {
            const int id = dataCenter.second.get<int>("id");
            dataCenters[id] = DataCenterInfo(
                    id,
                    dataCenter.second.get<string>("address"),
                    dataCenter.second.get<int>("port"),
                    dataCenter.second.get<bool>("master", false)
                    );
        }
        if (dataCenters.find(selfId) == dataCenters.end()) {
            throw std::invalid_argument("Invalid config. Specify correct self id");
        }

        CDataCenter dataCenter(selfId, std::move(dataCenters), std::move(region), balance, std::move(serverAddress), serverPort);
    } catch(const std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    return 0;
}

