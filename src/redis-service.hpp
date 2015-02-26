/*
Copyright (c) 2013, David C Horton

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
#ifndef __REDIS_CLIENT_H__
#define __REDIS_CLIENT_H__

#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include <redisclient.h>

#include "drachtio.h"

namespace drachtio {
    
    class RedisService : boost::noncopyable {
    public:
        RedisService( DrachtioController* pController, const string& address, unsigned int port ) ;
        ~RedisService() ;
        
        void threadFunc(void) ;
        void connect() ;
        void retryConnect(const boost::system::error_code& e) ;
        void stop() ;
        bool isConnected(void) { return m_bConnected; }

        void handleConnected(bool ok, const std::string &errmsg) ;

    private:

        boost::shared_ptr<RedisClient>      m_pRedis ;
        DrachtioController*         m_pController ;
        boost::thread               m_thread ;
        boost::mutex                m_lock ;
        string                      m_redisAddress ;
        unsigned int                m_redisPort ;
        bool                        m_bConnected ;

        boost::asio::io_service m_ioservice;
        boost::asio::deadline_timer m_timer ;
        
    } ;

}  



#endif
