/*
Copyright (c) 2022, David C Horton

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <boost/algorithm/string.hpp>
#include <boost/variant.hpp>
#include <boost/redis/src.hpp>
#include <boost/redis/logger.hpp>
#include <boost/system/error_code.hpp>
#include <iostream>
#include <iterator>

#include <string>
#include <iostream>


namespace net = boost::asio;
using boost::redis::connection;
using boost::redis::request;
using boost::redis::response;
using boost::redis::config;
using boost::redis::logger;
using boost::redis::address;
using boost::redis::ignore_t;

#include "blacklist.hpp"
#include "controller.hpp"

namespace {
  logger l(logger::level::err);

  std::vector<address> parseAddressList(const std::string& addrList) {
    std::vector<address> addresses;
    std::vector<std::string> hostPortPairs;

    // Split the string on commas to get a list of host:port pairs.
    boost::split(hostPortPairs, addrList, boost::is_any_of(","));

    for(const std::string& pair : hostPortPairs) {
        std::vector<std::string> hostPort;
        // Split each pair on ':' to separate the host and the port.
        boost::split(hostPort, pair, boost::is_any_of(":"));
        if(hostPort.size() == 2) {
            address addr = { hostPort[0], hostPort[1] };
            addresses.push_back(addr);
        }
    }
    return addresses;
  }

}

namespace drachtio {

  void Blacklist::start(void) {
      srand (time(NULL));    
      std::thread t(&Blacklist::threadFunc, this) ;
      m_thread.swap( t ) ;
  }
  void Blacklist::threadFunc() {
    DR_LOG(log_debug) << "Blacklist thread id: " << std::this_thread::get_id()  ;
    while (true) {
      try {
        address addr;
        if (m_redisSentinels.length() > 0) {
          std::string ip, port;
          querySentinels(ip, port);
          addr.host = ip;
          addr.port = port;
        }
        else {
          addr.host = m_redisAddress;
          addr.port = m_redisPort;
        }
        DR_LOG(log_debug) << "querying redis for blacklisted ips at " << addr.host << ":" << addr.port ;

        config cfg;
        request req;
        response< std::list<std::string> > resp;
        net::io_context ioc;
        connection conn{ioc};

        cfg.addr = addr;
        if (!m_redisUsername.empty()) {
          cfg.username = m_redisUsername;
        }
        if (!m_redisPassword.empty()) {
          cfg.password = m_redisPassword;
        }

        req.push("SMEMBERS", m_redisKey);
        conn.async_run(cfg, l, net::detached);
        conn.async_exec(req, resp, [&](auto ec, auto) {
          if (!ec) {
            auto& arr = std::get<0>(resp).value();
            DR_LOG(log_debug) << "Found:" << arr.size() << " blacklisted ips in redis";
            for (auto& r : arr) {
              m_ips.insert(r);
            }
          }
          conn.cancel();
        });
        ioc.run();

        int sleepSecs = m_ips.size() > 0 ? m_refreshSecs : 300;
        DR_LOG(log_debug) << "sleeping for " << sleepSecs << " seconds";
        std::this_thread::sleep_for (std::chrono::seconds(sleepSecs));

      } catch (const boost::system::system_error& e) {
        DR_LOG(log_error) << "Caught boost system error in threadFunc: " << e.what();
        return;
      } catch (const std::exception& e) {
        DR_LOG(log_error) << "Caught exception in Blacklist::threadFunc: " << e.what();
        return;
      } catch (...) {
        DR_LOG(log_error) << "Caught unknown exception in Blacklist::threadFunc";
        return;
      }
    }
  }

  void Blacklist::querySentinels(std::string& ip, std::string& port) {
    try {
      config cfg;
      request req;
      response<std::optional<std::array<std::string, 2>>, ignore_t> resp;
      net::io_context ioc;
      connection conn{ioc};

      auto addresses = parseAddressList(m_redisSentinels);
      auto addr = addresses.front();
      req.push("SENTINEL", "get-master-addr-by-name", m_redisServiceName.c_str());

      DR_LOG(log_debug) << "querying sentinels " << addr.host << ":" << addr.port;
      cfg.addr = addresses.front();
      conn.async_run(cfg, l, net::detached);
      conn.async_exec(req, resp, [&](auto ec, auto) {
          if (!ec && std::get<0>(resp)) {
            ip = std::get<0>(resp).value().value().at(0);
            port = std::get<0>(resp).value().value().at(1);
            DR_LOG(log_debug) << "sentinel reports master at " << ip << " port " << port;
          }
          else if (ec) {
            DR_LOG(log_debug) << "error querying sentinel: " << ec.message();
          }
          else {
            DR_LOG(log_debug) << "sentinel reports no master";
          }
          conn.cancel();
      });
      ioc.run();
    } catch (const boost::system::system_error& e) {
      DR_LOG(log_error) << "Caught boost system error in Blacklist::querySentinels: " << e.what();
      return;
    } catch (const std::exception& e) {
      DR_LOG(log_error) << "Caught exception in Blacklist::Blacklist::querySentinels: " << e.what();
      return;
    } catch (...) {
      DR_LOG(log_error) << "Caught unknown exception in Blacklist::Blacklist::querySentinels";
      return;
    }

  }

}
