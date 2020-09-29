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
#include "dns-resolver.hpp"

#include "sofia-sip/su.h"
#include "sofia-sip/sip.h"

#include "sofia-resolv/sres.h"
#include "sofia-resolv/sres_record.h"

#include "sofia-sip/su_alloc.h"
#include "sofia-sip/su_string.h"
#include "sofia-sip/hostdomain.h"

#include <sstream>
#include <deque>

#include "controller.hpp"
#include "client-controller.hpp"

#define MY_HOME (theOneAndOnlyController->getHome())

drachtio::DnsResolver::QueryId_t drachtio::DnsResolver::m_id = 0;

namespace drachtio {

  /// QueryInProgress

  DnsResolver::QueryInProgress::QueryInProgress(std::shared_ptr<DnsResolver> resolver, QueryId_t id, url_t* uri, 
    const char* tport, bool resolvingRouteUrl, const string& clientMsgId, const std::string& startLine, 
    const std::string& headers, const std::string& body, std::string& transactionId, std::string& dialogId, std::string& routeUrl) : 
    m_parent(resolver), m_id(id), m_uri(uri), m_resolvingRouteUrl(resolvingRouteUrl), m_clientMsgId(clientMsgId),
     m_startLine(startLine), m_headers(headers), m_body(body), m_transactionId(transactionId),
      m_dialogId(dialogId), m_routeUrl(routeUrl)
  {
    std::deque<std::string> types = {{"a"}};
    if (!tport && m_uri->url_type == url_sips) types.push_back({"tls"});
    else if (!tport) {
      types.push_back({"udp"});
      types.push_back({"tcp"});
    }
    else types.push_back(tport);

    for (auto t : types) {
      std::ostringstream ss;
      int type = sres_type_srv;
      if (0 == t.compare("udp")) ss << "_sip._udp." << m_uri->url_host;
      else if (0 == t.compare("tcp")) ss << "_sip._tcp." << m_uri->url_host;
      else if (0 == t.compare("tls")) ss << "_sips._tls." << m_uri->url_host;
      else {
        type = sres_type_a;
        ss << m_uri->url_host;
      }

      std::string s = ss.str();
      DR_LOG(log_debug) << "DnsResolver::QueryInProgress sending DNS query " << s << " : " << m_id ;
      //std::cout << "sending query " << s << std::endl;

      sres_query_t * query = sres_query(m_parent->resolver(),
        [](sres_context_t *context, sres_query_t *q, sres_record_t *answers[]) {
          QueryInProgress* self = (QueryInProgress *) context;
          self->processAnswers(q, answers);
        },
        (sres_context_t*) this,
        type,
        s.c_str()
      );
      m_queries.insert(query);
    }
  }

  DnsResolver::QueryInProgress::~QueryInProgress() {
  }

