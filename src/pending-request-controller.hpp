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

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <boost/thread.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/msg.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>


#include "drachtio.h"
#include "client-controller.hpp"
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

  private:
    msg_t*  m_msg ;
    string  m_transactionId ;
    string  m_callId ;
    uint32_t m_seq ;
    string m_methodName ;
    tport_t* m_tp ;
    TimerEventHandle m_handle ;
  } ;


  class PendingRequestController : public boost::enable_shared_from_this<PendingRequestController> {
  public:
    PendingRequestController(DrachtioController* pController);
    ~PendingRequestController() ;

    int processNewRequest( msg_t* msg, sip_t* sip, string& transactionId ) ;

    boost::shared_ptr<PendingRequest_t> findAndRemove( const string& transactionId, bool timeout = false ) ;

    void logStorageCount(void) ;

    bool isRetransmission( sip_t* sip ) {
      string id ;
      makeUniqueSipTransactionIdentifier( sip, id ) ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapCallId2Invite::iterator it = m_mapCallId2Invite.find( id ) ;   
      return it != m_mapCallId2Invite.end() ;
    }

    void timeout(const string& transactionId) ;

  protected:

    boost::shared_ptr<PendingRequest_t> add( msg_t* msg, sip_t* sip ) ;

  private:
    DrachtioController* m_pController ;
    nta_agent_t*    m_agent ;
    boost::shared_ptr< ClientController > m_pClientController ;

    boost::mutex    m_mutex ;

    typedef boost::unordered_map<string, boost::shared_ptr<PendingRequest_t> > mapCallId2Invite ;
    mapCallId2Invite m_mapCallId2Invite ;

    typedef boost::unordered_map<string, boost::shared_ptr<PendingRequest_t> > mapTxnId2Invite ;
    mapTxnId2Invite m_mapTxnId2Invite ;

    TimerQueue      m_timerQueue ;

  } ;

}

#endif
