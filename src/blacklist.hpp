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
#ifndef __BLACKLIST_H__
#define __BLACKLIST_H__


#include <boost/asio.hpp>

#include <unordered_set>
#include <thread>

#include "drachtio.h"

namespace drachtio {
    
  class Blacklist {
  public:
    Blacklist(string& redisKey, unsigned int refreshSecs = 3600) : m_redisKey(redisKey), m_refreshSecs(refreshSecs) {
    }
    ~Blacklist(){};

    void start(void);

    void redisAddress(std::string& address, std::string& port) {
      m_redisAddress = address;
      m_redisPort = port;
    }
    void sentinelAddresses(std::string& addresses) {
      m_redisSentinels = addresses;
    }
    void redisServiceName(std::string& serviceName) {
      m_redisServiceName = serviceName;
    }
    void username(std::string& username) {
      m_redisUsername = username;
    }
    void password(std::string& password) {
      m_redisPassword = password;
    }
    void threadFunc(void) ;
    bool isBlackListed(const char* srcAddress) {
      return m_ips.end() != m_ips.find(srcAddress);
    }

  private:
    void querySentinels(std::string& ip, std::string& port);

    std::string                     m_redisAddress;
    std::string                     m_redisPort;
    std::string&                    m_redisKey; 
    unsigned int                    m_refreshSecs;
    std::string                     m_redisSentinels;
    std::string                     m_redisServiceName;
    std::string                     m_redisUsername;
    std::string                     m_redisPassword;
    std::unordered_set<std::string> m_ips ;      
    std::thread                     m_thread ;
  } ;
}

#endif
