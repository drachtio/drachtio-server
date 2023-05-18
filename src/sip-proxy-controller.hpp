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

#include <time.h>

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>

#include <boost/foreach.hpp>

#include <sofia-sip/nta.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>

#include "drachtio.h"
#include "pending-request-controller.hpp"
#include "timer-queue.hpp"
#include "timer-queue-manager.hpp"

#define MAX_DESTINATIONS (10)
#define HDR_STR_LEN (1024)

namespace drachtio {

  class DrachtioController ;

  class ProxyCore : public std::enable_shared_from_this<ProxyCore> {
  public:

    class ServerTransaction {
    public:

      ServerTransaction(std::shared_ptr<ProxyCore> pCore, msg_t* msg) ;
      ~ServerTransaction() ;

      msg_t* msgDup(void) ;
      msg_t* msg(void) { return m_msg; }

      void processRequest( msg_t* msg, sip_t* sip ) ;
      void proxyResponse( msg_t* msg ) ;

      bool isCanceled(void) { return m_canceled; }
      void setCanceled(bool b) { m_canceled = b;}

      int getSipStatus(void) { return m_sipStatus ;}

      bool isRetransmission(sip_t* sip) ;

      bool forwardResponse( msg_t* msg, sip_t* sip ) ;
      bool generateResponse( int status, const char *szReason = NULL ) ;

      uint32_t getReplacedRSeq( uint32_t rseq ) ;

    protected:
      chrono::time_point<chrono::steady_clock>& getArrivalTime(void) {
        return m_timeArrive;
      }
      bool hasAlerted() const {
        return m_bAlerting;
      }
      void alerting(void) {
        m_bAlerting = true;
      }

      void writeCdr( msg_t* msg, sip_t* sip ) ;

      std::weak_ptr<ProxyCore>  m_pCore ;
      msg_t*  m_msg ;
      bool    m_canceled ;
      int     m_sipStatus ;
      uint32_t m_rseq ;
      std::unordered_map<uint32_t,uint32_t> m_mapAleg2BlegRseq ;

      //timing
      chrono::time_point<chrono::steady_clock> m_timeArrive;
      bool m_bAlerting;
    } ;

    class ClientTransaction : public std::enable_shared_from_this<ClientTransaction>  {
    public:

      enum State_t {
        not_started = 0,
        trying,             // non-INVITE transactions only
        calling,            // INVITE transactions only
        proceeding, 
        completed,
        terminated
      } ;

      ClientTransaction(std::shared_ptr<ProxyCore> pCore, std::shared_ptr<TimerQueueManager> pTQM, const string& target) ;
      ~ClientTransaction() ;

      int getSipStatus(void) const { return m_sipStatus ;}
      bool isCanceled(void) const { return m_canceled; }
      const string& getBranch(void) const { return m_branch; }
      State_t getTransactionState(void) const { return m_state;}
      msg_t* getFinalResponse(void) { return m_msgFinal; }
      void setState( State_t newState ) ;
      uint32_t getRSeq(void) { return m_rseq ;}
      const string& getTarget(void) { return m_target; }
      const char* getCurrentStateName(void) { return getStateName(m_state);} ;
      bool isInviteTransaction(void) { return sip_method_invite == m_method;}
      void reinitState(void) { 
        m_state = not_started ; 
        m_transmitCount = 0;
        m_sipStatus = 0 ;
        m_canceled = false ;
        if( m_msgFinal ) {
          msg_destroy( m_msgFinal ) ;
          m_msgFinal = NULL ;
        }
        //cancel  timers 
        removeTimer( m_timerA, "timerA" ) ;
        removeTimer( m_timerB, "timerB" ) ;
        removeTimer( m_timerC, "timerC" ) ;
        removeTimer( m_timerD, "timerD" ) ;
        removeTimer( m_timerE, "timerE" ) ;
        removeTimer( m_timerF, "timerF" ) ;
        removeTimer( m_timerK, "timerK" ) ;
      }


      bool processResponse( msg_t* msg, sip_t* sip ) ;
      
      bool forwardRequest(msg_t* msg, const string& headers) ;
      bool retransmitRequest(msg_t* msg, const string& headers) ;
      bool forwardPrack(msg_t* msg, sip_t* sip) ;
      int cancelRequest(msg_t* msg) ;

