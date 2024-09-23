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
#include <boost/variant.hpp>
#include <regex>

#include <hiredis/hiredis.h>

#include "blacklist.hpp"
#include "controller.hpp"

namespace drachtio {


    std::tuple<std::string, unsigned int> parseAddress(const std::string& address) {
      size_t colon_pos = address.find(':');
      if (colon_pos != std::string::npos) {
        std::string ip = address.substr(0, colon_pos);
        unsigned int port = std::stoul(address.substr(colon_pos + 1));
        return std::make_tuple(ip, port);
      }
      // Handle error case if needed, for now returning an empty string and 0
      return std::make_tuple(std::string(), 0);
    }

    std::vector<std::tuple<std::string, int>> parseIpPort(const std::string& input) {
      std::vector<std::tuple<std::string, int>> result;
      std::istringstream ss(input);
      std::string token;

      while (std::getline(ss, token, ',')) {
          // Trim leading and trailing spaces from the token
          size_t start = token.find_first_not_of(" \t");
          size_t end = token.find_last_not_of(" \t");
          token = token.substr(start, end - start + 1);

          // Split token into IP and port
          size_t colon_pos = token.find(':');
          if (colon_pos != std::string::npos) {
              std::string ip = token.substr(0, colon_pos);
              int port = std::stoi(token.substr(colon_pos + 1));
              result.emplace_back(ip, port);
          }
      }

      return result;
    }


    static bool QuerySentinel(
      std::string redisMaster,
      const boost::asio::ip::tcp::endpoint& endpoint,
      std::unordered_set<std::string>& replicas
      ) {
      try {
        auto ip = endpoint.address().to_string();
        auto port = endpoint.port();
        redisContext* c = redisConnect(ip.c_str(), port);

        replicas.clear();

        DR_LOG(log_notice) << "Blacklist::QuerySentinel - connecting to redis sentinel at " << ip << ":" << port;
        if (c == NULL || c->err) {
          if (c) {
            DR_LOG(log_error) << "Blacklist::QuerySentinel - Error: connecting to " << endpoint.address() << " " << c->errstr ;
            redisFree(c);
          } else {
            DR_LOG(log_error) << "Blacklist::QuerySentinel - Error: connecting to " << endpoint.address() << " can't allocate redis context" ;
          }
          return false;
        }

        redisReply *reply = (redisReply *) redisCommand(c, "SENTINEL REPLICAS %s", redisMaster.c_str());
        if (reply == NULL) {
          DR_LOG(log_error) << "Blacklist::QuerySentinel - Error: connecting to " << endpoint.address() << " " << c->errstr ;
          redisFree(c);
          return false;
        }
        if (c->err) {
          DR_LOG(log_error) << "Blacklist::QuerySentinel - Error: querying redis " << endpoint.address() << " " << c->errstr ;
          freeReplyObject(reply);
          redisFree(c);
          return false;
        }
        if (reply->type != REDIS_REPLY_ARRAY) {
          DR_LOG(log_error) << "Blacklist::QuerySentinel - Error: querying redis " << endpoint.address() << " unexpected reply type " << reply->type ;
          freeReplyObject(reply);
          redisFree(c);
          return false;
        }

        DR_LOG(log_debug) << "Blacklist::QuerySentinel - redis sentinel reports " << reply->elements << " replicas" ;
        for (int i = 0; i < reply->elements; i++) {
          int j;
          redisReply *r2;
          std::string ip;
          unsigned int port = 0;

          r2 = reply->element[i];
          if (r2->type != REDIS_REPLY_ARRAY || r2->elements % 2) {
              DR_LOG(log_notice) << "Error:  No sub array or bad count";
              return false;
          }


          for (j = 0; j < r2->elements; j+=2) {
            if (r2->element[j]->type != REDIS_REPLY_STRING ||
                r2->element[j+1]->type != REDIS_REPLY_STRING)
            {
                DR_LOG(log_notice) << "Error:  Invalid sub array replies";
                return false;
            }
            auto name = r2->element[j]->str;
            auto value = r2->element[j+1]->str;
            if (0 == strcmp(r2->element[j]->str, "ip")) ip = r2->element[j+1]->str;
            if (0 == strcmp(r2->element[j]->str, "port")) port = atoi(r2->element[j+1]->str);
            
            if (ip.length() && port) {
              auto replica = ip + ":" + std::to_string(port);
              DR_LOG(log_notice) << "Blacklist::QuerySentinel - replica found at " << replica ; 
              replicas.insert(replica);
              break;
            }
          }
        }
        freeReplyObject(reply);
        redisFree(c);
        return true;
      } catch( std::exception& e) {
        DR_LOG(log_info) << "Blacklist::QuerySentinel - Error: connecting to " << endpoint.address() << " " << std::string( e.what() )  ;
        return false;
      }
    }

