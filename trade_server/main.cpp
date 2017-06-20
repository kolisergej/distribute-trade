#include <exception>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>

#include "CTradeServer.h"
#include "Common.h"

namespace pt = boost::property_tree;

int main(int argc, char** argv)
{
    try
    {
        pt::ptree pt;
        read_json("./trade_server.json", pt);

        const logLevel loggerLevel = static_cast<logLevel>(pt.get<size_t>("logLevel"));
        setLoggerLevel(loggerLevel);

        const size_t port = pt.get<size_t>("port");

        CTradeServer tradeServer(port);
        tradeServer.start();

    } catch(const std::exception& ex) {
        mylog(ERROR, ex.what());
    }

    return 0;
}

