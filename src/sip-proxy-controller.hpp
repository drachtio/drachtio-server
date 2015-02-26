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
#ifndef __SIP_PROXY_CONTROLLER_HPP__
#define __SIP_PROXY_CONTROLLER_HPP__

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <sofia-sip/nta.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>

#include "drachtio.h"
#include "pending-request-controller.hpp"

namespace drachtio {

  class DrachtioController ;

  class Proxy_t {
  public:
    Proxy_t(const string& clientMsgId, const string& transactionId, msg_t* msg, sip_t* sip, tport_t* tp, const string& type, bool fullResponse,
      const vector<string>& vecDestination, const string& headers );
    ~Proxy_t() ;

    msg_t* getMsg() ;
    sip_t* getSipObject() ;
    const string& getClientMsgId() { return m_clientMsgId; }
    const string& getTransactionId() ;
    tport_t* getTport() ;
    const char* getCallId(void) { return getSipObject()->sip_call_id->i_id; }
    bool isStateless(void) { return m_stateless; }
    bool isStateful(void) { return !m_stateless; }
    bool wantsFullResponse(void) { return m_fullResponse; }
    const vector<string>& getDestinations(void) { return m_vecDestination; }
    const string& getHeaders(void) { return m_headers; }
    int getLastStatus(void) { return m_lastResponse; }
    void setLastStatus(int status) { m_lastResponse = status; }
    bool hasMoreTargets(void) { return m_nCurrentDest + 1 < m_vecDestination.size(); }
    const string& getCurrentTarget(void) { return m_vecDestination[m_nCurrentDest]; }
    const string& getNextTarget(void) { return m_vecDestination[++m_nCurrentDest]; }
    bool isCanceled(void) { return m_canceled; }
    void setCanceled(void) { m_canceled = true; }
    bool isFirstTarget(void) { return 0 == m_nCurrentDest; }
    void setCurrentBranch(const string& branch) { m_currentBranch = branch ;}
    const string& getCurrentBranch(void) { return m_currentBranch; }

  private:
    msg_t*  m_msg ;
    string  m_transactionId ;
    string  m_clientMsgId ;
    tport_t* m_tp ;
    bool m_canceled ;
    bool m_stateless ;
    bool m_fullResponse ;
    vector<string> m_vecDestination ;
    string m_headers ;
    unsigned int m_nCurrentDest ;
    int m_lastResponse ;
    string m_currentBranch ;
  } ;


  class SipProxyController : public boost::enable_shared_from_this<SipProxyController> {
  public:
    SipProxyController(DrachtioController* pController, su_clone_r* pClone );
    ~SipProxyController() ;

    class ProxyData {
    public:
      ProxyData() {
        memset(m_szClientMsgId, 0, sizeof(m_szClientMsgId) ) ;
        memset(m_szTransactionId, 0, sizeof(m_szTransactionId) ) ;
      }
      ProxyData(const string& clientMsgId, const string& transactionId ) {
        strncpy( m_szClientMsgId, clientMsgId.c_str(), MSG_ID_LEN ) ;
        strncpy( m_szTransactionId, transactionId.c_str(), MSG_ID_LEN ) ;
       }
      ~ProxyData() {}
      ProxyData& operator=(const ProxyData& md) {
        strncpy( m_szClientMsgId, md.m_szClientMsgId, MSG_ID_LEN) ;
        strncpy( m_szTransactionId, md.m_szTransactionId, MSG_ID_LEN) ;
        return *this ;
      }

      const char* getClientMsgId() { return m_szClientMsgId; } 
      const char* getTransactionId() { return m_szTransactionId; } 

    private:
      char  m_szClientMsgId[MSG_ID_LEN];
      char  m_szTransactionId[MSG_ID_LEN];
    } ;

    void proxyRequest( const string& clientMsgId, const string& transactionId, const string& proxyType, bool fullResponse,
      const vector<string>& vecDestination, const string& headers )  ;
    void doProxy( ProxyData* pData ) ;
    bool processResponse( msg_t* msg, sip_t* sip ) ;
    bool processRequestWithRouteHeader( msg_t* msg, sip_t* sip ) ;
    bool processRequestWithoutRouteHeader( msg_t* msg, sip_t* sip ) ;

    bool isProxyingRequest( msg_t* msg, sip_t* sip )  ;

    void logStorageCount(void) ;

  protected:

    int proxyToTarget( boost::shared_ptr<Proxy_t> p, const string& dest ) ;

    int ackResponse( msg_t* response ) ;

    boost::shared_ptr<Proxy_t> addProxy( const string& clientMsgId, const string& transactionId, msg_t* msg, sip_t* sip, tport_t* tp, 
      const string& proxyType, bool fullResponse, vector<string> vecDestination, const string& headers ) {
      boost::shared_ptr<Proxy_t> p = boost::make_shared<Proxy_t>( clientMsgId, transactionId, msg, sip, tp, proxyType, 
        fullResponse, vecDestination, headers ) ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      m_mapCallId2Proxy.insert( mapCallId2Proxy::value_type(sip->sip_call_id->i_id, p) ) ;
      m_mapTxnId2Proxy.insert( mapTxnId2Proxy::value_type(p->getTransactionId(), p) ) ;   
      return p ;         
    }
    boost::shared_ptr<Proxy_t> getProxyByTransactionId( const string& transactionId ) {
      boost::shared_ptr<Proxy_t> p ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapTxnId2Proxy::iterator it = m_mapTxnId2Proxy.find( transactionId ) ;
      if( it != m_mapTxnId2Proxy.end() ) {
        p = it->second ;
      }
      return p ;
    }
    boost::shared_ptr<Proxy_t> getProxyByCallId( const string& callId ) {
      boost::shared_ptr<Proxy_t> p ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( callId ) ;
      if( it != m_mapCallId2Proxy.end() ) {
        p = it->second ;
      }
      return p ;
    }
    boost::shared_ptr<Proxy_t> removeProxyByTransactionId( const string& transactionId );
    boost::shared_ptr<Proxy_t> removeProxyByCallId( const string& callId );

    bool isTerminatingResponse( int status ) ;


  private:
    DrachtioController* m_pController ;
    su_clone_r*     m_pClone ;
    nta_agent_t*    m_agent ;

    boost::mutex    m_mutex ;

    typedef boost::unordered_map<string, boost::shared_ptr<Proxy_t> > mapTxnId2Proxy ;
    mapTxnId2Proxy m_mapTxnId2Proxy ;

    typedef boost::unordered_map<string, boost::shared_ptr<Proxy_t> > mapCallId2Proxy ;
    mapCallId2Proxy m_mapCallId2Proxy ;

  } ;

}

#endif