    static bool QueryRedis(
      std::string redisPassword,
      std::string redisKey,
      const boost::asio::ip::tcp::endpoint& endpoint,
      std::unordered_set<std::string>& ips
      ) {
      try {
        auto ip = endpoint.address().to_string();
        auto port = endpoint.port();
        redisContext* c = redisConnect(ip.c_str(), port);
        if (c == NULL || c->err) {
          if (c) {
            DR_LOG(log_error) << "Blacklist::QueryRedis - Error: connecting to " << endpoint.address() << " " << c->errstr ;
            redisFree(c);
          } else {
            DR_LOG(log_error) << "Blacklist::QueryRedis - Error: connecting to " << endpoint.address() << " can't allocate redis context" ;
          }
          return false;
        }

        if (redisPassword.length()) {
          redisReply *reply = (redisReply *) redisCommand(c, "AUTH %s", redisPassword.c_str());
          if (reply->type == REDIS_REPLY_ERROR) {
            DR_LOG(log_error) << "Blacklist::QueryRedis - AUTH failed to " << endpoint.address() << 
              " with password " << redisPassword << " :" << reply->str ;
            freeReplyObject(reply);
            redisFree(c);
            return false;
          }
          freeReplyObject(reply);
        }

        redisReply *reply = (redisReply *) redisCommand(c, "SMEMBERS %s", redisKey.c_str());
        if (reply == NULL) {
          DR_LOG(log_error) << "Blacklist::QueryRedis - Error: connecting to " << endpoint.address() << " " << c->errstr ;
          redisFree(c);
          return false;
        }
        if (c->err) {
          DR_LOG(log_error) << "Blacklist::QueryRedis - Error: querying redis " << endpoint.address() << " " << c->errstr ;
          freeReplyObject(reply);
          redisFree(c);
          return false;
        }
        if (reply->type != REDIS_REPLY_ARRAY) {
          if (reply->type == REDIS_REPLY_ERROR) {
            DR_LOG(log_error) << "Blacklist::QueryRedis - Redis error " << reply->str ;
          }
          else {
            DR_LOG(log_error) << "Blacklist::QueryRedis - Error: querying redis " << endpoint.address() << " unexpected reply type " << reply->type ;
          }
          freeReplyObject(reply);
          redisFree(c);
          return false;
        }

        DR_LOG(log_info) << "Blacklist::QueryRedis - got " << reply->elements << " IPs to blacklist" ;
        ips.clear();
        for (int i = 0; i < reply->elements; i++) {
          auto member = reply->element[i];
          ips.insert(member->str);
        }
        freeReplyObject(reply);
        redisFree(c);
        return true;
      } catch( std::exception& e) {
        DR_LOG(log_info) << "Blacklist::QueryRedis - Error: connecting to " << endpoint.address() << " " << std::string( e.what() )  ;
        return false;
      }
    }
    
    Blacklist::Blacklist(std::string& redisAddress, unsigned int redisPort,  std::string& redisPassword, std::string& redisKey, unsigned int refreshSecs) :
      m_redisKey(redisKey),
      m_refreshSecs(refreshSecs),
      m_redisAddress(redisAddress),
      m_redisPassword(redisPassword),
      m_redisPort(redisPort)
    {
    } 
    Blacklist::Blacklist(std::string& sentinels, std::string& masterName,std::string& redisPassword, std::string& redisKey, unsigned int refreshSecs) :
      m_redisKey(redisKey),
      m_refreshSecs(refreshSecs),
      m_redisPassword(redisPassword),
      m_sentinels(sentinels),
      m_masterName(masterName)
    {
    } 