  void DnsResolver::QueryInProgress::processAnswers(sres_query_t *query, sres_record_t *answers[]) {

    for (int i = 0; answers[i]; i++) {
      sres_record_t* a = answers[i];
      sres_common_t *r = a->sr_record;

      if (r->r_status == 0) {
        sres_sort_answers(m_parent->resolver(), answers);

        if (r->r_type == sres_type_srv) {
          sres_srv_record_t* srv = a->sr_srv;
          std::string queryString = srv->srv_record->r_name;

          DR_LOG(log_debug) << "DnsResolver::QueryInProgress SRV response for " << r->r_name << 
            " is " <<  srv->srv_target << " : " << m_id ;

          Result_t res;
          res.target = srv->srv_target;
          res.port = srv->srv_port;
          res.weight = srv->srv_weight;
          res.priority = srv->srv_priority;
          res.name = srv->srv_record->r_name;

          if (queryString.rfind("_sip._udp", 0) == 0) res.transport = "udp";
          else if (queryString.rfind("_sip._tcp", 0) == 0) res.transport = "tcp";
          else if (queryString.rfind("_sips.", 0) == 0) res.transport = "tls";

          // check if we have already resolved this target, or sent a request
          auto it = std::find_if(m_results.begin(), m_results.end(), [srv](const Result_t& res) {
            return 0 == res.target.compare(srv->srv_target);
          });
          if (it == m_results.end()) {
            // query the returned target to resolve IP
            DR_LOG(log_debug) << "DnsResolver::QueryInProgress sending DNS query " << srv->srv_target << " : " << m_id ;

            sres_query_t * query = sres_query(m_parent->resolver(),
              [](sres_context_t *context, sres_query_t *q, sres_record_t *answers[]) {
                QueryInProgress* self = (QueryInProgress *) context;
                self->processAnswers(q, answers);
              },
              (sres_context_t*) this,
              sres_type_a,
              srv->srv_target
            );
            m_queries.insert(query);
          }
          else {
            // if we already have a response copy it in
            auto it = std::find_if(m_results.begin(), m_results.end(), [srv](const Result_t& res) {
              return 0 == res.target.compare(srv->srv_target) && !res.ip.empty();
            });
            if (it != m_results.end()) {
              res.ip = it->ip;
            }
          }
          m_results.push_back(res);
        }
        else if (r->r_type == sres_type_a) {
          char addr[64];
          sres_a_record_t* a_record = a->sr_a;

          su_inet_ntop(AF_INET, &a_record->a_addr, addr, sizeof addr);

          auto it = std::find_if(m_results.begin(), m_results.end(), [r](const Result_t& res) {
            return 0 == res.target.compare( r->r_name);
          });
          if (it != m_results.end()) {
            // update all records that have this target
            for (auto& res : m_results) {
              if (0 == res.target.compare(r->r_name)) res.ip = addr;
            }
          }
          else {
            Result_t res;
            res.ip = addr;
            res.name = r->r_name;
            m_results.push_back(res);
          }

          DR_LOG(log_debug) << "DnsResolver::QueryInProgress A response for " << r->r_name << " returned " << 
            addr << " : " << m_id;
        }
      }
    }
    auto rc = m_queries.erase(query);
    if (0 == m_queries.size()) {

      // all queries complete at this point, sort and return
      std::sort(m_results.begin(), m_results.end(), [](const Result_t&a, const Result_t& b)  -> bool {

        // sort by priority then weight if we have those (we will for SRV records only)
        if (a.priority && b.priority && a.priority != b.priority) return a.priority < b.priority;
        if (a.weight && b.weight && a.weight != b.weight) return a.weight > b.weight;
        
        // prioritize SRV records over A records
        if (a.port && !b.port) return true;
        if (b.port && !a.port) return false;

        // prioritize tls over udp, udp ovr tcp
        if (0 == a.transport.compare("tls") && 0 != b.transport.compare("tls")) return true;
        if (0 != a.transport.compare("tls") && 0 != b.transport.compare("tls")) return false;
        if (0 == a.transport.compare("udp") && 0 != b.transport.compare("udp")) return true;
        if (0 != a.transport.compare("udp") && 0 != b.transport.compare("udp")) return false;
        return true;
      });

      if (m_results.size() == 0) DR_LOG(log_debug) << "DnsResolver::QueryInProgress - complete; no records found";
      else DR_LOG(log_debug) << "DnsResolver::QueryInProgress - complete; " << m_results.size() << " records found";
      for (auto res : m_results) {
         DR_LOG(log_debug) << "    ip: " << res.ip << ", name: " << res.name << ", target " << 
          res.target << ", port " << res.port << ", transport " << res.transport
            << ", weight " << res.weight << ", priority " << res.priority;
      }
      m_parent->queryDone(m_id, m_uri, m_results);
    }
    sres_free_answers(m_parent->resolver(), answers);
  }

  /// DnsResolver

  DnsResolver::DnsResolver(std::shared_ptr<ClientController> pClientController) : 
    m_pClientController(pClientController), m_resolver(sres_resolver_new(NULL)), m_done(false) {
  }

  DnsResolver::~DnsResolver() {
    if (m_resolver) sres_resolver_unref(m_resolver);
  }

  void DnsResolver::start() {
    DR_LOG(log_debug) << "Client controller thread id: " << std::this_thread::get_id()  ;
    std::thread t(&DnsResolver::threadFunc, this) ;
    m_thread.swap(t);  
  }

  void DnsResolver::stop() {
    m_done = true;
  }

  void DnsResolver::threadFunc() {
    DR_LOG(log_notice) << "DnsResolver thread id: " << std::this_thread::get_id()  ;
    int nSockets = 1;
    m_sockets = (sres_socket_t *) calloc(nSockets, sizeof(*m_sockets));
    m_pollfds = (struct pollfd *) calloc(nSockets, sizeof(*m_pollfds));

    int rc = sres_resolver_sockets(m_resolver, m_sockets, nSockets);

    for (int i = 0; i < nSockets; i++) {
      m_pollfds[i].fd = m_sockets[0];
      m_pollfds[i].events = POLLIN | POLLERR;
    }

    int events;
    while (!m_done) {
      events = poll(m_pollfds, nSockets, 500);
      if (events) {
        for (int i = 0; i < nSockets; i++) {
          if (m_pollfds[i].revents) {
            sres_resolver_receive(m_resolver, m_pollfds[i].fd);
          }
        }
        /* No harm is done (except wasted CPU) if timer is called more often */
        for (int i = 0; i < nSockets; i++) sres_resolver_timer(m_resolver, m_sockets[i]);
      }
    }
    DR_LOG(log_notice) << "DnsResolver stopping" ;
  }


