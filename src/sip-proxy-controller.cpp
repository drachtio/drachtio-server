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

namespace drachtio {
    class SipDialogController ;
}

#include <algorithm> // for remove_if
#include <functional> // for unary_function
#
#include <boost/regex.hpp>
#include <boost/bind.hpp>

#include <sofia-sip/sip_util.h>
#include <sofia-sip/msg_header.h>
#include <sofia-sip/msg_addr.h>
#include <sofia-sip/su_random.h>

#include "sip-proxy-controller.hpp"
#include "controller.hpp"
#include "pending-request-controller.hpp"
#include "cdr.hpp"

#define TIMER_C_MSECS (30 * 1000)
#define TIMER_B_MSECS (NTA_SIP_T1 * 64)
#define TIMER_D_MSECS (32500)

static nta_agent_t* nta = NULL ;
static drachtio::SipProxyController* theProxyController = NULL ;

namespace {
    void cloneProxy(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipProxyController::ProxyData* d = reinterpret_cast<drachtio::SipProxyController::ProxyData*>( arg ) ;
        pController->getProxyController()->doProxy(d) ;
    }
} ;


namespace drachtio {
    struct has_rseq
    {
        has_rseq(uint32_t rseq) : m_rseq(rseq) {}
        uint32_t m_rseq;
        bool operator()(boost::shared_ptr<ProxyCore::ClientTransaction> pClient)
        {
            return m_rseq == pClient->getRSeq(); 
        }
    };
    struct has_target
    {
        has_target(const string& target) : m_target(target) {}
        string m_target;
        bool operator()(boost::shared_ptr<ProxyCore::ClientTransaction> pClient)
        {
            return 0 == m_target.compare( pClient->getTarget() ); 
        }
    };

    bool ClientTransactionIsTerminated( const boost::shared_ptr<ProxyCore::ClientTransaction> pClient ) {
        return pClient->getTransactionState() == ProxyCore::ClientTransaction::terminated ;
    }
    bool ClientTransactionIsCallingOrProceeding( const boost::shared_ptr<ProxyCore::ClientTransaction> pClient ) {
        return pClient->getTransactionState() == ProxyCore::ClientTransaction::calling ||
            pClient->getTransactionState() == ProxyCore::ClientTransaction::proceeding;
    }
    bool bestResponseOrder( boost::shared_ptr<ProxyCore::ClientTransaction> c1, boost::shared_ptr<ProxyCore::ClientTransaction> c2 ) {
        // per RFC 3261 16.7.6
        if( ProxyCore::ClientTransaction::completed == c1->getTransactionState() && 
            ProxyCore::ClientTransaction::completed != c2->getTransactionState() ) return true ;

        if( ProxyCore::ClientTransaction::completed != c1->getTransactionState() && 
            ProxyCore::ClientTransaction::completed == c2->getTransactionState() ) return false ;

        if( ProxyCore::ClientTransaction::completed == c1->getTransactionState() && 
            ProxyCore::ClientTransaction::completed == c2->getTransactionState()) {
            
            int c1Status = c1->getSipStatus() ;
            int c2Status = c2->getSipStatus()  ;

            if( c1Status >= 600 ) return true ;
            if( c2Status >= 600 ) return false ;

            switch( c1Status ) {
                case 401:
                case 407:
                case 415:
                case 420:
                case 484:
                    return true ;
                default:
                    switch( c2Status ) {
                        case 401:
                        case 407:
                        case 415:
                        case 420:
                        case 484:
                            return false ;
                        default:
                            break ;

                    }
            }
            if( c1Status >= 400 && c1Status <= 499 ) return true ;
            if( c2Status >= 400 && c2Status <= 499 ) return false ;
            if( c1Status >= 500 && c1Status <= 599 ) return true ;
            if( c2Status >= 500 && c2Status <= 599 ) return false ;
        }
        return true ;
    }

    ///ServerTransaction
    ProxyCore::ServerTransaction::ServerTransaction(boost::shared_ptr<ProxyCore> pCore, msg_t* msg) : 
        m_pCore(pCore), m_msg(msg), m_canceled(false), m_sipStatus(0), m_rseq(0) {

        msg_ref(m_msg) ;
    }
    ProxyCore::ServerTransaction::~ServerTransaction() {
        DR_LOG(log_debug) << "ServerTransaction::~ServerTransaction" ;
        msg_unref(m_msg) ;
    }
    msg_t* ProxyCore::ServerTransaction::msgDup() {
        return msg_dup( m_msg ) ;
    }
    uint32_t ProxyCore::ServerTransaction::getReplacedRSeq( uint32_t rseq ) {
        //DR_LOG(log_debug) << "searching for original rseq that was replaced with " << rseq << " map size " << m_mapAleg2BlegRseq.size();
        uint32_t original = 0 ;
        boost::unordered_map<uint32_t,uint32_t>::const_iterator it = m_mapAleg2BlegRseq.find(rseq) ;
        if( m_mapAleg2BlegRseq.end() != it ) original = it->second ;
        //DR_LOG(log_debug) << "rseq that was replaced with " << rseq << " was originally " << original;
        return original ;
    }


