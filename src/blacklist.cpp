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

    static bool QueryRedis(
      boost::asio::io_context& ioservice,
      std::string redisKey,
      const boost::asio::ip::tcp::endpoint& endpoint,
      std::unordered_set<std::string>& ips
      ) {
      try {
        Buffer rx_buff;
        socket_t socket(ioservice, endpoint.protocol());
        DR_LOG(log_debug) << "Blacklist: connecting to " << endpoint.address() ;
        socket.connect(endpoint) ;
        DR_LOG(log_debug) << "Blacklist: successfully connected to redis" ;

        r::Connection<socket_t> c(std::move(socket));
        c.write(r::single_command_t{"SMEMBERS", redisKey});
        auto r = c.read(rx_buff);
        auto extract = boost::apply_visitor(r::extractor<Iterator>(), r.result);
        rx_buff.consume(r.consumed);
        auto &reply = boost::get<r::extracts::array_holder_t>(extract);
        DR_LOG(log_info) << "Blacklist: got " << reply.elements.size() << " IPs to blacklist" ;
        ips.clear();
        BOOST_FOREACH(auto& member, reply.elements) {
          auto &reply_str = boost::get<r::extracts::string_t>(member);
          ips.insert(reply_str.str);
        }
        c.next_layer().close();
        return true;
      } catch( std::exception& e) {
        DR_LOG(log_info) << "Blacklist::QueryRedis - Error: connecting to " << endpoint.address() << " " << std::string( e.what() )  ;
        return false;
      }
    }
    
    Blacklist::Blacklist(std::string& redisAddress, unsigned int redisPort, std::string& redisKey, unsigned int refreshSecs) :
      m_redisKey(redisKey),
      m_refreshSecs(refreshSecs),
      m_redisAddress(redisAddress),
      m_redisPort(redisPort)
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

        /* get redis endpoint */
        boost::system::error_code ec;
        boost::asio::ip::address ip_address = 
          boost::asio::ip::address::from_string(m_redisAddress, ec);
        if (ec.value() != 0) {
          /* must be a dns name */
          DR_LOG(log_debug) << "Blacklist resolving " << m_redisAddress ;

          boost::asio::ip::tcp::resolver resolver(m_ioservice);
          boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(
              m_redisAddress, 
              boost::lexical_cast<std::string>(m_redisPort),
              ec);
          for (boost::asio::ip::tcp::endpoint const& endpoint : results) {
            DR_LOG(log_debug) << "Blacklist resolved to " << endpoint.address() ;
            if (QueryRedis(m_ioservice, m_redisKey, endpoint, m_ips)) initialized = true;
            break;
          }
        }
        else {
          boost::asio::ip::tcp::endpoint endpoint(ip_address, m_redisPort);
          DR_LOG(log_debug) << "Connecting to redis at " << m_redisAddress << ":" << m_redisPort ;
          if (QueryRedis(m_ioservice, m_redisKey, endpoint, m_ips)) initialized = true;
        }
        if (!initialized) interval = 60;
        std::this_thread::sleep_for (std::chrono::seconds(interval));
      }
   }

    void Blacklist::stop() {
      //m_thread.join() ;
    }

 }
