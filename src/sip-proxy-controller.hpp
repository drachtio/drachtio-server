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
#include <boost/unordered_set.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <sofia-sip/nta.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>

#include "drachtio.h"
#include "pending-request-controller.hpp"
#include "timer-queue.hpp"
#include "timer-queue-manager.hpp"

namespace drachtio {

  class DrachtioController ;

  class ProxyCore : public boost::enable_shared_from_this<ProxyCore> {
  public:

    class ServerTransaction {
    public:

      ServerTransaction(boost::shared_ptr<ProxyCore> pCore, msg_t* msg) ;
      ~ServerTransaction() ;

      msg_t* msgDup(void) ;
      msg_t* msg(void) { return m_msg; }

      void processRequest( msg_t* msg, sip_t* sip ) ;
      void proxyResponse( msg_t* msg ) ;

      bool isCanceled(void) { return m_canceled; }
      int getSipStatus(void) { return m_sipStatus ;}

      bool isRetransmission(sip_t* sip) ;

      bool forwardResponse( msg_t* msg, sip_t* sip ) ;
      bool generateResponse( int status, const char *szReason = NULL ) ;

      uint32_t getReplacedRSeq( uint32_t rseq ) ;

    protected:
      void writeCdr( msg_t* msg, sip_t* sip ) ;

      boost::weak_ptr<ProxyCore>  m_pCore ;
      msg_t*  m_msg ;
      bool    m_canceled ;
      int     m_sipStatus ;
      uint32_t m_rseq ;
      boost::unordered_map<uint32_t,uint32_t> m_mapAleg2BlegRseq ;
    } ;

    class ClientTransaction : public boost::enable_shared_from_this<ClientTransaction>  {
    public:

      enum State_t {
        not_started = 0,
        calling,
        proceeding, 
        completed,
        terminated
      } ;

      ClientTransaction(boost::shared_ptr<ProxyCore> pCore, boost::shared_ptr<TimerQueueManager> pTQM, const string& target) ;
      ~ClientTransaction() ;

      int getSipStatus(void) const { return m_sipStatus ;}
      bool isCanceled(void) const { return m_canceled; }
      const string& getBranch(void) const { return m_branch; }
      State_t getTransactionState(void) const { return m_state;}
      msg_t* getFinalResponse(void) { return m_msgFinal; }
      void setState( State_t newState ) ;
      uint32_t getRSeq(void) { return m_rseq ;}
      const string& getTarget(void) { return m_target; }

      bool processResponse( msg_t* msg, sip_t* sip ) ;
      
      bool forwardRequest(msg_t* msg, const string& headers) ;
      bool retransmitRequest(msg_t* msg, const string& headers) ;
      bool forwardPrack(msg_t* msg, sip_t* sip) ;
      int cancelRequest(msg_t* msg) ;

      void clearTimerA(void) { m_timerA = NULL;}
      void clearTimerB(void) { m_timerB = NULL;}
      void clearTimerC(void) { m_timerC = NULL;}
      void clearTimerD(void) { m_timerD = NULL;}

    protected:
      void writeCdr( msg_t* msg, sip_t* sip ) ;
      const char* getStateName( State_t state) ;
      void removeTimer( TimerEventHandle& handle, const char *szTimer = NULL ) ;

      boost::weak_ptr<ProxyCore>  m_pCore ;
      msg_t*  m_msgFinal ;
      sip_method_t m_method ;
      string  m_target ;
      string  m_branch ;
      string  m_branchPrack ;
      State_t m_state ;
      int     m_sipStatus ;
      bool    m_canceled ;
      int     m_transmitCount ;
      int     m_durationTimerA ;
      uint32_t m_rseq ;
      boost::shared_ptr<TimerQueueManager> m_pTQM ;

      //timers
      TimerEventHandle  m_timerA ;
      TimerEventHandle  m_timerB ;
      TimerEventHandle  m_timerC ;
      TimerEventHandle  m_timerD ;
    } ;

    enum LaunchType_t {
      serial,
      simultaneous
    } ;


    ProxyCore(const string& clientMsgId, const string& transactionId, tport_t* tp, bool recordRoute, 
      bool fullResponse, bool simultaneous, const string& headers );

    ~ProxyCore() ;

    void initializeTransactions( msg_t* msg, const vector<string>& vecDestination ) ;

    msg_t* msg() { return m_pServerTransaction->msg(); }
    bool isRetransmission(sip_t* sip) { return m_pServerTransaction->isRetransmission(sip);}
    bool generateResponse( int status, const char *szReason = NULL ) { return m_pServerTransaction->generateResponse(status,szReason);}


    bool processResponse(msg_t* msg, sip_t* sip) ;
    bool forwardResponse( msg_t* msg, sip_t* sip ) { return m_pServerTransaction->forwardResponse(msg, sip);}
    bool forwardPrack( msg_t* msg, sip_t* sip) ;
    int startRequests(void) ;
    void removeTerminated(void) ;
    void notifyForwarded200OK( boost::shared_ptr<ClientTransaction> pClient ) ;