    bool ProxyCore::ServerTransaction::isRetransmission(sip_t* sip) {
        return sip->sip_request->rq_method == sip_object( m_msg )->sip_request->rq_method ; //NB: we already know that the Call-Id matches
    }
    bool ProxyCore::ServerTransaction::forwardResponse( msg_t* msg, sip_t* sip ) {

        //if sending a reliable provisional response, we need to generate our own RSeq value
        bool reliable = true ;
        if( sip->sip_rseq && sip->sip_status->st_status < 200 ) {
            if( 0 == m_rseq ) m_rseq = su_random() ;
            m_rseq++ ;

            //mapping of the re-written RSeq value on the UAS side to what the RSeq value recived on the UAC side was
            //DR_LOG(log_debug) << "saving to map original rseq " << sip->sip_rseq->rs_response << " that was replaced by " << m_rseq ;
            m_mapAleg2BlegRseq.insert( make_pair<uint32_t,uint32_t>(m_rseq,sip->sip_rseq->rs_response) ) ;

            sip->sip_rseq->rs_response = m_rseq; 
        }

        int rc = nta_msg_tsend( nta, msg_ref(msg), NULL,
            TAG_IF( reliable, SIPTAG_RSEQ(sip->sip_rseq) ),
            TAG_END() ) ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "ServerTransaction::forwardResponse failed proxying response " << std::dec << 
                sip->sip_status->st_status << " " << sip->sip_call_id->i_id << ": error " << rc ; 
            msg_unref(msg) ;
            return false ;            
        }
        bool bRetransmitFinal = m_sipStatus >= 200 &&  sip->sip_status->st_status >= 200 ;
        if( !bRetransmitFinal ) m_sipStatus = sip->sip_status->st_status ;