    void Blacklist::start() {
        srand (time(NULL));    
        std::thread t(&Blacklist::threadFunc, this) ;
        m_thread.swap( t ) ;
    }

    Blacklist::~Blacklist() {
        stop() ;
    }
    void Blacklist::threadFunc() {
      bool initialized = false;
      DR_LOG(log_debug) << "Blacklist thread id: " << std::this_thread::get_id()  ;

      while (true) {
        unsigned int interval = m_refreshSecs;

       /**
        * @brief If we are using redis sentinels, query the sentinels for the read replicas
        * 
        */
        if (m_sentinels.length()) {
          auto result = parseIpPort(m_sentinels);
          for (const auto& entry : result) {
            std::string ip = std::get<0>(entry);
            unsigned int port = std::get<1>(entry);

            DR_LOG(log_notice) << "Blacklist::threadFunc - querying sentinel " << ip << ":" << port ;
            boost::system::error_code ec;
            boost::asio::ip::address ip_address = boost::asio::ip::address::from_string(ip, ec);
            if (ec.value() != 0) {
              /* must be a dns name */
              DR_LOG(log_debug) << "Blacklist resolving sentinel dns " << ip ;

              boost::asio::ip::tcp::resolver resolver(m_ioservice);
              boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(
                  ip, 
                  boost::lexical_cast<std::string>(port),
                  ec);
              for (boost::asio::ip::tcp::endpoint const& endpoint : results) {
                DR_LOG(log_debug) << "redis sentinel resolved to " << endpoint.address() ;
                if (QuerySentinel(m_masterName, endpoint, m_replicas)) break;
              }
            }
            else {
              boost::asio::ip::tcp::endpoint endpoint(ip_address, port);
              DR_LOG(log_debug) << "Connecting to sentinel at " << ip << ":" << port ;
              if (QuerySentinel(m_masterName, endpoint, m_replicas)) break;
            }
            if (m_replicas.size()) {
              DR_LOG(log_notice) << "got " << m_replicas.size() << " replicas to use" ;
              break;
            }
          }
        }

        /**
         * @brief Query the redis server we were given, or the replicas we found
         * 
         */
        if (!m_redisAddress.empty() || m_replicas.size() > 0) {    
          std::list<std::string> addresses = !m_replicas.empty() ?
            std::list<std::string>(m_replicas.begin(), m_replicas.end()) :
            std::list<std::string>{m_redisAddress + ":" + std::to_string(m_redisPort)};

          for (const auto& address : addresses) {
            /* get redis endpoint */
            DR_LOG(log_notice) << " querying redis at " << address ;
            auto [ip, port] = parseAddress(address);
            boost::system::error_code ec;
            boost::asio::ip::address ip_address = 
              boost::asio::ip::address::from_string(ip, ec);
            if (ec.value() != 0) {
              /* must be a dns name */
              DR_LOG(log_debug) << "Blacklist resolving " << ip ;

              boost::asio::ip::tcp::resolver resolver(m_ioservice);
              boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(
                  ip, 
                  boost::lexical_cast<std::string>(port),
                  ec);
              for (boost::asio::ip::tcp::endpoint const& endpoint : results) {
                DR_LOG(log_debug) << "Blacklist resolved to " << endpoint.address() ;
                if (QueryRedis(m_redisPassword, m_redisKey, endpoint, m_ips)) initialized = true;
                break;
              }
            }
            else {
              boost::asio::ip::tcp::endpoint endpoint(ip_address, port);
              DR_LOG(log_debug) << "Connecting to redis at " << ip << ":" << port ;
              if (QueryRedis(m_redisPassword, m_redisKey, endpoint, m_ips)) initialized = true;
            }
          }
          if (initialized) break;
        }
        else {
          DR_LOG(log_error) << "Blacklist::threadFunc - Error: no redis address or sentinels configured" ;
          break;
        }
        if (!initialized) interval = 60;
        std::this_thread::sleep_for (std::chrono::seconds(interval));
      }
   }

    void Blacklist::stop() {
      //m_thread.join() ;
    }

 }
