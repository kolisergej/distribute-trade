#include <iostream>
#include <vector>
#include <exception>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>

#include "Common.h"

namespace pt = boost::property_tree;
using std::vector;

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) {
            throw std::invalid_argument("Specify client id");
        }
        const size_t clientId = atoi(argv[1]);
        pt::ptree pt;
        read_json("./client.json", pt);
        const string serverAddress = pt.get<string>("server_address");
        const size_t serverPort = pt.get<size_t>("server_port");
        const int balance = pt.get<int>("balance");
        const string region = pt.get<string>("region");

        vector<DataCenterInfo> dataCenters;
        for (pt::ptree::value_type& dataCenter: pt.get_child("data_centers"))
        {
            const int id = dataCenter.second.get<int>("id");
            const string address = dataCenter.second.get<string>("address");
            const int port = dataCenter.second.get<int>("port");
            const bool isMaster = dataCenter.second.get<bool>("master", false);
            std::cout << id << " " << serverPort << " " << balance << " " << region << " " << isMaster << std::endl;
            dataCenters.push_back(DataCenterInfo(id, address, port, isMaster));
        }

        std::cout << serverAddress << " " << serverPort << " " << balance << " " << region << std::endl;


    } catch(const std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    return 0;
}

