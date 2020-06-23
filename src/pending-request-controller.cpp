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


#include <functional>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

#include "pending-request-controller.hpp"
#include "controller.hpp"
#include "cdr.hpp"
#include "request-router.hpp"
#include "request-handler.hpp"

#define CLIENT_TIMEOUT (64000)


namespace drachtio {
  class SipDialogController ;

  PendingRequest_t::PendingRequest_t(msg_t* msg, sip_t* sip, tport_t* tp ) : m_msg( msg ), m_tp(tp), m_canceled(false),
    m_callId(sip->sip_call_id->i_id), m_seq(sip->sip_cseq->cs_seq), 
    m_methodName(sip->sip_cseq->cs_method_name), m_timeArrive({std::chrono::steady_clock::now()}) {
    
    generateUuid( m_transactionId ) ;   
    msg_ref_create( m_msg ) ; 
  }
  PendingRequest_t::~PendingRequest_t()  {
    msg_destroy( m_msg ) ;
  }
  msg_t* PendingRequest_t::getMsg() { return m_msg ; }
  sip_t* PendingRequest_t::getSipObject() { return sip_object(m_msg); }
  const string& PendingRequest_t::getCallId() { return m_callId; }
  const string& PendingRequest_t::getTransactionId() { return m_transactionId; }
  const string& PendingRequest_t::getMethodName() { return m_methodName; }
  tport_t* PendingRequest_t::getTport() { return m_tp; }
  uint32_t PendingRequest_t::getCSeq() { return m_seq; }

  PendingRequestController::PendingRequestController( DrachtioController* pController) : m_pController(pController), 
    m_agent(pController->getAgent()), m_pClientController(pController->getClientController()), 
    m_timerQueue(pController->getRoot(), "pending-request", true) {

    assert(m_agent) ;
 
  }
  PendingRequestController::~PendingRequestController() {
  }

  bool PendingRequestController::getMethodForRequest(const string& transactionId, string& method) {
    std::shared_ptr<PendingRequest_t> p = this->find( transactionId ) ;
    if (!p) return false;

    method = p->getMethodName();
    return true;
  }

  int PendingRequestController::processNewRequest(  msg_t* msg, sip_t* sip, tport_t* tp_incoming, string& transactionId ) {
    if (sip->sip_request->rq_method == sip_method_invite && NULL != sip->sip_to->a_tag ) {
        DR_LOG(log_error) << "processNewRequest - received new INVITE with a to tag that we are not tracking: " << sip->sip_call_id->i_id  ;
        return 481;
    }

    client_ptr client ;
    RequestRouter& router = m_pController->getRequestRouter() ;
    string httpMethod, httpUrl ;
    bool verifyPeer ;

    if( !router.getRoute( sip->sip_request->rq_method_name, httpMethod, httpUrl, verifyPeer ) ) {

      //using inbound connections for this call
      client = m_pClientController->selectClientForRequestOutsideDialog( sip->sip_request->rq_method_name ) ;
      if( !client ) {
        DR_LOG(log_error) << "processNewRequest - No providers available for " << sip->sip_request->rq_method_name  ;
        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {
          {"method", sip->sip_request->rq_method_name},
          {"code", "503"}})
        generateUuid( transactionId ) ;
        return 503 ;
      }
    }

    std::shared_ptr<PendingRequest_t> p = add( msg, sip ) ;
    transactionId = p->getTransactionId() ;      

    msg_destroy( msg ) ;  //our PendingRequest_t is now the holder of the message

    const tp_name_t* tpn = tport_name( tport_parent( tp_incoming ) );
    string host = tpn->tpn_host ;
    string port = tpn->tpn_port ;
    string encodedMessage ;
    EncodeStackMessage( sip, encodedMessage ) ;
    SipMsgData_t meta( msg ) ;
    meta.setDestAddress(host);
    meta.setDestPort(port);

    p->setMeta(meta); 
    p->setEncodedMsg(encodedMessage);