      void clearTimerA(void) { m_timerA = NULL;}
      void clearTimerB(void) { m_timerB = NULL;}
      void clearTimerC(void) { m_timerC = NULL;}
      void clearTimerD(void) { m_timerD = NULL;}
      void clearTimerE(void) { m_timerE = NULL;}
      void clearTimerF(void) { m_timerF = NULL;}
      void clearTimerK(void) { m_timerK = NULL;}
      void clearTimerProvisional(void) { m_timerProvisional = NULL;}


    protected:
      chrono::time_point<chrono::steady_clock>& getArrivalTime(void) {
        return m_timeArrive;
      }
      bool hasAlerted() const {
        return m_bAlerting;
      }
      void alerting(void) {
        m_bAlerting = true;
      }
      void writeCdr( msg_t* msg, sip_t* sip ) ;
      const char* getStateName( State_t state) ;
      void removeTimer( TimerEventHandle& handle, const char *szTimer = NULL ) ;

      std::weak_ptr<ProxyCore>  m_pCore ;
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
      std::shared_ptr<TimerQueueManager> m_pTQM ;

      //timers
      TimerEventHandle  m_timerA ;
      TimerEventHandle  m_timerB ;
      TimerEventHandle  m_timerC ;
      TimerEventHandle  m_timerD ;
      TimerEventHandle  m_timerProvisional ;

      //non-INVITE request timers
      TimerEventHandle  m_timerE ;
      TimerEventHandle  m_timerF ;
      TimerEventHandle  m_timerK ;

      //timing
      chrono::time_point<chrono::steady_clock> m_timeArrive;
      bool m_bAlerting;

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
    bool forwardRequest( msg_t* msg, sip_t* sip) ;
    int startRequests(void) ;
    void removeTerminated(bool alsoRemoveNotStarted = false) ;
    void notifyForwarded200OK( std::shared_ptr<ClientTransaction> pClient ) ;

    void timerA( std::shared_ptr<ClientTransaction> pClient ) ;
    void timerB( std::shared_ptr<ClientTransaction> pClient ) ;
    void timerC( std::shared_ptr<ClientTransaction> pClient ) ;
    void timerD( std::shared_ptr<ClientTransaction> pClient ) ;
    void timerE( std::shared_ptr<ClientTransaction> pClient ) ;
    void timerF( std::shared_ptr<ClientTransaction> pClient ) ;
    void timerK( std::shared_ptr<ClientTransaction> pClient ) ;
    void timerProvisional( std::shared_ptr<ClientTransaction> pClient ) ;

    const char* getCallId(void) { return sip_object( m_pServerTransaction->msg() )->sip_call_id->i_id; }
    void getUniqueSipTransactionIdentifier(string& str);
    const char* getMethodName(void) { return sip_object( m_pServerTransaction->msg() )->sip_request->rq_method_name; }
    sip_method_t getMethod(void) { return sip_object( m_pServerTransaction->msg() )->sip_request->rq_method; }
    sip_cseq_t* getCseq(void) { return sip_object( m_pServerTransaction->msg() )->sip_cseq; }

    void cancelOutstandingRequests(void) ;
    void setCanceled(bool b) {     
        m_canceled = true ;
        m_pServerTransaction->setCanceled(true) ;
    }
    const string& getClientMsgId() { return m_clientMsgId; }
    const string& getTransactionId() ;
    tport_t* getTport() ;
    bool wantsFullResponse(void) { return m_fullResponse; }
    const string& getHeaders(void) { return m_headers; }

    bool isCanceled(void) { return m_canceled; }

    bool shouldFollowRedirects(void) { return m_bFollowRedirects; }
    void shouldFollowRedirects(bool bValue) { m_bFollowRedirects = bValue;}

    bool shouldAddRecordRoute(void) { return m_bRecordRoute;}
    bool getLaunchType(void) { return m_launchType; }
    bool allClientsAreTerminated(void) ;
    void addClientTransactions( const vector< std::shared_ptr<ClientTransaction> >& vecClientTransactions, std::shared_ptr<ClientTransaction> pClient ) ;

    unsigned int getProvisionalTimeout(void) { return m_nProvisionalTimeout; }
    void setProvisionalTimeout(const string& t ) ;

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

    uint32_t m_nProvisionalTimeout ;
    
    std::shared_ptr<ServerTransaction> m_pServerTransaction ;
    vector< std::shared_ptr<ClientTransaction> > m_vecClientTransactions ;
    LaunchType_t m_launchType ;

    string  m_transactionId ;
    string  m_clientMsgId ;
    tport_t* m_tp ;
  } ;


