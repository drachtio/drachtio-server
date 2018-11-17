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
#ifndef __REQUEST_HANDLER_H__
#define __REQUEST_HANDLER_H__

#include <thread>
#include <unordered_set>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/pool/object_pool.hpp>
#include <curl/curl.h>

#include "drachtio.h"

#define TXNID_LEN (255)
#define URL_LEN (1024)
#define HTTP_BODY_LEN (16384)

using boost::asio::ip::tcp;

using namespace std ;

namespace drachtio {
    
  class RequestHandler : public std::enable_shared_from_this<RequestHandler>  {
  public:

    typedef struct _GlobalInfo {
        CURLM *multi;
        int still_running;
    } GlobalInfo;

    /* Information associated with a specific easy handle */
    typedef struct _ConnInfo {
      _ConnInfo() {
        memset(this, 0, sizeof(ConnInfo));
      }

      CURL *easy;
      char url[URL_LEN+1];
      char body[HTTP_BODY_LEN+1];
      char transactionId[TXNID_LEN+1];
      char response[HTTP_BODY_LEN+1] ;
      struct curl_slist *hdr_list;
      GlobalInfo *global;
      char error[CURL_ERROR_SIZE];
    } ConnInfo;

    static std::shared_ptr<RequestHandler> getInstance();

    ~RequestHandler() ;

    void makeRequestForRoute(const string& transactionId, const string& httpMethod, 
      const string& httpUrl, const string& body, bool verifyPeer = true) ;

    void threadFunc(void) ;
    GlobalInfo& getGlobal(void) { return m_g; }
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>& getSocketMap(void) { return m_socket_map; }
    boost::asio::deadline_timer& getTimer(void) { return m_timer; }
    boost::asio::io_service& getIOService(void) { return m_ioservice; }

    static std::deque<CURL*>   m_cacheEasyHandles ;
    static boost::object_pool<ConnInfo> m_pool ;

  protected:

    void startRequest(const string& transactionId, const string& httpMethod, 
      const string& url, const string& body, bool verifyPeer);

  private:
    // NB: this is a singleton object, accessed via the static getInstance method
    RequestHandler( DrachtioController* pController ) ;
    static CURL* createEasyHandle(void) ;

    static bool               instanceFlag;
    static std::shared_ptr<RequestHandler> single;
    static unsigned int       easyHandleCacheSize ;
    //static std::mutex        m_lock ;

    DrachtioController*         m_pController ;
    std::thread               m_thread ;

    boost::asio::io_service     m_ioservice;

    boost::asio::deadline_timer m_timer ;
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *> m_socket_map;


    GlobalInfo                  m_g ;
  } ;
}  

#endif