        if( !bRetransmitFinal && sip->sip_cseq->cs_method == sip_method_invite && sip->sip_status->st_status >= 200 ) {
            writeCdr( msg, sip ) ;
        }
        msg_unref(msg) ;
        return true ;
    }
    void ProxyCore::ServerTransaction::writeCdr( msg_t* msg, sip_t* sip ) {
        if( 200 == sip->sip_status->st_status ) {
            Cdr::postCdr( boost::make_shared<CdrStart>( msg, "application", Cdr::proxy_uas ) );                
        }
        else if( sip->sip_status->st_status > 200 ) {
            Cdr::postCdr( boost::make_shared<CdrStop>( msg, "application",
                487 == sip->sip_status->st_status ? Cdr::call_canceled : Cdr::call_rejected ) );
        }        
    }
    bool ProxyCore::ServerTransaction::generateResponse( int status, const char *szReason ) {
       msg_t* reply = nta_msg_create(nta, 0) ;
        msg_ref(reply) ;
        nta_msg_mreply( nta, reply, sip_object(reply), status, szReason, 
            msg_ref(m_msg), //because it will lose a ref in here
            TAG_END() ) ;

        if( sip_method_invite == sip_object(m_msg)->sip_request->rq_method && status >= 200 ) {
            Cdr::postCdr( boost::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );
        }

        msg_unref(reply) ;  

        return true ;      
    }


    ///ClientTransaction
    ProxyCore::ClientTransaction::ClientTransaction(boost::shared_ptr<ProxyCore> pCore, 
        boost::shared_ptr<TimerQueueManager> pTQM,  const string& target) : 
        m_pCore(pCore), m_target(target), m_canceled(false), m_sipStatus(0),
        m_timerA(NULL), m_timerB(NULL), m_timerC(NULL), m_timerD(NULL), m_msgFinal(NULL),
        m_transmitCount(0), m_method(sip_method_unknown), m_state(not_started), m_pTQM(pTQM) {

        string random ;
        generateUuid( random ) ;
        m_branch = string(rfc3261prefix) + random ;
    }
    ProxyCore::ClientTransaction::~ClientTransaction() {
        DR_LOG(log_debug) << "ClientTransaction::~ClientTransaction" ;
        removeTimer( m_timerA, "timerA" ) ;
        removeTimer( m_timerB, "timerB" ) ;
        removeTimer( m_timerC, "timerC" ) ;
        removeTimer( m_timerD, "timerD" ) ;

        if( m_msgFinal ) msg_unref( m_msgFinal ) ;
    }
    const char* ProxyCore::ClientTransaction::getStateName( State_t state) {
        static const char* szNames[] = {
            "NOT STARTED",
            "CALLING",
            "PROCEEDING",
            "COMPLETED",
            "TERMINATED",
        } ;
        return szNames[ static_cast<int>( state ) ] ;
    }
    void ProxyCore::ClientTransaction::removeTimer( TimerEventHandle& handle, const char* szTimer ) {
        if( NULL == handle ) return ;
        m_pTQM->removeTimer( handle, szTimer ) ;
        handle = NULL ;
    }
    void ProxyCore::ClientTransaction::setState( State_t newState ) {
        if( newState == m_state ) return ;

        boost::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        DR_LOG(log_info) << getStateName(m_state) << " --> " << getStateName(newState) << " " << pCore->getCallId();

        m_state = newState ;

        //set transaction state timers
        if( sip_method_invite == m_method ) {
            switch( m_state ) {
                case calling:
                    assert( !m_timerA ) ; //TODO: should only be doing this on unreliable transports
                    assert( !m_timerB ) ;
                    assert( !m_timerC ) ;

                    //timer A = retransmission timer 
                    m_timerA = m_pTQM->addTimer("timerA", 
                        boost::bind(&ProxyCore::timerA, pCore, shared_from_this()), NULL, m_durationTimerA = NTA_SIP_T1 ) ;

                    //timer B = timeout when all invite retransmissions have been exhausted
                    m_timerB = m_pTQM->addTimer("timerB", 
                        boost::bind(&ProxyCore::timerB, pCore, shared_from_this()), NULL, TIMER_B_MSECS ) ;
                    
                    //timer C - timeout to wait for final response before returning 408 Request Timeout. 
                    m_timerC = m_pTQM->addTimer("timerC", 
                        boost::bind(&ProxyCore::timerC, pCore, shared_from_this()), NULL, TIMER_C_MSECS ) ;
                break ;

                case proceeding:
                    removeTimer( m_timerA, "timerA" ) ;
                    removeTimer( m_timerB, "timerB" ) ;
                break; 

                case completed:
                    removeTimer( m_timerA, "timerA" ) ;
                    removeTimer( m_timerB, "timerB" ) ;

                    //timer D - timeout when transaction can move from completed state to terminated
                    //note: in the case of a late-arriving provisional response after we've decided to cancel an invite, 
                    //we can have a timer D set when we get here as state will go 
                    //CALLING --> COMPLETED (when decide to cancel) --> PROCEEDING (when late response arrives) --> COMPLETED (as we send the CANCEL)
                    removeTimer( m_timerD, "timerD" ) ;
                    m_timerD = m_pTQM->addTimer("timerD", boost::bind(&ProxyCore::timerD, pCore, shared_from_this()), 
                        NULL, TIMER_D_MSECS ) ;
                break ;

                case terminated:
                    removeTimer( m_timerA, "timerA" ) ;
                    removeTimer( m_timerB, "timerB" ) ;
                break ;

                default:
                break; 
            }
        }
    }
    bool ProxyCore::ClientTransaction::forwardPrack(msg_t* msg, sip_t* sip) {
        boost::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        assert( sip->sip_rack ) ;
        assert( sip_method_prack == sip->sip_request->rq_method ) ;
        assert( 0 != m_rseq ) ;

        sip->sip_rack->ra_response = m_rseq ;

        string random ;
        generateUuid( random ) ;
        m_branchPrack =string(rfc3261prefix) + random ;

        int rc = nta_msg_tsend( nta, 
            msg, 
            NULL, 
            TAG_IF( pCore->shouldAddRecordRoute(), SIPTAG_RECORD_ROUTE(pCore->getMyRecordRoute() ) ),
            NTATAG_BRANCH_KEY( m_branchPrack.c_str() ),
            SIPTAG_RACK( sip->sip_rack ),
            TAG_END() ) ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "forwardPrack: error forwarding request " ;
            return false ;
        }
        return true ;
    }
    bool ProxyCore::ClientTransaction::forwardRequest(msg_t* msg, const string& headers) {
        boost::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        sip_t* sip = sip_object(msg) ;

        m_transmitCount++ ;

        if( not_started == m_state ) {
            assert( 1 == m_transmitCount ) ;

            string random ;
            generateUuid( random ) ;
            m_branch = string(rfc3261prefix) + random ;
            m_method = sip->sip_request->rq_method ;

            setState( calling ) ;
        }

        //Max-Forwards: decrement or set to 70 
        if( sip->sip_max_forwards ) {
            sip->sip_max_forwards->mf_count-- ;
        }
        else {
            sip_add_tl(msg, sip, SIPTAG_MAX_FORWARDS_STR("70"), TAG_END());
        }

        sip_request_t *rq = sip_request_format(msg_home(msg), "%s %s SIP/2.0", sip->sip_request->rq_method_name, m_target.c_str() ) ;
        msg_header_replace(msg, NULL, (msg_header_t *)sip->sip_request, (msg_header_t *) rq) ;

        tagi_t* tags = makeTags( headers ) ;

        int rc = nta_msg_tsend( nta, 
            msg_ref(msg), 
            URL_STRING_MAKE(m_target.c_str()), 
            TAG_IF( pCore->shouldAddRecordRoute(), SIPTAG_RECORD_ROUTE(pCore->getMyRecordRoute() ) ),
            NTATAG_BRANCH_KEY(m_branch.c_str()),
            TAG_NEXT(tags) ) ;

        deleteTags( tags ) ;

        if( rc < 0 ) {
            setState( terminated ) ;
            m_sipStatus = 503 ; //RFC 3261 16.9, but we should validate the request-uri to prevent errors sending to malformed uris
            msg_unref(msg) ;
            return true ;
        }

        if( 1 == m_transmitCount && sip_method_invite == m_method ) {
            Cdr::postCdr( boost::make_shared<CdrAttempt>( msg, "application" ) );
        }
        
        return true ;
    }
    bool ProxyCore::ClientTransaction::retransmitRequest(msg_t* msg, const string& headers) {
        m_durationTimerA <<= 1 ;
        DR_LOG(log_debug) << "ClientTransaction - retransmitting request, timer A will be set to " << dec << m_durationTimerA << "ms" ;
        if( forwardRequest(msg, headers) ) {
            boost::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
            assert( pCore ) ;
            m_timerA = m_pTQM->addTimer("timerA", 
                boost::bind(&ProxyCore::timerA, pCore, shared_from_this()), NULL, m_durationTimerA) ;
            return true ;
        }
        return false ;
     }

     bool ProxyCore::ClientTransaction::processResponse( msg_t* msg, sip_t* sip ) {
        bool bForward = false ;

        DR_LOG(log_debug) << "client processing response, via branch is " << sip->sip_via->v_branch <<
            " original request via branch was " << m_branch << " and PRACK via branch was " << m_branchPrack ;

        if( 0 != m_branch.compare(sip->sip_via->v_branch) && sip_method_prack != sip->sip_cseq->cs_method ) return false ; 
        if( 0 != m_branchPrack.compare(sip->sip_via->v_branch) && sip_method_prack == sip->sip_cseq->cs_method ) return false ;

        boost::shared_ptr<ClientTransaction> me = shared_from_this() ;
        boost::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        if( terminated == m_state ) {
            DR_LOG(log_info) << "Discarding late-arriving response because transaction is terminated " <<
                sip->sip_status->st_status << " " << sip->sip_cseq->cs_method << " " << sip->sip_call_id->i_id ;
            nta_msg_discard( nta, msg ) ;
            return true ;
        }

        //late-arriving response to a request that is no longer desired by us, but which we did not cancel yet since there was no response
        if( m_canceled && completed == m_state && 0 == m_sipStatus && m_method == sip->sip_cseq->cs_method ) {
            DR_LOG(log_info) << "late-arriving response to a transaction that we want to cancel " <<
                sip->sip_status->st_status << " " << sip->sip_cseq->cs_method << " " << sip->sip_call_id->i_id ;
            setState (proceeding ); 
            cancelRequest( msg ) ;
            nta_msg_discard( nta, msg ) ;
            return true ;
        }

        //response to our original request?
        if( m_method == sip->sip_cseq->cs_method ) {    

            //retransmission of final response?
            if( (completed == m_state || terminated == m_state) && sip->sip_status->st_status >= 200 ) {
                ackResponse( msg ) ;
                nta_msg_discard( nta, msg ) ;
                return true ;               
            }
            m_sipStatus = sip->sip_status->st_status ;

            //set new state, (re)set timers
            if( m_sipStatus >= 100 && m_sipStatus <= 199 ) {
                setState( proceeding ) ;

                if( 100 != m_sipStatus && sip_method_invite == sip->sip_cseq->cs_method ) {
                    assert( m_timerC ) ;
                    removeTimer( m_timerC, "timerC" ) ;
                    m_timerC = m_pTQM->addTimer("timerC", boost::bind(&ProxyCore::timerC, pCore, shared_from_this()), 
                        NULL, TIMER_C_MSECS ) ;
                }                  
            }
            else if( m_sipStatus >= 200 && m_sipStatus <= 299 ) {
                //NB: order is important here - 
                //proxy core will attempt to cancel any invite not in the terminated state
                //so set that first before announcing our success
                setState( terminated ) ;
                pCore->notifyForwarded200OK( me ) ;                 
            }
            else if( m_sipStatus >= 300 ) {
                setState( completed ) ;
            }

            if( m_sipStatus >= 200 && sip_method_invite == sip->sip_cseq->cs_method ) {
                removeTimer(m_timerC, "timerC") ;
            }

            //determine whether to forward this response upstream
            if( 100 == m_sipStatus ) {
                DR_LOG(log_debug) << "discarding 100 Trying since we are a stateful proxy" ;
                nta_msg_discard( nta, msg ) ;
                return true ;
            }
            if( m_sipStatus > 100 && m_sipStatus < 300 ) {
                //forward immediately: RFC 3261 16.7.5
                //TODO: move this up to ProxyCore?
                bForward = true ;

                //check if reliable provisional - stow RSeq to put in RAck header in PRACK later
                if( m_sipStatus < 200 && sip->sip_rseq ) m_rseq = sip->sip_rseq->rs_response ;
            }
            if( m_sipStatus >= 300 ) {
                
                //save final response for later forwarding
                m_msgFinal = msg ;
                msg_ref( m_msgFinal ) ;

                ackResponse( msg ) ;

                //TODO: move this up to ProxyCore??
                if( m_sipStatus >= 300 && m_sipStatus <= 399 && pCore->shouldFollowRedirects() && sip->sip_contact ) {
                    vector< boost::shared_ptr<ClientTransaction> > vecNewTransactions  ;
                    sip_contact_t* contact = sip->sip_contact ;
                    for (sip_contact_t* m = sip->sip_contact; m; m = m->m_next) {
                        char buffer[URL_MAXLEN] = "" ;
                        url_e(buffer, URL_MAXLEN, m->m_url) ;

                        DR_LOG(log_debug) << "ClientTransaction::processResponse -- adding contact from redirect response " << buffer ;

                        boost::shared_ptr<TimerQueueManager> pTQM = theProxyController->getTimerQueueManager() ;
                        boost::shared_ptr<ClientTransaction> pClient = boost::make_shared<ClientTransaction>(pCore, pTQM, buffer) ;
                        vecNewTransactions.push_back( pClient ) ;
                    }
                    pCore->addClientTransactions( vecNewTransactions, shared_from_this() ) ;
                }
            }     

            //write CDRS on the UAC side for final response to an INVITE
            if( sip_method_invite == sip->sip_cseq->cs_method && m_sipStatus >= 200 ) {
                writeCdr( msg, sip ) ;
            }       

            //send response back if full responses requested and this is a final
            ////TODO: move this up to ProxyCore??
            if( pCore->wantsFullResponse() && m_sipStatus >= 200 ) {
                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta(msg) ;
                string s ;
                meta.toMessageFormat(s) ;

                string data = s + "|||continue" + CRLF + encodedMessage ; 

                theOneAndOnlyController->getClientController()->route_api_response( pCore->getClientMsgId(), "OK", data ) ;   
                if( m_sipStatus >= 200 && m_sipStatus <= 299 ) {
                    theOneAndOnlyController->getClientController()->route_api_response( pCore->getClientMsgId(), "OK", "done" ) ;
                 }             
            }
        }
        else if( sip_method_cancel == sip->sip_cseq->cs_method ) {
            DR_LOG(log_debug) << "Received " << sip->sip_status->st_status << " response to CANCEL" ;
            nta_msg_discard( nta, msg ); 
            return true ;
        }
        else {
            DR_LOG(log_debug) << "Received " << sip->sip_status->st_status << " " << sip->sip_cseq->cs_method_name <<
                " response, forwarding upstream" ;            
        }

        if( bForward ) {
            bool bOK = pCore->forwardResponse( msg, sip ) ;
        }  
        else {
            nta_msg_discard( nta, msg ) ;
        }      
        return true ;
    }
    int ProxyCore::ClientTransaction::cancelRequest(msg_t* msg) {

        //cancel retransmission timers 
        removeTimer( m_timerA, "timerA" ) ;
        removeTimer( m_timerB, "timerB" ) ;
        removeTimer( m_timerC, "timerC" ) ;

        if( calling == m_state ) {
            DR_LOG(log_debug) << "cancelRequest - client request in CALLING state has not received a response so not sending CANCEL" ;
            setState(completed) ;
            m_canceled = true ;
            return 0 ;
        }
        if( proceeding != m_state ) {
            DR_LOG(log_debug) << "cancelRequest - returning without canceling because state is not PROCEEDING it is " << 
                getStateName(m_state) ;
            return 0 ;
        }

        sip_t* sip = sip_object(msg) ;
        msg_t *cmsg = nta_msg_create(nta, 0);
        sip_t *csip = sip_object(cmsg);
        url_string_t const *ruri;

        nta_outgoing_t *cancel = NULL ;
        sip_request_t *rq;
        sip_cseq_t *cseq;
        su_home_t *home = msg_home(cmsg);

        if (csip == NULL)
        return -1;

        sip_add_tl(cmsg, csip,
            SIPTAG_TO(sip->sip_to),
            SIPTAG_FROM(sip->sip_from),
            SIPTAG_CALL_ID(sip->sip_call_id),
            TAG_END());

        if (!(cseq = sip_cseq_create(home, sip->sip_cseq->cs_seq, SIP_METHOD_CANCEL)))
            goto err;
        else
            msg_header_insert(cmsg, (msg_pub_t *)csip, (msg_header_t *)cseq);

        if (!(rq = sip_request_format(home, "CANCEL %s SIP/2.0", m_target.c_str() )))
            goto err;
        else
            msg_header_insert(cmsg, (msg_pub_t *)csip, (msg_header_t *)rq);

        if( nta_msg_tsend( nta, cmsg, NULL, 
            NTATAG_BRANCH_KEY(m_branch.c_str()),
            TAG_END() ) < 0 )
 
            goto err ;

        m_canceled = true ;
        return 0;

        err:
            if( cmsg ) msg_unref(cmsg);
            return -1;
    }
    void ProxyCore::ClientTransaction::writeCdr( msg_t* msg, sip_t* sip ) {
        string encodedMessage ;
        EncodeStackMessage( sip, encodedMessage ) ;
        if( 200 == m_sipStatus ) {
            Cdr::postCdr( boost::make_shared<CdrStart>( msg, "network", Cdr::proxy_uac ), encodedMessage );                
        }               
        else {
            Cdr::postCdr( boost::make_shared<CdrStop>( msg, "network",  
                487 == m_sipStatus ? Cdr::call_canceled : Cdr::call_rejected ), encodedMessage );                
        }        
    }

    ///ProxyCore
    ProxyCore::ProxyCore(const string& clientMsgId, const string& transactionId, tport_t* tp,bool recordRoute, 
        bool fullResponse, bool simultaneous, const string& headers ) : 
        m_clientMsgId(clientMsgId), m_transactionId(transactionId), m_tp(tp), m_canceled(false), m_headers(headers),
        m_fullResponse(fullResponse), m_bRecordRoute(recordRoute), m_launchType(simultaneous ? ProxyCore::simultaneous : ProxyCore::serial), 
        m_searching(true) {
    }
    ProxyCore::~ProxyCore() {
        DR_LOG(log_debug) << "ProxyCore::~ProxyCore" ;
    }
    void ProxyCore::initializeTransactions( msg_t* msg, const vector<string>& vecDestination ) {
        m_pServerTransaction = boost::make_shared<ServerTransaction>( shared_from_this(), msg ) ;

        for( vector<string>::const_iterator it = vecDestination.begin(); it != vecDestination.end(); it++ ) {
            boost::shared_ptr<TimerQueueManager> pTQM = theProxyController->getTimerQueueManager() ;
            boost::shared_ptr<ClientTransaction> pClient = boost::make_shared<ClientTransaction>( shared_from_this(), pTQM, *it ) ;
            m_vecClientTransactions.push_back( pClient ) ;
        }
        DR_LOG(log_debug) << "initializeTransactions - added " << dec << m_vecClientTransactions.size() << " client transactions " ;
    }
    void ProxyCore::removeTerminated() {
        int nBefore =  m_vecClientTransactions.size() ;
        m_vecClientTransactions.erase(
            std::remove_if( m_vecClientTransactions.begin(), m_vecClientTransactions.end(), ClientTransactionIsTerminated),
            m_vecClientTransactions.end() ) ;
        int nAfter = m_vecClientTransactions.size() ;
        DR_LOG(log_debug) << " removeTerminated - removed " << dec << nBefore-nAfter << ", leaving " << nAfter ;

        if( 0 == nAfter ) {
            DR_LOG(log_debug) << "removeTerminated - all client TUs are terminated, removing proxy core" ;
            theProxyController->removeProxy( shared_from_this() ) ;
        }
    }
    void ProxyCore::notifyForwarded200OK( boost::shared_ptr<ClientTransaction> pClient ) {
        DR_LOG(log_debug) << "forwarded 200 OK terminating other clients" ;
        m_searching = false ;
        cancelOutstandingRequests() ;
    }
    void ProxyCore::addClientTransactions( const vector< boost::shared_ptr<ClientTransaction> >& vecClientTransactions, 
        boost::shared_ptr<ClientTransaction> pClient ) {

        vector< boost::shared_ptr< ClientTransaction > >::iterator it = std::find_if( m_vecClientTransactions.begin(), 
            m_vecClientTransactions.end(), has_target(pClient->getTarget()) ) ;
        assert( m_vecClientTransactions.end() != it ) ;

        m_vecClientTransactions.insert( ++it, vecClientTransactions.begin(), vecClientTransactions.end() ) ;
    }

    //timer functions
    

    //retransmission timer
    void ProxyCore::timerA(boost::shared_ptr<ClientTransaction> pClient) {
        assert( pClient->getTransactionState() == ClientTransaction::calling ) ;
        pClient->clearTimerA() ;
        msg_t* msg = m_pServerTransaction->msgDup() ;
        pClient->retransmitRequest(msg, m_headers) ;
        msg_unref(msg) ;
    }
    //max retransmission timer
    void ProxyCore::timerB(boost::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_debug) << "timer B fired for a client transaction" ;
        assert( pClient->getTransactionState() == ClientTransaction::calling ) ;
        pClient->clearTimerB() ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
        if( m_searching && exhaustedAllTargets() ) forwardBestResponse() ;
    }
    //final invite response timer
    void ProxyCore::timerC(boost::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_debug) << "timer C fired for a client transaction" ;
        assert( pClient->getTransactionState() == ClientTransaction::proceeding || 
             pClient->getTransactionState() == ClientTransaction::calling ) ;
        pClient->clearTimerC() ;
        if( pClient->getTransactionState() == ClientTransaction::proceeding ) {
            msg_t* msg = m_pServerTransaction->msgDup() ;
            pClient->cancelRequest(msg) ;
            msg_ref( msg ) ;
        }
        m_pServerTransaction->generateResponse(408) ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
        if( m_searching && exhaustedAllTargets() ) forwardBestResponse() ;
    }
    //completed state timer
    void ProxyCore::timerD(boost::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_debug) << "timer D fired for a client transaction" ;
        assert( pClient->getTransactionState() == ClientTransaction::completed ) ;
        pClient->clearTimerD() ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
    }

    int ProxyCore::startRequests() {
        
        if( !m_searching ) {
            DR_LOG(log_debug) << "startRequests: Proxy is completed so not starting any new requests"; 
            return 0 ;
        }

        int count = 0 ;
        int idx = 0 ;
        vector< boost::shared_ptr<ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for( ; it != m_vecClientTransactions.end(); ++it, idx++ ) {
            DR_LOG(log_debug) << "startRequests: evaluating client " << idx ; 
            boost::shared_ptr<ClientTransaction> pClient = *it ;
            if( ClientTransaction::not_started == pClient->getTransactionState() ) {
                DR_LOG(log_debug) << "launching client " << idx ;
                msg_t* msg = m_pServerTransaction->msgDup();
                bool sent = pClient->forwardRequest(msg, m_headers) ;
                msg_unref( msg ) ;
                if( sent ) count++ ;
                if( sent && ProxyCore::serial == getLaunchType() ) {
                    break ;
                }
            }
        }
        DR_LOG(log_debug) << "startRequests: started " << dec << count << " clients"; 
        return count ;
    }
    void ProxyCore::cancelOutstandingRequests() {
        m_searching = false ;
        vector< boost::shared_ptr<ProxyCore::ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for( ; it != m_vecClientTransactions.end(); ++it ) {
            boost::shared_ptr<ProxyCore::ClientTransaction> pClient = *it ;
            msg_t* msg = m_pServerTransaction->msgDup() ;
            pClient->cancelRequest(msg) ;
            msg_unref(msg) ;
        }        
    }

    const string& ProxyCore::getTransactionId() { return m_transactionId; }
    tport_t* ProxyCore::getTport() { return m_tp; }
    const sip_record_route_t* ProxyCore::getMyRecordRoute(void) {
        return theOneAndOnlyController->getMyRecordRoute() ;
    }
    bool ProxyCore::allClientsAreTerminated(void) {
        vector< boost::shared_ptr<ProxyCore::ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for(; it != m_vecClientTransactions.end(); ++it ) {
            boost::shared_ptr<ProxyCore::ClientTransaction> pClient = *it ;
            if( ClientTransaction::completed != pClient->getTransactionState() && 
                ClientTransaction::terminated != pClient->getTransactionState() ) return false ;
        }
        return true ;
    }
    bool ProxyCore::processResponse(msg_t* msg, sip_t* sip) {
        bool handled = false ;
        int status = sip->sip_status->st_status  ;
        vector< boost::shared_ptr<ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for( ; it != m_vecClientTransactions.end() && !handled; ++it ) {
            boost::shared_ptr<ClientTransaction> pClient = *it ;
            if( pClient->processResponse( msg, sip ) ) {
                handled = true ;
            }
        }
        removeTerminated() ;
        if( status > 200 ) startRequests() ;

        if( m_searching && exhaustedAllTargets() ) {
            forwardBestResponse() ;
        }
        DR_LOG(log_debug) << "ProxyCore::processResponse - done" ;
        return handled ;
    }
    bool ProxyCore::forwardPrack( msg_t* msg, sip_t* sip ) {
        uint32_t rseq = m_pServerTransaction->getReplacedRSeq( sip->sip_rack->ra_response ) ;
        boost::shared_ptr< ClientTransaction > pClient ;
        vector< boost::shared_ptr< ClientTransaction > >::const_iterator it = std::find_if( m_vecClientTransactions.begin(), 
            m_vecClientTransactions.end(), has_rseq(rseq) ) ;
        if( m_vecClientTransactions.end() != it ) {
            pClient = *it ;
            pClient->forwardPrack( msg, sip ) ;
            return true ;
        }
        return false ;
    }
    bool ProxyCore::exhaustedAllTargets() {
        return  m_vecClientTransactions.end() == std::find_if( m_vecClientTransactions.begin(), m_vecClientTransactions.end(), 
            ClientTransactionIsCallingOrProceeding ) ;
    }
    void ProxyCore::forwardBestResponse() {
        m_searching = false ;
        std::sort( m_vecClientTransactions.begin(), m_vecClientTransactions.end(), bestResponseOrder ) ;
        if( 0 == m_vecClientTransactions.size() || ClientTransaction::completed != m_vecClientTransactions.at(0)->getTransactionState() ) {
            DR_LOG(log_debug) << "forwardBestResponse - sending 408 as there are no canidate final responses"  ;
            m_pServerTransaction->generateResponse( 408 ) ;
        }
        else {
            msg_t* msg = m_vecClientTransactions.at(0)->getFinalResponse() ;
            assert( msg ) ;

            DR_LOG(log_debug) << "forwardBestResponse - selected " << m_vecClientTransactions.at(0)->getSipStatus() << 
                " as best non-success status to return"  ;

            msg_ref(msg); 
            m_pServerTransaction->forwardResponse( msg, sip_object(msg) ) ;      
        }

        if( m_fullResponse  ) {
            theOneAndOnlyController->getClientController()->route_api_response( getClientMsgId(), "OK", "done" ) ;
         }             
    }

    ///SipProxyController
    SipProxyController::SipProxyController( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone), 
        m_agent(pController->getAgent())   {

            assert(m_agent) ;
            nta = m_agent ;
            theProxyController = this ;
            m_pTQM = boost::make_shared<SipTimerQueueManager>( pController->getRoot() ) ;
    }
    SipProxyController::~SipProxyController() {
    }

    void SipProxyController::proxyRequest( const string& clientMsgId, const string& transactionId, bool recordRoute, 
        bool fullResponse, bool followRedirects, bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, 
        const vector<string>& vecDestinations, const string& headers )  {

        DR_LOG(log_debug) << "SipProxyController::proxyRequest - transactionId: " << transactionId ;
        boost::shared_ptr<PendingRequest_t> p = m_pController->getPendingRequestController()->findAndRemove( transactionId ) ;
        if( !p) {
            string failMsg = "Unknown transaction id: " + transactionId ;
            DR_LOG(log_error) << "SipProxyController::proxyRequest - " << failMsg;  
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", failMsg) ;
            return ;
        }
        else {
            addProxy( clientMsgId, transactionId, p->getMsg(), p->getSipObject(), p->getTport(), recordRoute, fullResponse, followRedirects, 
                simultaneous, provisionalTimeout, finalTimeout, vecDestinations, headers ) ;
        }

        su_msg_r m = SU_MSG_R_INIT ;
        int rv = su_msg_create( m, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneProxy, sizeof( SipProxyController::ProxyData ) );
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error allocating message") ;
            return  ;
        }
        void* place = su_msg_data( m ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        ProxyData* msgData = new(place) ProxyData( clientMsgId, transactionId ) ;
        rv = su_msg_send(m);  
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error sending message") ;
            return  ;
        }
        
        return  ;
    } 
    void SipProxyController::doProxy( ProxyData* pData ) {
        string transactionId = pData->getTransactionId()  ;
        DR_LOG(log_debug) << "SipProxyController::doProxy - transactionId: " <<transactionId ;

        boost::shared_ptr<ProxyCore> p = getProxyByTransactionId( transactionId ) ;
        if( !p ) {
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", "transaction id no longer exists") ;
        }
        else {

            msg_t* msg = p->msg() ;
            sip_t* sip = sip_object(msg);

            if( sip->sip_max_forwards && sip->sip_max_forwards->mf_count <= 0 ) {
                DR_LOG(log_error) << "SipProxyController::doProxy rejecting request due to max forwards used up " << sip->sip_call_id->i_id ;
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                    "Rejected with 483 Too Many Hops due to Max-Forwards value of 0" ) ;

                msg_t* reply = nta_msg_create(nta, 0) ;
                msg_ref(reply) ;
                nta_msg_mreply( nta, reply, sip_object(reply), SIP_483_TOO_MANY_HOPS, 
                    msg_ref(p->msg()), //because it will lose a ref in here
                    TAG_END() ) ;

                Cdr::postCdr( boost::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );

                msg_unref(reply) ;

                removeProxyByTransactionId( transactionId )  ;
                pData->~ProxyData() ; 
                return ;
            }

            //stateful proxy sends 100 Trying
            nta_msg_treply( m_agent, msg_dup(msg), 100, NULL, TAG_END() ) ;                
 
            int clients = p->startRequests() ;

            //check to make sure we got at least one request out
            if( 0 == clients ) {
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", "error proxying request to " ) ;
                DR_LOG(log_error) << "Error proxying request; please check that this is a valid SIP Request-URI and retry" ;

                msg_t* reply = nta_msg_create(nta, 0) ;
                msg_ref(reply) ;
                nta_msg_mreply( nta, reply, sip_object(reply), 500, NULL, 
                    msg_ref(p->msg()), //because it will lose a ref in here
                    TAG_END() ) ;

                Cdr::postCdr( boost::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );

                msg_unref(reply) ;

                removeProxyByTransactionId( transactionId ) ;
             }
            else if( !p->wantsFullResponse() ) {
                m_pController->getClientController()->route_api_response( p->getClientMsgId(), "OK", "done" ) ;
            }
        }

        //N.B.: we must explicitly call the destructor of an object allocated with placement new
        pData->~ProxyData() ; 

    }
    bool SipProxyController::processResponse( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;
        DR_LOG(log_debug) << "SipProxyController::processResponse " << std::dec << sip->sip_status->st_status << " " << callId ;

        boost::shared_ptr<ProxyCore> p = getProxyByCallId( sip->sip_call_id->i_id ) ;

        if( !p ) return false ;

        //search for a matching client transaction to handle the response
        if( !p->processResponse( msg, sip ) ) {
            DR_LOG(log_debug)<< "processResponse - forwarding upstream (not handled by client transactions)" << callId ;
            nta_msg_tsend( nta, msg, NULL, TAG_END() ) ;  
            return true ;          
        }
        DR_LOG(log_debug) << "SipProxyController::processResponse exiting " ;
        return true ;
    }
    bool SipProxyController::processRequestWithRouteHeader( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;
        string transactionId ;

        DR_LOG(log_debug) << "SipProxyController::processRequestWithRouteHeader " << callId ;

        sip_route_remove( msg, sip) ;

        //generate cdrs on BYE
        if( sip_method_bye == sip->sip_request->rq_method ) {

            Cdr::postCdr( boost::make_shared<CdrStop>( msg, "network", Cdr::normal_release ) );
        }

        if( sip_method_prack == sip->sip_request->rq_method ) {
            boost::shared_ptr<ProxyCore> p = getProxyByCallId( sip->sip_call_id->i_id ) ;
            if( !p ) {
               DR_LOG(log_error) << "SipProxyController::processRequestWithRouteHeader unknown call-id for PRACK " <<  
                    sip->sip_call_id->i_id ;
                nta_msg_discard( nta, msg ) ;
                return true;                
            }
            p->forwardPrack( msg, sip ) ;
            return true ;
        }
        int rc = nta_msg_tsend( nta, msg_ref(msg), NULL, 
            TAG_END() ) ;
        if( rc < 0 ) {
            msg_unref(msg) ;
            DR_LOG(log_error) << "SipProxyController::processRequestWithRouteHeader failed proxying request " << callId << ": error " << rc ; 
            return false ;
        }

        if( sip_method_bye == sip->sip_request->rq_method ) {
            Cdr::postCdr( boost::make_shared<CdrStop>( msg, "application", Cdr::normal_release ) );            
        }

        msg_unref(msg) ;

        return true ;
    }
    bool SipProxyController::processRequestWithoutRouteHeader( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;

        boost::shared_ptr<ProxyCore> p = getProxyByCallId( sip->sip_call_id->i_id ) ;
        if( !p ) {
            DR_LOG(log_error) << "SipProxyController::processRequestWithoutRouteHeader unknown call-id for " <<  
                sip->sip_request->rq_method_name << " " << sip->sip_call_id->i_id ;
            nta_msg_discard( nta, msg ) ;
            return false ;
        }

        if( sip_method_ack == sip->sip_request->rq_method ) {
            //TODO: this is wrong:
            //1. We may get ACKs for success if we Record-Route'd...(except we'd be terminated immediately after sending 200OK)
            //2. What about PRACK ? (PRACK will either have route header or will not come through us)
            DR_LOG(log_debug) << "SipProxyController::processRequestWithoutRouteHeader discarding ACK for non-success response " <<  
                sip->sip_call_id->i_id ;
            nta_msg_discard( nta, msg ) ;
            return true ;
        }

        bool bRetransmission = p->isRetransmission( sip ) ;

        if( bRetransmission ) {
            DR_LOG(log_debug) << "Discarding retransmitted message since we are a stateful proxy" ;
            nta_msg_discard( nta, msg ) ;
            return false ;
        }

        //I think we only expect a CANCEL to come through here
        assert( sip_method_cancel == sip->sip_request->rq_method ) ;

        nta_msg_treply( nta, msg, 200, NULL, TAG_END() ) ;  //200 OK to the CANCEL
        p->generateResponse( 487 ) ;   //487 to INVITE

        p->cancelOutstandingRequests() ;
    
        return true ;
    }
    bool SipProxyController::isProxyingRequest( msg_t* msg, sip_t* sip )  {
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( sip->sip_call_id->i_id ) ;
      return it != m_mapCallId2Proxy.end() ;
    }

    boost::shared_ptr<ProxyCore> SipProxyController::removeProxyByTransactionId( const string& transactionId ) {
      boost::shared_ptr<ProxyCore> p ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapTxnId2Proxy::iterator it = m_mapTxnId2Proxy.find( transactionId ) ;
      if( it != m_mapTxnId2Proxy.end() ) {
        p = it->second ;
        m_mapTxnId2Proxy.erase(it) ;
        mapCallId2Proxy::iterator it2 = m_mapCallId2Proxy.find( sip_object( p->msg() )->sip_call_id->i_id ) ;
        assert( it2 != m_mapCallId2Proxy.end()) ;
        m_mapCallId2Proxy.erase( it2 ) ;
      }
      assert( m_mapTxnId2Proxy.size() == m_mapCallId2Proxy.size() );
      DR_LOG(log_debug) << "SipProxyController::removeProxyByTransactionId - there are now " << m_mapTxnId2Proxy.size() << " proxy instances" ;
      return p ;
    }
    boost::shared_ptr<ProxyCore> SipProxyController::removeProxyByCallId( const string& callId ) {
      boost::shared_ptr<ProxyCore> p ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( callId ) ;
      if( it != m_mapCallId2Proxy.end() ) {
        p = it->second ;
        m_mapCallId2Proxy.erase(it) ;
        mapTxnId2Proxy::iterator it2 = m_mapTxnId2Proxy.find( p->getTransactionId() ) ;
        assert( it2 != m_mapTxnId2Proxy.end()) ;
        m_mapTxnId2Proxy.erase( it2 ) ;
      }
      assert( m_mapTxnId2Proxy.size() == m_mapCallId2Proxy.size() );
      DR_LOG(log_debug) << "SipProxyController::removeProxyByTransactionId - there are now " << m_mapCallId2Proxy.size() << " proxy instances" ;
      return p ;
    }
    void SipProxyController::removeProxy( boost::shared_ptr<ProxyCore> pCore ) {
        removeProxyByCallId( sip_object( pCore->msg() )->sip_call_id->i_id ) ;
    }

    bool SipProxyController::isTerminatingResponse( int status ) {
        switch( status ) {
            case 200:
            case 486:
            case 603:
                return true ;
            default:
                return false ;
        }
    }

    boost::shared_ptr<ProxyCore>  SipProxyController::addProxy( const string& clientMsgId, const string& transactionId, 
        msg_t* msg, sip_t* sip, tport_t* tp, bool recordRoute, bool fullResponse, bool followRedirects,
        bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, vector<string> vecDestination, 
        const string& headers ) {

      boost::shared_ptr<ProxyCore> p = boost::make_shared<ProxyCore>( clientMsgId, transactionId, tp, recordRoute, 
        fullResponse, simultaneous, headers ) ;
      p->shouldFollowRedirects( followRedirects ) ;
      p->initializeTransactions( msg, vecDestination ) ;
      
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      m_mapCallId2Proxy.insert( mapCallId2Proxy::value_type(sip->sip_call_id->i_id, p) ) ;
      m_mapTxnId2Proxy.insert( mapTxnId2Proxy::value_type(p->getTransactionId(), p) ) ;   
      return p ;         
    }
    void SipProxyController::logStorageCount(void)  {
        boost::lock_guard<boost::mutex> lock(m_mutex) ;

        DR_LOG(log_info) << "SipProxyController storage counts"  ;
        DR_LOG(log_info) << "----------------------------------"  ;
        DR_LOG(log_info) << "m_mapCallId2Proxy size:                                          " << m_mapCallId2Proxy.size()  ;
        DR_LOG(log_info) << "m_mapTxnId2Proxy size:                                           " << m_mapTxnId2Proxy.size()  ;
        m_pTQM->logQueueSizes() ;
    }


} ;
