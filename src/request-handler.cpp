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
#include <iomanip>

#include <boost/bind/bind.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/asio.hpp>
#include <boost/assign/list_of.hpp>

#include <jansson.h>

#include "sofia-sip/url.h"

#include "request-handler.hpp"
#include "controller.hpp"

namespace {
  inline bool is_ssl_short_read_error(boost::system::error_code err) {
    return err.category() == boost::asio::error::ssl_category &&
      (err.value() == 335544539 || //short read error
      err.value() == 336130329); //decryption failed or bad record mac
  }

}


namespace drachtio {

  unsigned int RequestHandler::easyHandleCacheSize = 4 ;
  bool RequestHandler::instanceFlag = false;
  std::shared_ptr<RequestHandler> RequestHandler::single ;
  std::deque<CURL*> RequestHandler::m_cacheEasyHandles ;
  boost::object_pool<RequestHandler::ConnInfo> RequestHandler::m_pool(16, 256) ;

  static int multi_timer_cb(CURLM *multi, long timeout_ms, drachtio::RequestHandler::GlobalInfo *g);
  static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp);
  static void timer_cb(const boost::system::error_code & error, drachtio::RequestHandler::GlobalInfo *g);
  static int mcode_test(const char *where, CURLMcode code);
  static void check_multi_info(drachtio::RequestHandler::GlobalInfo *g);
  static void event_cb(drachtio::RequestHandler::GlobalInfo *g, curl_socket_t s,
                       int action, const boost::system::error_code & error,
                       int *fdp);
  static void remsock(int *f, drachtio::RequestHandler::GlobalInfo *g);
  static void setsock(int *fdp, curl_socket_t s, CURL *e, int act, int oldact,
                      drachtio::RequestHandler::GlobalInfo *g);
  static void addsock(curl_socket_t s, CURL *easy, int action, drachtio::RequestHandler::GlobalInfo *g);
  static size_t write_cb(void *ptr, size_t size, size_t nmemb, drachtio::RequestHandler::ConnInfo *conn);
  static size_t header_callback(char *buffer, size_t size, size_t nitems, drachtio::RequestHandler::ConnInfo *conn);
  static int prog_cb(void *p, double dltotal, double dlnow, double ult,
                     double uln);
  static curl_socket_t opensocket(void *clientp, curlsocktype purpose,
                                  struct curl_sockaddr *address);
  static int close_socket(void *clientp, curl_socket_t item);
  static int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, 
    RequestHandler::ConnInfo *conn);

  int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp) {
    std::shared_ptr<RequestHandler> p = RequestHandler::getInstance() ;
    RequestHandler::GlobalInfo *g = &(p->getGlobal()) ;

    int *actionp = (int *) sockp;
    static const char *whatstr[] = { "none", "IN", "OUT", "INOUT", "REMOVE"};

    if(what == CURL_POLL_REMOVE) {
      remsock(actionp, g);
    }
    else {
      if(!actionp) {
        addsock(s, e, what, g);
      }
      else {
        setsock(actionp, s, e, what, *actionp, g);
      }
    }
    return 0;  
  }
  void addsock(curl_socket_t s, CURL *easy, int action, drachtio::RequestHandler::GlobalInfo *g) {
    /* fdp is used to store current action */
    int *fdp = (int *) calloc(sizeof(int), 1);

    setsock(fdp, s, easy, action, 0, g);
    curl_multi_assign(g->multi, s, fdp);
  }
  void remsock(int *f, RequestHandler::GlobalInfo *g) {
    if(f) {
      free(f);
    }
  }

  void setsock(int *fdp, curl_socket_t s, CURL *e, int act, int oldact, drachtio::RequestHandler::GlobalInfo *g) {
    std::shared_ptr<RequestHandler> p = RequestHandler::getInstance() ;
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>& socket_map = p->getSocketMap() ;

    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>::iterator it =
      socket_map.find(s);

    if(it == socket_map.end()) {
      return;
    }

    boost::asio::ip::tcp::socket * tcp_socket = it->second;

    *fdp = act;

    if(act == CURL_POLL_IN) {
      if(oldact != CURL_POLL_IN && oldact != CURL_POLL_INOUT) {
        tcp_socket->async_read_some(boost::asio::null_buffers(),
                                    boost::bind(&event_cb, g, s,
                                                CURL_POLL_IN, boost::placeholders::_1, fdp));
      }
    }
    else if(act == CURL_POLL_OUT) {
      if(oldact != CURL_POLL_OUT && oldact != CURL_POLL_INOUT) {
        tcp_socket->async_write_some(boost::asio::null_buffers(),
                                     boost::bind(&event_cb, g, s,
                                                 CURL_POLL_OUT, boost::placeholders::_1, fdp));
      }
    }
    else if(act == CURL_POLL_INOUT) {
      if(oldact != CURL_POLL_IN && oldact != CURL_POLL_INOUT) {
        tcp_socket->async_read_some(boost::asio::null_buffers(),
                                    boost::bind(&event_cb, g, s,
                                                CURL_POLL_IN, boost::placeholders::_1, fdp));
      }
      if(oldact != CURL_POLL_OUT && oldact != CURL_POLL_INOUT) {
        tcp_socket->async_write_some(boost::asio::null_buffers(),
                                     boost::bind(&event_cb, g, s,
                                                 CURL_POLL_OUT, boost::placeholders::_1, fdp));
      }
    }
  }

  int multi_timer_cb(CURLM *multi, long timeout_ms, drachtio::RequestHandler::GlobalInfo *g) {
    std::shared_ptr<RequestHandler> p = RequestHandler::getInstance() ;
    boost::asio::deadline_timer& timer = p->getTimer() ;

    /* cancel running timer */
    timer.cancel();

    if(timeout_ms > 0) {
      /* update timer */
      timer.expires_from_now(boost::posix_time::millisec(timeout_ms));
      timer.async_wait(boost::bind(&timer_cb, boost::placeholders::_1, g));
    }
    else if(timeout_ms == 0) {
      /* call timeout function immediately */
      boost::system::error_code error; /*success*/
      timer_cb(error, g);
    }

    return 0;
  }
  int mcode_test(const char *where, CURLMcode code) {
    if(CURLM_OK != code) {
      const char *s;
      switch(code) {
      case CURLM_CALL_MULTI_PERFORM:
        s = "CURLM_CALL_MULTI_PERFORM";
        break;
      case CURLM_BAD_HANDLE:
        s = "CURLM_BAD_HANDLE";
        break;
      case CURLM_BAD_EASY_HANDLE:
        s = "CURLM_BAD_EASY_HANDLE";
        break;
      case CURLM_OUT_OF_MEMORY:
        s = "CURLM_OUT_OF_MEMORY";
        break;
      case CURLM_INTERNAL_ERROR:
        s = "CURLM_INTERNAL_ERROR";
        break;
      case CURLM_UNKNOWN_OPTION:
        s = "CURLM_UNKNOWN_OPTION";
        break;
      case CURLM_LAST:
        s = "CURLM_LAST";
        break;
      default:
        s = "CURLM_unknown";
        break;
      case CURLM_BAD_SOCKET:
        s = "CURLM_BAD_SOCKET";
        break;
      }

      DR_LOG(log_debug) << "RequestHandler::mcode_test ERROR: " << where << " returns " << s;

      return -1;
    }
    return 0 ;
  }

  /* Check for completed transfers, and remove their easy handles */
  void check_multi_info(drachtio::RequestHandler::GlobalInfo *g) {
    char *eff_url;
    CURLMsg *msg;
    int msgs_left;
    RequestHandler::ConnInfo *conn;
    CURL *easy;
    CURLcode res;

    while((msg = curl_multi_info_read(g->multi, &msgs_left))) {
      if(msg->msg == CURLMSG_DONE) {
        long response_code;
        double namelookup=0, connect=0, total=0 ;
        char *ct = NULL ;
        easy = msg->easy_handle;
        res = msg->data.result;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
        curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &ct);

        curl_easy_getinfo(easy, CURLINFO_NAMELOOKUP_TIME, &namelookup);
        curl_easy_getinfo(easy, CURLINFO_CONNECT_TIME, &connect);
        curl_easy_getinfo(easy, CURLINFO_TOTAL_TIME, &total);

        DR_LOG(log_info) << "http " << response_code << " response received from server in " << dec <<
          std::setprecision(3) << total << " secs: " << conn->response;

        //notify controller
        theOneAndOnlyController->httpCallRoutingComplete(conn->transactionId, response_code, conn->response) ;

        // return easy handle to cache
        {
          //alloc and free happen in the same thread
          //std::lock_guard<std::mutex> l( m_lock ) ;
          RequestHandler::m_cacheEasyHandles.push_back(easy) ;
          DR_LOG(log_debug) << "RequestHandler::makeRequestForRoute - after returning handle  in thread" << 
            std::this_thread::get_id() << " " << dec <<
            RequestHandler::m_cacheEasyHandles.size() << " handles are available in cache";
        }

        curl_multi_remove_handle(g->multi, easy);
        
        if( conn->hdr_list ) curl_slist_free_all(conn->hdr_list);

        memset(conn, 0, sizeof(RequestHandler::ConnInfo));
        RequestHandler::m_pool.destroy(conn) ;
        //free(conn);
      }
    }
  }

  /* Called by asio when there is an action on a socket */
  void event_cb(drachtio::RequestHandler::GlobalInfo *g, curl_socket_t s,
                       int action, const boost::system::error_code & error,
                       int *fdp) {
    int f = *fdp;
    std::shared_ptr<RequestHandler> p = RequestHandler::getInstance() ;
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>& socket_map = p->getSocketMap() ;
    boost::asio::deadline_timer& timer = p->getTimer() ;

    if(socket_map.find(s) == socket_map.end()) {
      DR_LOG(log_error) << "event_cb: socket already closed";
      return;
    }

    /* make sure the event matches what are wanted */
    if(f == action || f == CURL_POLL_INOUT) {
      CURLMcode rc;
      if(error)
        action = CURL_CSELECT_ERR;
      rc = curl_multi_socket_action(g->multi, s, action, &g->still_running);

      mcode_test("event_cb: curl_multi_socket_action", rc);
      check_multi_info(g);

      if(g->still_running <= 0) {
        timer.cancel();
      }

      /* keep on watching.
       * the socket may have been closed and/or fdp may have been changed
       * in curl_multi_socket_action(), so check them both */
      if(!error && socket_map.find(s) != socket_map.end() &&
         (f == action || f == CURL_POLL_INOUT)) {
        boost::asio::ip::tcp::socket *tcp_socket = socket_map.find(s)->second;

        if(action == CURL_POLL_IN) {
          tcp_socket->async_read_some(boost::asio::null_buffers(),
                                      boost::bind(&event_cb, g, s,
                                                  action, boost::placeholders::_1, fdp));
        }
        if(action == CURL_POLL_OUT) {
          tcp_socket->async_write_some(boost::asio::null_buffers(),
                                       boost::bind(&event_cb, g, s,
                                                   action, boost::placeholders::_1, fdp));
        } 
      }
    }
  }

  /* CURLOPT_PROGRESSFUNCTION */
  int prog_cb(void *p, double dltotal, double dlnow, double ult,
                     double uln) {
    drachtio::RequestHandler::ConnInfo *conn = (drachtio::RequestHandler::ConnInfo *)p;

    (void)ult;
    (void)uln;

    DR_LOG(log_debug) << "Progress: " << conn->url << " (" << dlnow << "/" << dltotal << ")";
    DR_LOG(log_debug) << "Progress: " << conn->url << " (" << ult << ")";

    return 0;
  }

  /* Called by asio when our timeout expires */
  void timer_cb(const boost::system::error_code & error, drachtio::RequestHandler::GlobalInfo *g)
  {
    if(!error) {
      CURLMcode rc;
      rc = curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0,
                                    &g->still_running);

      mcode_test("timer_cb: curl_multi_socket_action", rc);
      check_multi_info(g);
    }
  }

  /* CURLOPT_WRITEFUNCTION */
  size_t write_cb(void *ptr, size_t size, size_t nmemb, RequestHandler::ConnInfo *conn) {
    size_t written = size * nmemb;
    size_t needed = strlen(conn->response) + written ;
    if( needed > HTTP_BODY_LEN ) {
      DR_LOG(log_error) << "RequestHandler::write_cb total length of response " << needed << 
        " exceeds buffer size";
      return 0 ;
    }
    else {
      strncat( conn->response, (const char *) ptr, written) ;
    }
    return written;
  }

  size_t header_callback(char *buffer, size_t size, size_t nitems, RequestHandler::ConnInfo *conn) {
    size_t written = size * nitems;
    string header(buffer, written);
    DR_LOG(log_debug) << header ;
    return written;
  }

  /* CURLOPT_OPENSOCKETFUNCTION */
  curl_socket_t opensocket(void *clientp, curlsocktype purpose,
                                  struct curl_sockaddr *address) {
    std::shared_ptr<RequestHandler> p = drachtio::RequestHandler::getInstance() ;
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>& socket_map = p->getSocketMap() ;
    boost::asio::io_service& io_service = p->getIOService() ;

    curl_socket_t sockfd = CURL_SOCKET_BAD;

    /* restrict to IPv4 */
    if(purpose == CURLSOCKTYPE_IPCXN && address->family == AF_INET) {
      /* create a tcp socket object */
      boost::asio::ip::tcp::socket *tcp_socket =
        new boost::asio::ip::tcp::socket(io_service);

      /* open it and get the native handle*/
      boost::system::error_code ec;
      tcp_socket->open(boost::asio::ip::tcp::v4(), ec);

      if(ec) {
        /* An error occurred */
        DR_LOG(log_error) << "Couldn't open socket [" << ec << "][" << ec.message() << "]";
      }
      else {
        sockfd = tcp_socket->native_handle();

        /* save it for monitoring */
        socket_map.insert(std::pair<curl_socket_t,
                          boost::asio::ip::tcp::socket *>(sockfd, tcp_socket));
      }
    }

    return sockfd;
  }

  /* CURLOPT_CLOSESOCKETFUNCTION */
  int close_socket(void *clientp, curl_socket_t item) {
    //DR_LOG(log_debug) <<"close_socket : " << hex << item;

    std::shared_ptr<RequestHandler> p = drachtio::RequestHandler::getInstance() ;
    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>& socket_map = p->getSocketMap() ;

    std::map<curl_socket_t, boost::asio::ip::tcp::socket *>::iterator it =
      socket_map.find(item);

    if(it != socket_map.end()) {
      delete it->second;
      socket_map.erase(it);
    }

    return 0;
  }
  int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, 
    drachtio::RequestHandler::ConnInfo *conn) {

    return 0 ;
  }

  RequestHandler::RequestHandler( DrachtioController* pController ) :
      m_pController( pController ), m_timer(m_ioservice) {
          
      memset(&m_g, 0, sizeof(GlobalInfo));
      m_g.multi = curl_multi_init();

      assert(m_g.multi);

      curl_multi_setopt(m_g.multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
      curl_multi_setopt(m_g.multi, CURLMOPT_SOCKETDATA, &m_g);
      curl_multi_setopt(m_g.multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
      curl_multi_setopt(m_g.multi, CURLMOPT_TIMERDATA, &m_g);

      std::thread t(&RequestHandler::threadFunc, this) ;
      m_thread.swap( t ) ;
  }
  RequestHandler::~RequestHandler() {
    curl_multi_cleanup(m_g.multi);
  }
  void RequestHandler::threadFunc() {
               
    /* to make sure the event loop doesn't terminate when there is no work to do */
    boost::asio::io_service::work work(m_ioservice);
    
    for(;;) {
        
      try {
        m_ioservice.run() ;
        break ;
      }
      catch( std::exception& e) {
        DR_LOG(log_error) << "RequestHandler::threadFunc - Error in event thread: " << string( e.what() )  ;
      }
    }
  }

  /* Create a new easy handle, and add it to the global curl_multi */
  void RequestHandler::startRequest(const string& transactionId, 
    const string& httpMethod, const string& url, const string& body, bool verifyPeer) {

    RequestHandler::ConnInfo *conn;
    CURLMcode rc;

    if (0 == url.find("tcp://") || 0 == url.find("tls://")) {
      string json = "{\"action\": \"route\", \"data\": {\"uri\": \"";
      json.append(url.substr(6));
      if (0 == url.find("tcp://")) json.append(";transport=tcp");
      else json.append(";transport=tls");
      json.append("\"}}");
      DR_LOG(log_info) << "RequestHandler::startRequest: no web callback required, sending directly to " << url << ":" << json.c_str() ;
      m_pController->httpCallRoutingComplete(transactionId, 200, json);
      return;
    }


    DR_LOG(log_info) << "RequestHandler::startRequest: sending http " << httpMethod << ": " << url ;

    conn = m_pool.malloc() ;
    CURL* easy = NULL ;
    {
      //alloc and free happen in the same thread
      //std::lock_guard<std::mutex> l( m_lock ) ;
      if( m_cacheEasyHandles.empty() ) {
        m_cacheEasyHandles.push_back(createEasyHandle()) ;
      }
      easy = m_cacheEasyHandles.front() ;
      m_cacheEasyHandles.pop_front() ;
      DR_LOG(log_debug) << "RequestHandler::makeRequestForRoute - after acquiring handle in thread " << 
        std::this_thread::get_id() << " " << dec <<
        m_cacheEasyHandles.size() << " handles remain in cache";
    }

    conn->easy = easy;

    conn->global = &m_g;
    strncpy(conn->url, url.c_str(), URL_LEN) ;
    strncpy(conn->body, body.c_str(), HTTP_BODY_LEN);
    strncpy(conn->transactionId, transactionId.c_str(), TXNID_LEN);
    conn->hdr_list = NULL ;
    *conn->response = '\0' ;

    curl_easy_setopt(easy, CURLOPT_URL, conn->url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, conn);
    //curl_easy_setopt(easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
    //curl_easy_setopt(easy, CURLOPT_PROGRESSDATA, conn);
    //curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 3L);
    //curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
    //curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, debug_callback);
    //curl_easy_setopt(easy, CURLOPT_DEBUGDATA, &conn);
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 1L);
    //curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_callback);
    //curl_easy_setopt(easy, CURLOPT_HEADERDATA, conn);

    
    /* call this function to get a socket */
    curl_easy_setopt(easy, CURLOPT_OPENSOCKETFUNCTION, opensocket);

    /* call this function to close a socket */
    curl_easy_setopt(easy, CURLOPT_CLOSESOCKETFUNCTION, close_socket);

    if( 0 == url.find("https:") ) {
      curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, verifyPeer);
    }

    conn->hdr_list = curl_slist_append(conn->hdr_list, "Accept: application/json");
    
    if( 0 == httpMethod.compare("POST") ) {
      curl_easy_setopt(easy, CURLOPT_POSTFIELDS, conn->body);
      curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, strlen(conn->body));
      conn->hdr_list = curl_slist_append(conn->hdr_list, "Content-Type: text/plain; charset=UTF-8");
    }
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, conn->hdr_list);

    rc = curl_multi_add_handle(m_g.multi, conn->easy);
    mcode_test("new_conn: curl_multi_add_handle", rc);

    /* note that the add_handle() will set a time-out to trigger very soon so
       that the necessary socket_action() call will be called by this app */
  }

  CURL* RequestHandler::createEasyHandle(void) {
    CURL* easy = curl_easy_init();
    if(!easy) {
      DR_LOG(log_error) << "curl_easy_init() failed, exiting!";
      throw new std::runtime_error("curl_easy_init() failed");
    }  

    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "Drachtio/" DRACHTIO_VERSION);

    // set connect timeout to 2 seconds and total timeout to 3 seconds
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 3L);

    return easy ;    
  }

  // public API

  std::shared_ptr<RequestHandler> RequestHandler::getInstance() {
    if(!instanceFlag) {

      for( int i = 0; i < easyHandleCacheSize; i++ ) {
        CURL* easy = createEasyHandle() ;
        m_cacheEasyHandles.push_back( easy ) ;
      }

      single.reset(new RequestHandler(theOneAndOnlyController));
      instanceFlag = true;
      return single;
    }
    else {
      return single;
    }
  }  

  void RequestHandler::makeRequestForRoute(const string& transactionId, const string& httpMethod, 
    const string& httpUrl, const string& body, bool verifyPeer) {

    m_ioservice.post( std::bind(&RequestHandler::startRequest, this, transactionId, httpMethod, httpUrl, body, verifyPeer)) ;
  }
 }