  class SipProxyController : public std::enable_shared_from_this<SipProxyController> {
  public:
    SipProxyController(DrachtioController* pController, su_clone_r* pClone );
    ~SipProxyController() ;

  class ChallengedRequest : public std::enable_shared_from_this<ChallengedRequest>{
    public:
      ChallengedRequest(const char *realm, const char* nonce, const string& remoteAddress) : m_realm(realm), 
        m_nonce(nonce), m_remoteAddress(remoteAddress) {}
      ~ChallengedRequest() {}

      const string& getRealm(void) { return m_realm; }
      const string& getNonce(void) { return m_nonce; }
      const string& getRemoteAddress(void) { return m_remoteAddress; }
      TimerEventHandle getTimerHandle(void) { return m_handle;}
      void setTimerHandle( TimerEventHandle handle ) { m_handle = handle;}

    private:
      string  m_realm ;
      string  m_nonce ;
      string  m_remoteAddress ;
      TimerEventHandle m_handle ;
    } ;

    class ProxyData {
    public:
      ProxyData() {
        memset(m_szClientMsgId, 0, sizeof(m_szClientMsgId) ) ;
        memset(m_szTransactionId, 0, sizeof(m_szTransactionId) ) ;
        memset(m_szProvisionalTimeout, 0, sizeof(m_szProvisionalTimeout)) ;
        memset(m_szFinalTimeout, 0, sizeof(m_szFinalTimeout)) ;
        memset(m_szDestination, 0, MAX_DESTINATIONS * URI_LEN) ;
        memset(m_szHeaders, 0, sizeof(m_szHeaders)) ;
        m_bRecordRoute = m_bFullResponse = m_bSimultaneous = m_bFollowRedirects = false ;
      }
      ProxyData(const string& clientMsgId, const string& transactionId, bool recordRoute, 
        bool fullResponse, bool followRedirects, bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, 
        const vector<string>& vecDestinations, const string& headers ) {

        strncpy( m_szClientMsgId, clientMsgId.c_str(), MSG_ID_LEN - 1) ;
        strncpy( m_szTransactionId, transactionId.c_str(), MSG_ID_LEN -1 ) ;
        m_bRecordRoute = recordRoute ;
        m_bFullResponse = fullResponse ;
        m_bFollowRedirects = followRedirects ;
        m_bSimultaneous = simultaneous ;
        strncpy( m_szProvisionalTimeout, provisionalTimeout.c_str(), 15) ;
        strncpy( m_szFinalTimeout, finalTimeout.c_str(), 15) ;
        strncpy( m_szHeaders, headers.c_str(), HDR_STR_LEN - 1) ;
        int i = 0 ;
        BOOST_FOREACH( const string& dest, vecDestinations ) {
          strncpy( m_szDestination[i++], dest.c_str(), URI_LEN - 1) ;
        }
       }
      ~ProxyData() {}
      ProxyData& operator=(const ProxyData& md) {
        strncpy( m_szClientMsgId, md.m_szClientMsgId, MSG_ID_LEN) ;
        strncpy( m_szTransactionId, md.m_szTransactionId, MSG_ID_LEN) ;
        m_bRecordRoute = md.m_bRecordRoute ;
        m_bFullResponse = md.m_bFullResponse ;
        m_bFollowRedirects = md.m_bFollowRedirects ;
        m_bSimultaneous = md.m_bSimultaneous ;
        strncpy( m_szProvisionalTimeout, md.m_szProvisionalTimeout, 15) ;
        strncpy( m_szFinalTimeout, md.m_szFinalTimeout, 15) ;
        strncpy( m_szHeaders, md.m_szHeaders, HDR_STR_LEN - 1) ;
        memset(m_szDestination, 0, MAX_DESTINATIONS * URI_LEN) ;
        for( int i = 0; i < MAX_DESTINATIONS && *md.m_szDestination[i]; i++ ) {
          strcpy( m_szDestination[i], md.m_szDestination[i] ) ;
        }
        return *this ;
      }

      const char* getClientMsgId() { return m_szClientMsgId; } 
      bool hasClientMsgId() { return m_szClientMsgId[0] != '\0'; }
      const char* getTransactionId() { return m_szTransactionId; } 
      bool getRecordRoute() { return m_bRecordRoute;}
      bool getFullResponse() { return m_bFullResponse;}
      bool getFollowRedirects() { return m_bFollowRedirects;}
      bool getSimultaneous() { return m_bSimultaneous;}
      const char* getProvisionalTimeout() { return m_szProvisionalTimeout;}
      const char* getFinalTimeout() { return m_szFinalTimeout;}
      void getDestinations( vector<string>& vecDestination ) {
        vecDestination.clear() ;
        for( int i = 0; i < MAX_DESTINATIONS && *m_szDestination[i]; i++ ) {
          vecDestination.push_back( m_szDestination[i] ) ;
        }
      }
      const char* getHeaders() { return m_szHeaders;}

