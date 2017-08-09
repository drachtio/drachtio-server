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


#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/unordered_set.hpp>
#include <boost/asio/ssl.hpp>

#include <curl/curl.h>

#include "drachtio.h"

#define TXNID_SIZE 255

using boost::asio::ip::tcp;

using namespace std ;

namespace drachtio {
    
  class RequestHandler : public boost::enable_shared_from_this<RequestHandler>  {
  public:

    typedef struct _GlobalInfo {
        CURLM *multi;
        int still_running;
    } GlobalInfo;

    /* Information associated with a specific easy handle */
    typedef struct _ConnInfo {
        CURL *easy;
        string url;
        string body ;
        string transactionId;
        string response ;
        struct curl_slist *hdr_list;
        GlobalInfo *global;
        char error[CURL_ERROR_SIZE];
    } ConnInfo;

    static boost::shared_ptr<RequestHandler> getInstance();

    ~RequestHandler() ;

    void makeRequestForRoute(const string& transactionId, const string& httpMethod, 
      const string& httpUrl, const string& body, bool verifyPeer = true) ;

    void threadFunc(void) ;

  protected:
    GlobalInfo& getGlobal(void) { return m_g; }
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>& getSocketMap(void) { return m_socket_map; }
    boost::asio::deadline_timer& getTimer(void) { return m_timer; }
    boost::asio::io_service& getIOService(void) { return m_ioservice; }

    static int multi_timer_cb(CURLM *multi, long timeout_ms, GlobalInfo *g);
    static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp);
    static void timer_cb(const boost::system::error_code & error, GlobalInfo *g);
    static int mcode_test(const char *where, CURLMcode code);
    static void check_multi_info(GlobalInfo *g);
    static void event_cb(GlobalInfo *g, curl_socket_t s,
                         int action, const boost::system::error_code & error,
                         int *fdp);
    static void remsock(int *f, GlobalInfo *g);
    static void setsock(int *fdp, curl_socket_t s, CURL *e, int act, int oldact,
                        GlobalInfo *g);
    static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g);
    static size_t write_cb(void *ptr, size_t size, size_t nmemb, ConnInfo *conn);
    static size_t header_callback(char *buffer, size_t size, size_t nitems, ConnInfo *conn);
    static int prog_cb(void *p, double dltotal, double dlnow, double ult,
                       double uln);
    static curl_socket_t opensocket(void *clientp, curlsocktype purpose,
                                    struct curl_sockaddr *address);
    static int close_socket(void *clientp, curl_socket_t item);
    static int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, 
      RequestHandler::ConnInfo *conn);

    void startRequest(const string& transactionId, const string& httpMethod, 
      const string& url, const string& body, bool verifyPeer);

  private:
    // NB: this is a singleton object, accessed via the static getInstance method
    RequestHandler( DrachtioController* pController ) ;
    static CURL* createEasyHandle(void) ;

    static bool               instanceFlag;
    static boost::shared_ptr<RequestHandler> single;
    static unsigned int       easyHandleCacheSize ;
    static std::deque<CURL*>   m_cacheEasyHandles ;
    static boost::mutex        m_lock ;

    DrachtioController*         m_pController ;
    boost::thread               m_thread ;

    boost::asio::io_service     m_ioservice;

    boost::asio::deadline_timer m_timer ;
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *> m_socket_map;


    GlobalInfo                  m_g ;
  } ;
}  

#endif