    void timerA( boost::shared_ptr<ClientTransaction> pClient ) ;
    void timerB( boost::shared_ptr<ClientTransaction> pClient ) ;
    void timerC( boost::shared_ptr<ClientTransaction> pClient ) ;
    void timerD( boost::shared_ptr<ClientTransaction> pClient ) ;

    const char* getCallId(void) { return sip_object( m_pServerTransaction->msg() )->sip_call_id->i_id; }
    const char* getMethodName(void) { return sip_object( m_pServerTransaction->msg() )->sip_request->rq_method_name; }

    void cancelOutstandingRequests(void) ;
    const string& getClientMsgId() { return m_clientMsgId; }
    const string& getTransactionId() ;
    tport_t* getTport() ;
    const sip_record_route_t* getMyRecordRoute(void) ;
    bool wantsFullResponse(void) { return m_fullResponse; }
    const string& getHeaders(void) { return m_headers; }

    bool isCanceled(void) { return m_canceled; }
    void setCanceled(void) { m_canceled = true; }
    bool shouldFollowRedirects(void) { return m_bFollowRedirects; }
    void shouldFollowRedirects(bool bValue) { m_bFollowRedirects = bValue;}

    bool shouldAddRecordRoute(void) { return m_bRecordRoute;}
    bool getLaunchType(void) { return m_launchType; }
    bool allClientsAreTerminated(void) ;
    void addClientTransactions( const vector< boost::shared_ptr<ClientTransaction> >& vecClientTransactions, 
      boost::shared_ptr<ClientTransaction> pClient ) ;

  protected:
    bool exhaustedAllTargets(void) ;
    void forwardBestResponse(void) ;

  private:
    bool m_fullResponse ;
    bool m_bFollowRedirects ;
    bool m_bRecordRoute ;    
    string m_headers ;

    bool m_canceled ;
    bool m_searching ;
    
    boost::shared_ptr<ServerTransaction> m_pServerTransaction ;
    vector< boost::shared_ptr<ClientTransaction> > m_vecClientTransactions ;
    LaunchType_t m_launchType ;

    string  m_transactionId ;
    string  m_clientMsgId ;
    tport_t* m_tp ;
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

    void proxyRequest( const string& clientMsgId, const string& transactionId, bool recordRoute, bool fullResponse,
      bool followRedirects, bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, 
      const vector<string>& vecDestination, const string& headers )  ;
    void doProxy( ProxyData* pData ) ;
    bool processResponse( msg_t* msg, sip_t* sip ) ;
    bool processRequestWithRouteHeader( msg_t* msg, sip_t* sip ) ;
    bool processRequestWithoutRouteHeader( msg_t* msg, sip_t* sip ) ;

    void removeProxy( boost::shared_ptr<ProxyCore> pCore ) ;

    bool isProxyingRequest( msg_t* msg, sip_t* sip )  ;

    void logStorageCount(void) ;

    boost::shared_ptr<TimerQueueManager> getTimerQueueManager(void) { return m_pTQM; }

    void timerProvisional( boost::shared_ptr<ProxyCore> p ) ;
    void timerFinal( boost::shared_ptr<ProxyCore> p ) ;

  protected:

    //int proxyToTarget( boost::shared_ptr<ProxyCore> p, const string& dest ) ;

    void clearTimerProvisional( boost::shared_ptr<ProxyCore> p );
    void clearTimerFinal( boost::shared_ptr<ProxyCore> p ) ;

    boost::shared_ptr<ProxyCore> addProxy( const string& clientMsgId, const string& transactionId, msg_t* msg, sip_t* sip, tport_t* tp, 
      bool recordRoute, bool fullResponse, bool followRedirects, bool simultaneous, const string& provisionalTimeout, 
      const string& finalTimeout, vector<string> vecDestination, const string& headers ) ;

    boost::shared_ptr<ProxyCore> getProxyByTransactionId( const string& transactionId ) {
      boost::shared_ptr<ProxyCore> p ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapTxnId2Proxy::iterator it = m_mapTxnId2Proxy.find( transactionId ) ;
      if( it != m_mapTxnId2Proxy.end() ) {
        p = it->second ;
      }
      return p ;
    }
    boost::shared_ptr<ProxyCore> getProxyByCallId( const string& callId ) {
      boost::shared_ptr<ProxyCore> p ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( callId ) ;
      if( it != m_mapCallId2Proxy.end() ) {
        p = it->second ;
      }
      return p ;
    }
    boost::shared_ptr<ProxyCore> removeProxyByTransactionId( const string& transactionId );
    boost::shared_ptr<ProxyCore> removeProxyByCallId( const string& callId );

    bool isTerminatingResponse( int status ) ;


  private:
    DrachtioController* m_pController ;
    su_clone_r*     m_pClone ;
    nta_agent_t*    m_agent ;

    boost::mutex    m_mutex ;

    boost::shared_ptr<TimerQueueManager> m_pTQM ;

    typedef boost::unordered_map<string, boost::shared_ptr<ProxyCore> > mapTxnId2Proxy ;
    mapTxnId2Proxy m_mapTxnId2Proxy ;

    typedef boost::unordered_map<string, boost::shared_ptr<ProxyCore> > mapCallId2Proxy ;
    mapCallId2Proxy m_mapCallId2Proxy ;

  } ;

}

#endif
