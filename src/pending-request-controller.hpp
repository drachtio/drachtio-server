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
#ifndef __PENDING_CONTROLLER_HPP__
#define __PENDING_CONTROLLER_HPP__

#include <unordered_map>
#include <mutex>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/msg.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>


#include "drachtio.h"
#include "client-controller.hpp"
#include "request-handler.hpp"
#include "timer-queue.hpp"

using namespace std ;

namespace drachtio {

  class DrachtioController ;

  class PendingRequest_t {
  public:
    PendingRequest_t(msg_t* msg, sip_t* sip, tport_t* tp );
    ~PendingRequest_t() ;

    msg_t* getMsg() ;
    sip_t* getSipObject() ;
    const string& getCallId() ;
    const string& getTransactionId() ;
    void getUniqueSipTransactionIdentifier(string& str) { 
      sip_t* sip = sip_object(m_msg) ;
      makeUniqueSipTransactionIdentifier(sip, str);
    }
    const string& getMethodName() ;
    uint32_t getCSeq() ;
    tport_t* getTport() ;
    TimerEventHandle getTimerHandle(void) { return m_handle;}
    void setTimerHandle( TimerEventHandle handle ) { m_handle = handle;}
    bool isCanceled(void) { return m_canceled;}
    void cancel(void) { m_canceled = true ;}
    const SipMsgData_t& getMeta(void) { return m_meta; }
    void setMeta(SipMsgData_t& meta) { m_meta = meta ;}
    const string& getEncodedMsg(void) { return m_encodedMsg;}
    void setEncodedMsg(const string& msg) { m_encodedMsg = msg; }

  private:
    msg_t*  m_msg ;
    string  m_transactionId ;
    string  m_callId ;
    uint32_t m_seq ;
    string m_methodName ;
    tport_t* m_tp ;
    TimerEventHandle m_handle ;
    bool m_canceled;
    SipMsgData_t m_meta ;
    string m_encodedMsg ;
  } ;


  class PendingRequestController : public std::enable_shared_from_this<PendingRequestController> {
  public:
    PendingRequestController(DrachtioController* pController);
    ~PendingRequestController() ;

    int processNewRequest( msg_t* msg, sip_t* sip, tport_t* tp_incoming, string& transactionId ) ;
    int routeNewRequestToClient( client_ptr client, const string& transactionId) ; 

    std::shared_ptr<PendingRequest_t> findAndRemove( const string& transactionId, bool timeout = false ) ;

    void logStorageCount(void) ;

    bool isRetransmission( sip_t* sip ) {
      string id ;
      makeUniqueSipTransactionIdentifier( sip, id ) ;
      std::lock_guard<std::mutex> lock(m_mutex) ;
      mapCallId2Invite::iterator it = m_mapCallId2Invite.find( id ) ;   
      return it != m_mapCallId2Invite.end() ;
    }

    std::shared_ptr<PendingRequest_t> findInviteByCallId( const char* call_id ) {
      std::shared_ptr<PendingRequest_t> p ;
      string callId = call_id ;
      std::lock_guard<std::mutex> lock(m_mutex) ;
      for( mapCallId2Invite::iterator it = m_mapCallId2Invite.begin() ; m_mapCallId2Invite.end() != it; it++ ) {
        std::shared_ptr<PendingRequest_t> p = it->second ;
        sip_t* sip = p->getSipObject() ;
        if( 0 == callId.compare( sip->sip_call_id->i_id) && sip->sip_request->rq_method == sip_method_invite) {
          return p ;
        }
      }   
      return p ;
   }

  bool getMethodForRequest(const string& transactionId, string& method);
  void timeout(const string& transactionId) ;

  protected:

    std::shared_ptr<PendingRequest_t> find( const string& transactionId ) ;
    std::shared_ptr<PendingRequest_t> add( msg_t* msg, sip_t* sip ) ;

  private:
    DrachtioController* m_pController ;
    nta_agent_t*    m_agent ;
    std::shared_ptr< ClientController > m_pClientController ;

    std::mutex    m_mutex ;

    typedef std::unordered_map<string, std::shared_ptr<PendingRequest_t> > mapCallId2Invite ;
    mapCallId2Invite m_mapCallId2Invite ;

    typedef std::unordered_map<string, std::shared_ptr<PendingRequest_t> > mapTxnId2Invite ;
    mapTxnId2Invite m_mapTxnId2Invite ;

    TimerQueue      m_timerQueue ;

  } ;

}

#endif
