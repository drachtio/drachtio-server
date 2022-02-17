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

#include "bredis.hpp"

#include "blacklist.hpp"
#include "controller.hpp"

namespace r = bredis;
using socket_t = boost::asio::ip::tcp::socket;
using Buffer = boost::asio::streambuf;
using Iterator = typename r::to_iterator<Buffer>::iterator_t;
using Policy = r::parsing_policy::keep_result;
using result_t = r::parse_result_mapper_t<Iterator, Policy>;

namespace drachtio {
    
    Blacklist::Blacklist(string& redisAddress, unsigned int redisPort, string& redisKey, unsigned int refreshSecs) :
      m_redisKey(redisKey),
      m_refreshSecs(refreshSecs),
      m_endpoint(boost::asio::ip::make_address(redisAddress), redisPort)
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
        try {
          Buffer rx_buff;
          socket_t socket(m_ioservice, m_endpoint.protocol());
          socket.connect(m_endpoint) ;
          DR_LOG(log_debug) << "Blacklist: successfully connected to redis" ;

          r::Connection<socket_t> c(std::move(socket));
          c.write(r::single_command_t{"SMEMBERS", m_redisKey});
          auto r = c.read(rx_buff);
          auto extract = boost::apply_visitor(r::extractor<Iterator>(), r.result);
          rx_buff.consume(r.consumed);
          auto &reply = boost::get<r::extracts::array_holder_t>(extract);
          DR_LOG(log_info) << "Blacklist: got " << reply.elements.size() << " IPs to blacklist" ;
          m_ips.clear();
          BOOST_FOREACH(auto& member, reply.elements) {
            auto &reply_str = boost::get<r::extracts::string_t>(member);
            m_ips.insert(reply_str.str);
          }
          initialized = true;
          socket.close();
        } catch( std::exception& e) {
          if (!initialized) interval = 60;
          DR_LOG(log_error) << "Blacklist::threadFunc - Error in thread: " << string( e.what() )  ;
        }
        std::this_thread::sleep_for (std::chrono::seconds(m_refreshSecs));
      }
   }

    void Blacklist::stop() {
      //m_thread.join() ;
    }

 }
