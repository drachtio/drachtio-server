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
#include <iostream>

#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "redis-service.hpp"
#include "controller.hpp"

static const std::string redisKey = "unique-redis-key-example";
static const std::string redisValue = "unique-redis-value";

namespace drachtio {
     
    RedisService::RedisService( DrachtioController* pController, const string& address, unsigned int port ) :
        m_pController( pController ), m_timer(m_ioservice), m_redisAddress(address), m_redisPort(port), m_bConnected(false) {
         
        m_pRedis = boost::make_shared<RedisClient>(boost::ref(m_ioservice)) ;
        boost::thread t(&RedisService::threadFunc, this) ;
        m_thread.swap( t ) ;            
    }
    RedisService::~RedisService() {
        this->stop() ;
    }
    void RedisService::handleConnected(bool ok, const std::string &errmsg) {
        /* requires compiling with  -std=c++11
        auto const local_function = [&](const RedisValue &v) {
          std::cerr << "SET: " << v.toString() << std::endl;
        };      
        */
        if( ok ) {
            m_bConnected = true ;
            DR_LOG(log_info) << "RedisService: successfully connected to redis at " << m_redisAddress << ":" << m_redisPort ;
             //redis.command("SET", redisKey, redisValue, local_function ) ;
            /*
            redis.command("SET", redisKey, redisValue, [&](const RedisValue &v) {
                std::cerr << "SET: " << v.toString() << std::endl;
                redis.command("GET", redisKey, [&](const RedisValue &v) {
                    std::cerr << "GET: " << v.toString() << std::endl;
                    redis.command("DEL", redisKey, [&](const RedisValue &) {
                        ioService.stop();
                    });
                });
            });
            */
        }
        else {
            DR_LOG(log_info) << "RedisService: failed connecting to redis at " << m_redisAddress << ":" << m_redisPort << ": " << errmsg ;

            //try again in 5 secs
            m_timer.expires_from_now(boost::posix_time::seconds(5)) ;
            m_timer.async_wait(boost::bind(&RedisService::retryConnect, this, boost::asio::placeholders::error));
        }
    }    
    void RedisService::threadFunc() {
        
        DR_LOG(log_debug) << "RedisService thread id: " << boost::this_thread::get_id()  ;
         
        /* to make sure the event loop doesn't terminate when there is no work to do */
        boost::asio::io_service::work work(m_ioservice);

        connect() ;
        
        for(;;) {
            
            try {
                DR_LOG(log_notice) << "RedisService: io_service run loop started"  ;
                m_ioservice.run() ;
                DR_LOG(log_notice) << "RedisService: io_service run loop ended normally"  ;
                break ;
            }
            catch( std::exception& e) {
                DR_LOG(log_error) << "Error in event thread: " << string( e.what() )  ;
                break ;
            }
        }
    }
    void RedisService::retryConnect( const boost::system::error_code& e ) {
        if( e ) {
            DR_LOG(log_error) << "RedisService::retryConnect - error: " << e.message() ;
            return ;
        }
        connect() ;
    }
    void RedisService::connect() {
        assert(!m_bConnected) ;

        boost::asio::ip::address address = boost::asio::ip::address::from_string(m_redisAddress);
        m_pRedis->asyncConnect(address, m_redisPort,
            boost::bind(&RedisService::handleConnected, this, _1, _2));


    }
    void RedisService::stop() {
        m_ioservice.stop() ;
        m_thread.join() ;
    }

 }