    if( httpUrl.empty() ) {
      m_pClientController->addNetTransaction( client, p->getTransactionId() ) ;

      void (BaseClient::*fn)(const string&, const string&, const SipMsgData_t&) = &BaseClient::sendSipMessageToClient;
      m_pClientController->getIOService().post( std::bind(fn, client, p->getTransactionId(), encodedMessage, meta ) ) ;
    }
    else {
      // using outbound connection for this call

      // if we have tcp:// or tls:// scheme, then we are connecting directly, without an http query first
      if (0 != httpUrl.find("tcp:") && 0 != httpUrl.find("tls:")) {
        vector< pair<string, string> > v;
        v.push_back( make_pair("method", sip->sip_request->rq_method_name )) ;
        v.push_back( make_pair("domain", sip->sip_request->rq_url->url_host ? sip->sip_request->rq_url->url_host : "" )) ;
        v.push_back( make_pair("protocol", meta.getProtocol() )) ;
        v.push_back( make_pair("source_address", meta.getAddress() )) ;
        v.push_back( make_pair("fromUser", sip->sip_from->a_url->url_user ? sip->sip_from->a_url->url_user : "" )) ;
        v.push_back( make_pair("toUser", sip->sip_to->a_url->url_user ? sip->sip_to->a_url->url_user : ""  )) ;
        v.push_back( make_pair("uriUser", sip->sip_request->rq_url->url_user ? sip->sip_request->rq_url->url_user : ""  )) ;

        // add content-type
        string ct = "";
        if (sip->sip_content_type && sip->sip_content_type->c_type) {
          ct = sip->sip_content_type->c_type;
        }
        v.push_back( make_pair("contentType", ct)) ;

        // uri
        char buf[4096];
        url_e(buf, 4096, sip->sip_request->rq_url);
        v.push_back( make_pair("uri", buf)) ;

        // add request uri params to the querystring as well
        if (sip->sip_request->rq_url->url_params) {
          string paramString(sip->sip_request->rq_url->url_params);
          vector<string> strs;
          boost::split(strs, paramString, boost::is_any_of(";"));
          for (vector<string>::iterator it = strs.begin(); it != strs.end(); ++it) {
            vector<string> kv ;
            boost::split(kv, *it, boost::is_any_of("="));
            v.push_back(pair<string,string>(kv[0], kv.size() == 2 ? kv[1] : ""));
          }
        }

        //tmp!!
        httpUrl.append("/");
        int i = 0 ;
        pair<string,string> p;
        BOOST_FOREACH(p, v) {
            if( i++ > 0 ) {
                httpUrl.append("&") ;
            }
            else {
              httpUrl.append("?");
            }
            httpUrl.append(p.first) ;
            httpUrl.append("=") ;
            httpUrl.append(urlencode(p.second));
        }
      }
      
      std::shared_ptr<RequestHandler> pHandler = RequestHandler::getInstance();
      pHandler->makeRequestForRoute(transactionId, httpMethod, httpUrl, encodedMessage) ;
    }
    return 0 ;
  }

  int PendingRequestController::routeNewRequestToClient( client_ptr client, const string& transactionId) {
    std::shared_ptr<PendingRequest_t> p = this->find( transactionId ) ;
    if( !p ) {
      DR_LOG(log_error) << "PendingRequestController::routeNewRequestToClient: transactionId not found: " << transactionId ;
      return 500 ;
    }
    m_pClientController->addNetTransaction( client, p->getTransactionId() ) ;

    void (BaseClient::*fn)(const string&, const string&, const SipMsgData_t&) = &BaseClient::sendSipMessageToClient;
    m_pClientController->getIOService().post( std::bind(fn, client, p->getTransactionId(), 
        p->getEncodedMsg(), p->getMeta() ) ) ;
    return 0 ;
  }

  std::shared_ptr<PendingRequest_t> PendingRequestController::add( msg_t* msg, sip_t* sip ) {
    tport_t *tp = nta_incoming_transport(m_pController->getAgent(), NULL, msg);
    tport_unref(tp) ; //because the above increments the refcount and we don't need to

    std::shared_ptr<PendingRequest_t> p = std::make_shared<PendingRequest_t>( msg, sip, tp ) ;

    DR_LOG(log_debug) << "PendingRequestController::add - tport: " << std::hex << (void*) tp << 
      ", Call-ID: " << p->getCallId() << ", transactionId " << p->getTransactionId() ;
    
    // give client 4 seconds to respond before clearing state
    TimerEventHandle handle = m_timerQueue.add( std::bind(&PendingRequestController::timeout, shared_from_this(), p->getTransactionId()), NULL, CLIENT_TIMEOUT ) ;
    p->setTimerHandle( handle ) ;

    string id ;
    p->getUniqueSipTransactionIdentifier(id) ;
    std::lock_guard<std::mutex> lock(m_mutex) ;
    m_mapCallId2Invite.insert( mapCallId2Invite::value_type(id, p) ) ;
    m_mapTxnId2Invite.insert( mapTxnId2Invite::value_type(p->getTransactionId(), p) ) ;

    return p ;
  }

  std::shared_ptr<PendingRequest_t> PendingRequestController::findAndRemove( const string& transactionId, bool timeout ) {
    std::shared_ptr<PendingRequest_t> p ;
    string id ;
    std::lock_guard<std::mutex> lock(m_mutex) ;
    mapTxnId2Invite::iterator it = m_mapTxnId2Invite.find( transactionId ) ;
    if( it != m_mapTxnId2Invite.end() ) {
      p = it->second ;
      m_mapTxnId2Invite.erase( it ) ;

      p->getUniqueSipTransactionIdentifier(id) ;
      mapCallId2Invite::iterator it2 = m_mapCallId2Invite.find( id ) ;
      assert( it2 != m_mapCallId2Invite.end()) ;
      m_mapCallId2Invite.erase( it2 ) ;

      if( !timeout ) {
        m_timerQueue.remove( p->getTimerHandle() ) ;
      }
    }   
    return p ;
  }

  std::shared_ptr<PendingRequest_t> PendingRequestController::find( const string& transactionId ) {
    std::shared_ptr<PendingRequest_t> p ;
    std::lock_guard<std::mutex> lock(m_mutex) ;
    mapTxnId2Invite::iterator it = m_mapTxnId2Invite.find( transactionId ) ;
    if( it != m_mapTxnId2Invite.end() ) {
      p = it->second ;
    }   
    return p ;
  }

  void PendingRequestController::timeout(const string& transactionId) {
    DR_LOG(log_debug) << "PendingRequestController::timeout: giving up on transactionId " << transactionId ;

    this->findAndRemove( transactionId, true ) ;
    m_pClientController->removeNetTransaction( transactionId ) ;
  }

  void PendingRequestController::logStorageCount(bool bDetail)  {
    std::lock_guard<std::mutex> lock(m_mutex) ;

    DR_LOG(log_debug) << "PendingRequestController storage counts"  ;
    DR_LOG(log_debug) << "----------------------------------"  ;
    DR_LOG(log_debug) << "m_mapCallId2Invite size:                                         " << m_mapCallId2Invite.size()  ;
    if (bDetail) {
        for (const auto& kv : m_mapCallId2Invite) {
          DR_LOG(log_debug) << "    call-id: " << std::hex << (kv.first).c_str();
        }
    }

    DR_LOG(log_debug) << "m_mapTxnId2Invite size:                                          " << m_mapTxnId2Invite.size()  ;
    if (bDetail) {
        for (const auto& kv : m_mapTxnId2Invite) {
          std::shared_ptr<PendingRequest_t> p = kv.second;
          DR_LOG(log_debug) << "    txn id: " << std::hex << (kv.first).c_str() << ", call-id : " << p->getCallId().c_str();
        }
    }
  }


} ;