  bool DnsResolver::resolve(const string& clientMsgId, const std::string& startLine, const std::string& headers, 
            const std::string& body, std::string& transactionId, std::string& dialogId, std::string& routeUrl) {
    char *transport = NULL, tport[32];
    char const *port;
    sip_request_t* req = nullptr;
    url_t *uri = nullptr;

    if (!m_resolver) return false;

    /**
     * If we have a routeUrl, then we use that
     * Otherwise, take the request-URI
     * 
     * If the selected uri is not a dns name, return false
     * Otherwise, resolve the dns name and substitute it back in place
     * then invoke/post back to the ClientController io_context for continued process
     *
    */
   std::string u = routeUrl;
   bool resolvingRouteUrl = true;
   if (routeUrl.empty()) {
     req = sip_request_make(MY_HOME, startLine.c_str());
     if (!req) {
        DR_LOG(log_error) << "DnsResolver::resolve failed to parse start line of request " << startLine;
       return false;
     }
     u = req->rq_url->url_host;
     resolvingRouteUrl = false;
     su_free(MY_HOME, req);
     req = nullptr;
   }

    uri = url_make(MY_HOME, u.c_str());

    if (uri && uri->url_type == url_unknown) url_sanitize(uri);

    if (uri && uri->url_type == url_any) {
      DR_LOG(log_error) << "DnsResolver::resolve invalid type " << u;
      goto fail;
    }

    if (!uri || (uri->url_type != url_sip && uri->url_type != url_sips)) {
      DR_LOG(log_error) << "DnsResolver::resolve invalid uri " << u;
      goto fail;
    }

    port = url_port(uri);
    if (port && !port[0]) port = NULL;
    if (url_param(uri->url_params, "transport=", tport, sizeof tport) > 0)
      transport = tport;

    if (!uri->url_host || !host_is_domain(uri->url_host)) {
      goto fail;
    }

    {
      // we are good to go to send
      QueryId_t id = ++m_id;
      std::shared_ptr<QueryInProgress> qip = std::make_shared<QueryInProgress>(shared_from_this(), id, uri, transport, 
        resolvingRouteUrl, clientMsgId, startLine, headers, body, transactionId, dialogId, routeUrl);
      m_queries.insert(std::make_pair(id, qip));
      return true;
    }

  fail:
      if (req) su_free(MY_HOME, req);
      if (uri) su_free(MY_HOME, uri);
      return false;
  }

  void DnsResolver::queryDone(QueryId_t id, url_t* uri, const Results_t& results) {
    auto it = m_queries.find(id);
    if (it == m_queries.end()) {
      DR_LOG(log_error) << "DnsResolver::queryDone unknown id " << std::dec << id;
      return;
    }
    auto qip = it->second;
    auto newRouteUrl = qip->routeUrl();

    if (results.size() > 0) {
      //TODO: load balance between mutiple results with same priority or weight
      const Result_t& res = results[0];
      url_t *newUri = nullptr;

      if (!qip->isResolvingRouteUrl()) {
        sip_request_t* req = sip_request_make(MY_HOME, qip->startLine().c_str());
        const char* host = req->rq_url->url_host;
        const char* port = req->rq_url->url_port;
        req->rq_url->url_host = res.ip.c_str();
        if (res.port) req->rq_url->url_port = std::to_string(res.port).c_str();
        newRouteUrl = url_as_string(MY_HOME, req->rq_url);
        req->rq_url->url_host = host;
        req->rq_url->url_port = port;
        su_free(MY_HOME, req);
      }
      else {
        url_t* u = url_make(MY_HOME, qip->routeUrl().c_str());
        const char* host = u->url_host;
        const char* port = u->url_port;
        u->url_host = res.ip.c_str();
        if (res.port) u->url_port = std::to_string(res.port).c_str();
        newRouteUrl = url_as_string(MY_HOME, u);
        u->url_host = host;
        u->url_port = port;
        su_free(MY_HOME, u);
      }
    }

    //post to client controller's ioservice
    m_pClientController->getIOService().post(
      std::bind(&ClientController::finalSendRequestOutsideDialog, m_pClientController, qip->clientMsgId(), qip->startLine(), qip->headers(), 
      qip->body(), qip->transactionId(), qip->dialogId(), newRouteUrl
    ));

    m_queries.erase(id);
    su_free(MY_HOME, uri);
    DR_LOG(log_info) << "DnsResolver::queryDone - Removed query " << id << " there are " << m_queries.size() << " remaining in progress";

  }

  bool DnsResolver::replaceDnsName(std::string& u, const Result_t& result) {
    url_t *uri = url_make(MY_HOME, u.c_str());
    if (uri && uri->url_type == url_unknown) url_sanitize(uri);

    if (uri && uri->url_type == url_any) {
      DR_LOG(log_error) << "DnsResolver::replaceDnsName invalid type " << u;
      su_free(MY_HOME, uri);
      return false;
    }

    if (!uri || (uri->url_type != url_sip && uri->url_type != url_sips)) {
      DR_LOG(log_error) << "DnsResolver::replaceDnsName invalid uri " << u;
      if (uri) su_free(MY_HOME, uri);
      return false;
    }

    const char *orig_host = uri->url_host;
    const char *orig_port = uri->url_port;
    std::string port;

    uri->url_host = result.ip.c_str();
    if (result.port != 0) {
      port = std::to_string(result.port);
      uri->url_port = port.c_str();
    }

    char * newRoute = url_as_string(MY_HOME, uri);
    u = newRoute;

    uri->url_host = orig_host;
    uri->url_port = orig_port;
    su_free(MY_HOME, newRoute);
    su_free(MY_HOME, uri);


  return true;
 
  }

}