    private:
      char  m_szClientMsgId[MSG_ID_LEN];
      char  m_szTransactionId[MSG_ID_LEN];
      bool  m_bRecordRoute ;
      bool  m_bFullResponse ;
      bool  m_bFollowRedirects ;
      bool  m_bSimultaneous ;
      char  m_szProvisionalTimeout[16] ;
      char  m_szFinalTimeout[16] ;
      char  m_szDestination[MAX_DESTINATIONS][URI_LEN] ;
      char  m_szHeaders[HDR_STR_LEN] ;
    } ;

    void proxyRequest( const string& clientMsgId, const string& transactionId, bool recordRoute, bool fullResponse,
      bool followRedirects, bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, 
      const vector<string>& vecDestination, const string& headers )  ;
    void doProxy( ProxyData* pData ) ;
    bool processResponse( msg_t* msg, sip_t* sip ) ;
    bool processRequestWithRouteHeader( msg_t* msg, sip_t* sip ) ;
    bool processRequestWithoutRouteHeader( msg_t* msg, sip_t* sip ) ;

    void removeProxy( std::shared_ptr<ProxyCore> pCore ) ;

    bool isProxyingRequest( msg_t* msg, sip_t* sip )  ;

    void logStorageCount(bool bDetail = false) ;

    bool isRetransmission( sip_t* sip ) {
      std::lock_guard<std::mutex> lock(m_mutex) ;
      string id ;
      this->makeUniqueSipTransactionIdentifier(sip, id) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( id ) ;   
      return it != m_mapCallId2Proxy.end() ;
    }

    std::shared_ptr<TimerQueueManager> getTimerQueueManager(void) { return m_pTQM; }

    void timerProvisional( std::shared_ptr<ProxyCore> p ) ;
    void timerFinal( std::shared_ptr<ProxyCore> p ) ;

    bool addChallenge( sip_t* sip, const string& target ) ;
    void timeoutChallenge(const char* nonce) ;

    void makeUniqueSipTransactionIdentifier(sip_t* sip, string& str);

  protected:

    void clearTimerProvisional( std::shared_ptr<ProxyCore> p );
    void clearTimerFinal( std::shared_ptr<ProxyCore> p ) ;

    std::shared_ptr<ProxyCore> addProxy( const string& clientMsgId, const string& transactionId, msg_t* msg, sip_t* sip, tport_t* tp, 
      bool recordRoute, bool fullResponse, bool followRedirects, bool simultaneous, const string& provisionalTimeout, 
      const string& finalTimeout, vector<string> vecDestination, const string& headers ) ;

    std::shared_ptr<ProxyCore> getProxy( sip_t* sip );
    std::shared_ptr<ProxyCore> getProxyByCallId( sip_t* sip ) {
      std::shared_ptr<ProxyCore> p ;
      std::lock_guard<std::mutex> lock(m_mutex) ;
      for( mapCallId2Proxy::iterator it = m_mapCallId2Proxy.begin(); it != m_mapCallId2Proxy.end(); ++it ) {
        if( string::npos != it->first.find( sip->sip_call_id->i_id ) ) {
          return it->second ;
        }
      }
      return p ;
    }

    std::shared_ptr<ProxyCore> removeProxy( sip_t* sip );

    bool isTerminatingResponse( int status ) ;

    bool isResponseToChallenge( sip_t* sip, string& target ) ;

  private:
    DrachtioController* m_pController ;
    su_clone_r*     m_pClone ;
    nta_agent_t*    m_agent ;

    std::mutex    m_mutex ;

    std::shared_ptr<TimerQueueManager> m_pTQM ;

    typedef std::unordered_map<string, std::shared_ptr<ProxyCore> > mapCallId2Proxy ;
    mapCallId2Proxy m_mapCallId2Proxy ;

    typedef std::unordered_map<string, std::shared_ptr<ChallengedRequest> > mapNonce2Challenge ;
    mapNonce2Challenge m_mapNonce2Challenge ;

    TimerQueue      m_timerQueue ;

  } ;

}

#endif
