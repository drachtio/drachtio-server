/*
Copyright (c) 2020, David C Horton

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
#ifndef __DNS_RESOLVER_H__
#define __DNS_RESOLVER_H__

#include <functional>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <string>

#include <boost/asio.hpp>

#include <sofia-sip/sresolv.h>
#include "sofia-sip/url.h"

namespace drachtio {

  class ClientController;

  class DnsResolver : public std::enable_shared_from_this<DnsResolver>  {
  public:
    DnsResolver(std::shared_ptr<ClientController> pClientController);
    ~DnsResolver();

    DnsResolver(const DnsResolver&) = delete;
    DnsResolver& operator=(const DnsResolver&) = delete;

    typedef unsigned QueryId_t;
    typedef struct Result {
      std::string ip;
      std::string target;
      std::string name;
      uint16_t port;
      std::string transport;
      uint16_t weight;
      uint16_t priority;

      Result() : weight(0), priority(0), port(0) {}
    } Result_t;
    typedef std::deque<Result_t> Results_t;

    void start(void);
    void stop(void);
    void threadFunc(void) ;
    bool resolve(const std::string& clientMsgId, const std::string& startLine, const std::string& headers, 
            const std::string& body, std::string& transactionId, std::string& dialogId, std::string& routeUrl);
    void queryDone(QueryId_t id, url_t* uri, const Results_t& results);
    sres_resolver_t* resolver(void) { return m_resolver; }
    
  private:
    class QueryInProgress{
    public:
      QueryInProgress(std::shared_ptr<DnsResolver> resolver, QueryId_t id, url_t* uri, const char* tport, 
        bool resolvingRouteUrl, const std::string& clientMsgId, const std::string& startLine, const std::string& headers, 
        const std::string& body, std::string& transactionId, std::string& dialogId, std::string& routeUrl) ;
      ~QueryInProgress();

      void processAnswers(sres_query_t *query, sres_record_t *answers[]);

      const std::string& clientMsgId(void) { return m_clientMsgId;}
      const std::string& startLine(void) { return m_startLine;}
      const std::string& headers(void) { return m_headers;}
      const std::string& body(void) { return m_body;}
      const std::string& transactionId(void) { return m_transactionId;}
      const std::string& dialogId(void) { return m_dialogId;}
      const std::string& routeUrl(void) { return m_routeUrl;}
      bool isResolvingRouteUrl(void) { return m_resolvingRouteUrl;}
      const Results_t& results(void) { return m_results; }

    private:
      std::shared_ptr<DnsResolver>        m_parent;
      QueryId_t                           m_id;
      url_t*                              m_uri;
      bool                                m_resolvingRouteUrl;
      Results_t                           m_results;
      std::unordered_set<sres_query_t*>   m_queries;
      std::string                         m_clientMsgId;
      std::string                         m_startLine;
      std::string                         m_headers;
      std::string                         m_body;
      std::string                         m_transactionId;
      std::string                         m_dialogId;
      std::string                         m_routeUrl;      
    };

		typedef std::unordered_map< QueryId_t, std::shared_ptr<QueryInProgress> > mapId2Query_t;

    bool replaceDnsName(std::string& u, const Result_t& result);

    static QueryId_t                    m_id;
    std::shared_ptr<ClientController>   m_pClientController;
    std::thread                         m_thread ;
    mapId2Query_t                       m_queries;

    // singleton resolver
    sres_resolver_t                     *m_resolver;

    // we use one socket
    sres_socket_t                       *m_sockets;
    struct pollfd                       *m_pollfds;
    bool                                 m_done;

  };


}

#endif
