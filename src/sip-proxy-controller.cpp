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
#include <regex>

#include <sofia-sip/sip_util.h>
#include <sofia-sip/msg_header.h>
#include <sofia-sip/msg_addr.h>
#include <sofia-sip/msg_header.h>

#include "sip-proxy-controller.hpp"
#include "controller.hpp"
#include "pending-request-controller.hpp"
#include "cdr.hpp"
#include "sip-transports.hpp"

static drachtio::SipProxyController* theProxyController = NULL ;

#define NTA (theOneAndOnlyController->getAgent())

namespace {
    void cloneProxy(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipProxyController::ProxyData* d = reinterpret_cast<drachtio::SipProxyController::ProxyData*>( arg ) ;
        pController->getProxyController()->doProxy(d) ;
    }
} ;


namespace drachtio {
    struct without_nonce
    {
        without_nonce(const string& nonce) : m_nonce(nonce) {}
        string m_nonce;
        bool operator()(std::shared_ptr<ProxyCore::ClientTransaction> pClient)
        {
            msg_t* msg = pClient->getFinalResponse() ;
            if( msg ) {
                sip_www_authenticate_t* auth = sip_object( msg  )->sip_www_authenticate ;
                if( auth ) {
                    const char* nonce = msg_header_find_param(auth->au_common, "nonce") ;
                    return 0 != m_nonce.compare( nonce ) ;
                }
            }
            return true ;
        }
    };
    struct has_rseq
    {
        has_rseq(uint32_t rseq) : m_rseq(rseq) {}
        uint32_t m_rseq;
        bool operator()(std::shared_ptr<ProxyCore::ClientTransaction> pClient)
        {
            return m_rseq == pClient->getRSeq(); 
        }
    };
    struct has_target
    {
        has_target(const string& target) : m_target(target) {}
        string m_target;
        bool operator()(std::shared_ptr<ProxyCore::ClientTransaction> pClient)
        {
            return 0 == m_target.compare( pClient->getTarget() ); 
        }
    };

    bool ClientTransactionIsTerminatedOrNotStarted( const std::shared_ptr<ProxyCore::ClientTransaction> pClient ) {
        return pClient->getTransactionState() == ProxyCore::ClientTransaction::terminated ||
               pClient->getTransactionState() == ProxyCore::ClientTransaction::not_started ; 
    }
    bool ClientTransactionIsTerminated( const std::shared_ptr<ProxyCore::ClientTransaction> pClient ) {
        return pClient->getTransactionState() == ProxyCore::ClientTransaction::terminated ;
    }
    bool ClientTransactionIsCallingOrProceeding( const std::shared_ptr<ProxyCore::ClientTransaction> pClient ) {
        return pClient->getTransactionState() == ProxyCore::ClientTransaction::calling ||
            pClient->getTransactionState() == ProxyCore::ClientTransaction::proceeding;
    }
    bool bestResponseOrder( std::shared_ptr<ProxyCore::ClientTransaction> c1, std::shared_ptr<ProxyCore::ClientTransaction> c2 ) {
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
    ProxyCore::ServerTransaction::ServerTransaction(std::shared_ptr<ProxyCore> pCore, msg_t* msg) : 
        m_pCore(pCore), m_msg(msg), m_canceled(false), m_sipStatus(0), m_rseq(0), m_bAlerting(false) {

        msg_ref_create(m_msg) ;
        m_timeArrive = std::chrono::steady_clock::now();
    }
    ProxyCore::ServerTransaction::~ServerTransaction() {
        DR_LOG(log_debug) << "ServerTransaction::~ServerTransaction" ;
        msg_destroy(m_msg) ;
    }
    msg_t* ProxyCore::ServerTransaction::msgDup() {
        return msg_dup( m_msg ) ;
    }
    uint32_t ProxyCore::ServerTransaction::getReplacedRSeq( uint32_t rseq ) {
        //DR_LOG(log_debug) << "searching for original rseq that was replaced with " << rseq << " map size " << m_mapAleg2BlegRseq.size();
        uint32_t original = 0 ;
        std::unordered_map<uint32_t,uint32_t>::const_iterator it = m_mapAleg2BlegRseq.find(rseq) ;
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
            m_mapAleg2BlegRseq.insert( make_pair(m_rseq,sip->sip_rseq->rs_response) ) ;

            sip->sip_rseq->rs_response = m_rseq; 
        }

        // we need to cache source address / port / transport for dialogs where UAC contact is in the .invalid domain
        if( (sip->sip_cseq->cs_method == sip_method_subscribe && sip->sip_status->st_status == 202) ||
            (sip->sip_cseq->cs_method == sip_method_register && sip->sip_status->st_status == 200) ) {

            sip_t* sipReq = sip_object( m_msg ) ;
            sip_contact_t* contact = sipReq->sip_contact ;
            if( contact ) {
                if( NULL != strstr( contact->m_url->url_host, ".invalid")  ) {
                    bool add = true ;
                    int expires = 0 ;

                    std::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
                    assert( pCore ) ;

                    if( sip->sip_cseq->cs_method == sip_method_subscribe ) {
                        if(  0 == strcmp( sip->sip_subscription_state->ss_substate, "terminated" ) ) {
                            add = false ;
                        }
                        else {
                            expires = ::atoi( sip->sip_subscription_state->ss_expires ) ;
                        }                        
                    }
                    else {
                        if( NULL != sip->sip_contact && NULL != sip->sip_contact->m_expires ) {
                            expires = ::atoi( sip->sip_contact->m_expires ) ;
                        }        
                        else {
                            expires = 0 ;
                        }
                        add = expires > 0 ;
                    }
                    
                    if( add ) {
                        theOneAndOnlyController->cacheTportForSubscription( contact->m_url->url_user, contact->m_url->url_host, expires, pCore->getTport() ) ;
                    }
                    else {
                        theOneAndOnlyController->flushTportForSubscription( contact->m_url->url_user, contact->m_url->url_host ) ;                        
                    }
                }
            }
        }
/*
        bool bReplaceRR = false ;
        string newRR ;
        if( sip->sip_record_route && theOneAndOnlyController->hasPublicAddress() ) {
            std::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
            assert( pCore ) ;

            if( pCore->shouldAddRecordRoute() ) {
                bReplaceRR = true ;
                string publicAddress ;
                theOneAndOnlyController->getPublicAddress( publicAddress ) ;
                DR_LOG(log_error) << "ServerTransaction::forwardResponse replacing record route with " << publicAddress ; 

                // update record route
                su_free( msg_home(msg), sip->sip_record_route);
                sip->sip_record_route = NULL ;
                newRR = "<sip:" + publicAddress + ";lr>" ;
                //su_realloc(msg_home(msg), (void *) sip->sip_record_route->r_url[0].url_host, publicAddress.length() + 1 );
                //memset( (void *) sip->sip_record_route->r_url[0].url_host, 0, publicAddress.length() + 1 ) ;
                //strcpy( (char *) sip->sip_record_route->r_url[0].url_host, publicAddress.c_str() ) ;
            }
        }
*/
        int rc = nta_msg_tsend( NTA, msg_ref_create(msg), NULL,
            TAG_IF( reliable, SIPTAG_RSEQ(sip->sip_rseq) ),
            //TAG_IF( bReplaceRR, SIPTAG_RECORD_ROUTE_STR(newRR.c_str()) ),
            TAG_END() ) ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "ServerTransaction::forwardResponse failed proxying response " << std::dec << 
                sip->sip_status->st_status << " " << sip->sip_call_id->i_id << ": error " << rc ; 
            msg_destroy(msg) ;
            return false ;            
        }
        bool bRetransmitFinal = m_sipStatus >= 200 &&  sip->sip_status->st_status >= 200 ;
        if( !bRetransmitFinal ) m_sipStatus = sip->sip_status->st_status ;

        if( !bRetransmitFinal && sip->sip_cseq->cs_method == sip_method_invite && sip->sip_status->st_status >= 200 ) {
            writeCdr( msg, sip ) ;
        }

        // stats
        if (theOneAndOnlyController->getStatsCollector().enabled()) {
            if (m_sipStatus >= 200) {
                STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {
                    {"method", sip->sip_cseq->cs_method_name},
                    {"code", boost::lexical_cast<std::string>(sip->sip_status->st_status)}
                })
            }
            if (sip->sip_cseq->cs_method == sip_method_invite) {
                auto now = std::chrono::steady_clock::now();
                std::chrono::duration<double> diff = now - this->getArrivalTime();
                if (!this->hasAlerted() && m_sipStatus >= 180) {
                    this->alerting();
                    STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_PDD_IN, diff.count())
                }
                if (m_sipStatus == 200) {
                    STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_RESPONSE_TIME_IN, diff.count())
                }

            }

        }

        msg_destroy(msg) ;
        return true ;
    }
    void ProxyCore::ServerTransaction::writeCdr( msg_t* msg, sip_t* sip ) {
        if( 200 == sip->sip_status->st_status ) {
            Cdr::postCdr( std::make_shared<CdrStart>( msg, "application", Cdr::proxy_uas ) );                
        }
        else if( sip->sip_status->st_status > 200 ) {
            Cdr::postCdr( std::make_shared<CdrStop>( msg, "application",
                487 == sip->sip_status->st_status ? Cdr::call_canceled : Cdr::call_rejected ) );
        }        
    }
    bool ProxyCore::ServerTransaction::generateResponse( int status, const char *szReason ) {
       msg_t* reply = nta_msg_create(NTA, 0) ;
        msg_ref_create(reply) ;
        nta_msg_mreply( NTA, reply, sip_object(reply), status, szReason, 
            msg_ref_create(m_msg), //because it will lose a ref in here
            TAG_END() ) ;

        if( sip_method_invite == sip_object(m_msg)->sip_request->rq_method && status >= 200 ) {
            Cdr::postCdr( std::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );
        }

        msg_destroy(reply) ;  

        return true ;      
    }


    ///ClientTransaction
    ProxyCore::ClientTransaction::ClientTransaction(std::shared_ptr<ProxyCore> pCore, 
        std::shared_ptr<TimerQueueManager> pTQM,  const string& target) : 
        m_pCore(pCore), m_target(target), m_canceled(false), m_sipStatus(0),
        m_timerA(NULL), m_timerB(NULL), m_timerC(NULL), m_timerD(NULL), m_timerProvisional(NULL), 
        m_timerE(NULL), m_timerF(NULL), m_timerK(NULL),
        m_msgFinal(NULL), m_bAlerting(false),
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
        removeTimer( m_timerE, "timerE" ) ;
        removeTimer( m_timerF, "timerF" ) ;
        removeTimer( m_timerK, "timerK" ) ;
        removeTimer( m_timerProvisional, "timerProvisional" ) ;

        if( m_msgFinal ) { 
            msg_destroy( m_msgFinal ) ;
            m_msgFinal = NULL ;
        }
    }
    const char* ProxyCore::ClientTransaction::getStateName( State_t state) {
        static const char* szNames[] = {
            "NOT STARTED",
            "TRYING",
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

        std::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        DR_LOG(log_info) << "ClientTransaction::setState - " << getStateName(m_state) << " --> " << getStateName(newState) << " " << pCore->getCallId();

        m_state = newState ;

        //set transaction state timers
        if( this->isInviteTransaction() ) {
            switch( m_state ) {
                case calling:
                    assert( !m_timerA ) ; //TODO: should only be doing this on unreliable transports
                    assert( !m_timerB ) ;
                    assert( !m_timerC ) ;

                    //timer A = retransmission timer 
                    m_timerA = m_pTQM->addTimer("timerA", 
                        std::bind(&ProxyCore::timerA, pCore, shared_from_this()), NULL, m_durationTimerA = NTA_SIP_T1 ) ;

                    //timer B = timeout when all invite retransmissions have been exhausted
                    m_timerB = m_pTQM->addTimer("timerB", 
                        std::bind(&ProxyCore::timerB, pCore, shared_from_this()), NULL, TIMER_B_MSECS ) ;
                    
                    //timer C - timeout to wait for final response before returning 408 Request Timeout. 
                    m_timerC = m_pTQM->addTimer("timerC", 
                        std::bind(&ProxyCore::timerC, pCore, shared_from_this()), NULL, TIMER_C_MSECS ) ;

                    if( pCore->getProvisionalTimeout() > 0 ) {
                        m_timerProvisional = m_pTQM->addTimer("timerProvisional", 
                            std::bind(&ProxyCore::timerProvisional, pCore, shared_from_this()), NULL, pCore->getProvisionalTimeout() ) ;
                    }
                    m_timeArrive = std::chrono::steady_clock::now();

                break ;

                case proceeding:
                    removeTimer( m_timerA, "timerA" ) ;
                    removeTimer( m_timerB, "timerB" ) ;
                    removeTimer( m_timerProvisional, "timerProvisional" ) ;
                break; 

                case completed:
                    removeTimer( m_timerA, "timerA" ) ;
                    removeTimer( m_timerB, "timerB" ) ;
                    removeTimer( m_timerC, "timerC" ) ;
                    removeTimer( m_timerProvisional, "timerProvisional" ) ;

                    //timer D - timeout when transaction can move from completed state to terminated
                    //note: in the case of a late-arriving provisional response after we've decided to cancel an invite, 
                    //we can have a timer D set when we get here as state will go 
                    //CALLING --> COMPLETED (when decide to cancel) --> PROCEEDING (when late response arrives) --> COMPLETED (as we send the CANCEL)
                    removeTimer( m_timerD, "timerD" ) ;
                    m_timerD = m_pTQM->addTimer("timerD", std::bind(&ProxyCore::timerD, pCore, shared_from_this()), 
                        NULL, TIMER_D_MSECS ) ;
                break ;

                case terminated:
                    removeTimer( m_timerA, "timerA" ) ;
                    removeTimer( m_timerB, "timerB" ) ;
                    removeTimer( m_timerC, "timerC" ) ;
                    removeTimer( m_timerProvisional, "timerProvisional" ) ;
                break ;

                default:
                break; 
            }
        }
        else {
            // non-INVITE state machine
            switch( m_state ) {
                case trying:
                    //start timer E: retransmission timer for non-INVITE (UDP only)
                    //start timer F: non-INVITE transaction timeout

                    assert( !m_timerE ) ; //TODO: should only be doing this on unreliable transports
                    assert( !m_timerF ) ;
                    m_timerE = m_pTQM->addTimer("timerE", 
                        std::bind(&ProxyCore::timerE, pCore, shared_from_this()), NULL, m_durationTimerA = NTA_SIP_T1 ) ;
                    m_timerF = m_pTQM->addTimer("timerF", 
                        std::bind(&ProxyCore::timerF, pCore, shared_from_this()), NULL, 64 * NTA_SIP_T1 ) ;
                break ;

                case proceeding: 
                    //we've received a provisional response to a non-INVITE (rare, but possible)
                    removeTimer( m_timerE, "timerE" ) ;
                break ;

                case completed: 
                    //we've received a final response to a non-INVITE request
                    removeTimer( m_timerE, "timerE" ) ;
                    removeTimer( m_timerF, "timerF" ) ;
                    m_timerK = m_pTQM->addTimer("timerK", std::bind(&ProxyCore::timerK, pCore, shared_from_this()), 
                        NULL, NTA_SIP_T4 ) ;
                break ;

                case terminated:
                    removeTimer( m_timerE, "timerE" ) ;
                    removeTimer( m_timerF, "timerF" ) ;
                    removeTimer( m_timerK, "timerK" ) ;
                break ;

                default:
                break ;
            }            
        }
    }
    bool ProxyCore::ClientTransaction::forwardPrack(msg_t* msg, sip_t* sip) {
        std::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        assert( sip->sip_rack ) ;
        assert( sip_method_prack == sip->sip_request->rq_method ) ;
        assert( 0 != m_rseq ) ;

        sip->sip_rack->ra_response = m_rseq ;

        string random ;
        generateUuid( random ) ;
        m_branchPrack = string(rfc3261prefix) + random ;

        string record_route ;
        bool hasRoute = NULL != sip->sip_route ;
        if( hasRoute ) {
            tport_t* tp ;
            char buf[255];
            url_e(buf, 255, sip->sip_route->r_url);
            int rc = nta_get_outbound_tport_name_for_url( theOneAndOnlyController->getAgent(), theOneAndOnlyController->getHome(), 
                        URL_STRING_MAKE(buf), (void **) &tp ) ;
            assert( 0 == rc ) ;
            if( 0 == rc ) {
                std::shared_ptr<SipTransport> p = SipTransport::findTransport(tp) ;
                assert(p) ;

                p->getContactUri(record_route) ;
                record_route = "<" + record_route + ";lr>";
                DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardPrack - record route will be " << record_route ;
            }
        }
 
        int rc = nta_msg_tsend( NTA, 
            msg, 
            NULL, 
            TAG_IF( pCore->shouldAddRecordRoute() && hasRoute, 
                SIPTAG_RECORD_ROUTE_STR( record_route.c_str() ) ),
            NTATAG_BRANCH_KEY( m_branchPrack.c_str() ),
            SIPTAG_RACK( sip->sip_rack ),
            TAG_END() ) ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "forwardPrack: error forwarding request " ;
            return false ;
        }

        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", "PRACK"}})

        return true ;
    }
    bool ProxyCore::ClientTransaction::forwardRequest(msg_t* msg, const string& headers) {
        std::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        sip_t* sip = sip_object(msg) ;

        m_transmitCount++ ;

        if( not_started == m_state ) {
            assert( 1 == m_transmitCount ) ;

            string random ;
            generateUuid( random ) ;
            m_branch = string(rfc3261prefix) + random ;
            m_method = sip->sip_request->rq_method ;

            setState( this->isInviteTransaction() ? calling : trying ) ;
        }

        //Max-Forwards: decrement or set to 70 
        if( sip->sip_max_forwards ) {
            sip->sip_max_forwards->mf_count-- ;
        }
        else {
            sip_add_tl(msg, sip, SIPTAG_MAX_FORWARDS_STR("70"), TAG_END());
        }

        string route ;
        bool useOutboundProxy = theOneAndOnlyController->getConfig()->getSipOutboundProxy( route ) ;
        if( !useOutboundProxy ) {
            route = m_target ;
        }
        else if( !m_target.empty() ) {
            DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardRequest - proxying request through outbound proxy: " << route ;            
        }
        else {
            throw std::runtime_error("ProxyCore::ClientTransaction::forwardRequest: TODO: need to implement support for app providing no destination (proxy to ruri)") ;
        }

        // check if the original request-uri was a local address -- if so, replace it with the provided destination
        char urlBuf[URL_MAXLEN];
        url_e(urlBuf, URL_MAXLEN, sip->sip_request->rq_url);

        //only replace request uri if it is a local address and not a tel uri
        signed char type = sip->sip_request->rq_url->url_type;
        if((url_sip == type || url_sips == type) && isLocalSipUri(urlBuf)) {
            DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardRequest - replacing request uri because incoming request uri is local: " << urlBuf ;
            sip_request_t *rq = sip_request_format(msg_home(msg), "%s %s SIP/2.0", sip->sip_request->rq_method_name, m_target.c_str() ) ;
            msg_header_replace(msg, NULL, (msg_header_t *)sip->sip_request, (msg_header_t *) rq) ;
        }

        string record_route, transport;

        bool forceTport = false ;
        const tport_t* tp = NULL ;
        DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardRequest checking for cached tport for " << sip->sip_request->rq_url->url_user << " " << sip->sip_request->rq_url->url_host;
        std::shared_ptr<UaInvalidData> pData = theOneAndOnlyController->findTportForSubscription( sip->sip_request->rq_url->url_user, sip->sip_request->rq_url->url_host ) ;
        if (pData) {
            tp = pData->getTport() ;
            forceTport = true ;

            const tp_name_t* tpn = tport_name(tp) ;
            char name[255] ;
            sprintf(name, "%s/%s:%s", tpn->tpn_proto, tpn->tpn_host, tpn->tpn_port) ;
            transport.assign( name ) ;            
            DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardRequest found cached tport for client " << name << " " << std::hex << (void *) tp ;

            if(pCore->shouldAddRecordRoute() ) {    
                tport_t* tp_incoming = nta_incoming_transport(NTA, NULL, msg );
                tport_t* tpMe = tport_parent( tp_incoming ) ;
                const tp_name_t* tpnMe = tport_name( tp );
                string record_route = 0 == (strcmp("ws", tpnMe->tpn_proto) || strcmp("wss", tpnMe->tpn_proto) || strcmp("tls", tpnMe->tpn_proto)) ?
                    "<sips:":
                    "<sip:";
                record_route += tpnMe->tpn_host;
                record_route += ":";
                record_route += tpnMe->tpn_proto;
                if (0 != strcmp("udp", tpnMe->tpn_proto)) {
                    record_route += ";transport=";
                    record_route += tpnMe->tpn_proto;
                }
                record_route += ">";
                DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardRequest adding Record-Route " << record_route ;
            }
        }
        else {
            std::shared_ptr<SipTransport> p = SipTransport::findAppropriateTransport(m_target.c_str());

            // try with tcp if the first lookup failed, and there is no explicit transport param in the uri
            if (!p && string::npos == m_target.find("transport=")) p = SipTransport::findAppropriateTransport(m_target.c_str(), "tcp");

            // if we still don't have an appropriate transfer, return failure
            if (!p) {
                DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardRequest - no transports found, returning failure: " << route ;            
                return false;
            }
            assert(p) ;
            p->getDescription(transport);
            if(pCore->shouldAddRecordRoute() ) {
                p->getContactUri(record_route) ;
                record_route = "<" + record_route + ";lr>";
                DR_LOG(log_debug) << "ProxyCore::ClientTransaction::forwardRequest - record route will be " << record_route ;
            }

            tp = p->getTport();

        }
        tagi_t* tags = makeTags( headers, transport ) ;


        int rc = nta_msg_tsend( NTA, 
            msg_ref_create(msg), 
            URL_STRING_MAKE(route.c_str()), 
            NTATAG_TPORT(tp),
            NTATAG_BRANCH_KEY(m_branch.c_str()),
            TAG_IF(pCore->shouldAddRecordRoute() && sip_method_register != sip->sip_request->rq_method, 
                SIPTAG_RECORD_ROUTE_STR( record_route.c_str())),
            TAG_IF(pCore->shouldAddRecordRoute() && sip_method_register == sip->sip_request->rq_method, 
                SIPTAG_PATH_STR( record_route.c_str())),
            TAG_IF(pCore->shouldAddRecordRoute() && sip_method_register == sip->sip_request->rq_method, 
                SIPTAG_REQUIRE_STR( "path" )),
            TAG_NEXT(tags)
        ) ;

        deleteTags( tags ) ;

        if( rc < 0 ) {
            setState( terminated ) ;
            m_sipStatus = 503 ; //RFC 3261 16.9, but we should validate the request-uri to prevent errors sending to malformed uris
            msg_destroy(msg) ;
            return true ;
        }

        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", sip->sip_request->rq_method_name}})

        if( 1 == m_transmitCount && this->isInviteTransaction() ) {
            Cdr::postCdr( std::make_shared<CdrAttempt>( msg, "application" ) );
        }

        msg_destroy(msg) ;
        
        return true ;
    }
    bool ProxyCore::ClientTransaction::retransmitRequest(msg_t* msg, const string& headers) {
        m_durationTimerA <<= 1 ;
        if( forwardRequest(msg, headers) ) {
            DR_LOG(log_debug) << "ClientTransaction - retransmitting request, timer A/E will be set to " << dec << m_durationTimerA << "ms" ;
            std::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
            assert( pCore ) ;
            if( this->isInviteTransaction() ) {
                m_timerA = m_pTQM->addTimer("timerA", 
                    std::bind(&ProxyCore::timerA, pCore, shared_from_this()), NULL, m_durationTimerA) ;
            }
            else {
                m_timerE = m_pTQM->addTimer("timerE", 
                    std::bind(&ProxyCore::timerE, pCore, shared_from_this()), NULL, m_durationTimerA) ;                
            }
            return true ;
        }
        return false ;
     }

     bool ProxyCore::ClientTransaction::processResponse( msg_t* msg, sip_t* sip ) {
        bool bForward = false ;

        DR_LOG(log_debug) << "client processing response, via branch is " << sip->sip_via->v_branch <<
            " original request via branch was " << m_branch << " and PRACK via branch was " << m_branchPrack ;

        if( 0 != m_branch.compare(sip->sip_via->v_branch) && sip_method_prack != sip->sip_cseq->cs_method ) return false ; 
        if( 0 == m_branchPrack.compare(sip->sip_via->v_branch) && sip_method_prack == sip->sip_cseq->cs_method ) return false ;

        std::shared_ptr<ClientTransaction> me = shared_from_this() ;
        std::shared_ptr<ProxyCore> pCore = m_pCore.lock() ;
        assert( pCore ) ;

        if( terminated == m_state ) {
            DR_LOG(log_info) << "ClientTransaction::processResponse - Discarding late-arriving response because transaction is terminated " <<
                sip->sip_status->st_status << " " << sip->sip_cseq->cs_method << " " << sip->sip_call_id->i_id ;
            nta_msg_discard( NTA, msg ) ;
            return true ;
        }

        //late-arriving response to a request that is no longer desired by us, but which we did not cancel yet since there was no response
        if( m_canceled && completed == m_state && 0 == m_sipStatus && m_method == sip->sip_cseq->cs_method && this->isInviteTransaction() ) {
            DR_LOG(log_info) << "ClientTransaction::processResponse - late-arriving response to a transaction that we want to cancel " <<
                sip->sip_status->st_status << " " << sip->sip_cseq->cs_method << " " << sip->sip_call_id->i_id ;
            setState (proceeding ); 
            cancelRequest( msg ) ;
            nta_msg_discard( NTA, msg ) ;
            return true ;
        }

        //response to our original request?
        if( m_method == sip->sip_cseq->cs_method ) {

            //retransmission of final response?
            if( (completed == m_state || terminated == m_state) && sip->sip_status->st_status >= 200 ) {
                ackResponse( msg ) ;
                nta_msg_discard( NTA, msg ) ;
                return true ;               
            }
            m_sipStatus = sip->sip_status->st_status ;

            //set new state, (re)set timers
            if( m_sipStatus >= 100 && m_sipStatus <= 199 ) {
                setState( proceeding ) ;

                if( 100 != m_sipStatus && this->isInviteTransaction() ) {
                    assert( m_timerC ) ;
                    removeTimer( m_timerC, "timerC" ) ;
                    m_timerC = m_pTQM->addTimer("timerC", std::bind(&ProxyCore::timerC, pCore, shared_from_this()), 
                        NULL, TIMER_C_MSECS ) ;

                    if (theOneAndOnlyController->getStatsCollector().enabled() && !this->hasAlerted()) {
                        this->alerting();
                        auto now = std::chrono::steady_clock::now();
                        std::chrono::duration<double> diff = now - this->getArrivalTime();
                        STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_PDD_OUT, diff.count())
                    }
                }                  
            }
            else if( m_sipStatus >= 200 && m_sipStatus <= 299 ) {
                //NB: order is important here - 
                //proxy core will attempt to cancel any invite not in the terminated state
                //so set that first before announcing our success
                setState( this->isInviteTransaction() ? terminated : completed ) ;
                pCore->notifyForwarded200OK( me ) ;         

                if (theOneAndOnlyController->getStatsCollector().enabled() && this->isInviteTransaction() && 200 == m_sipStatus) {
                    auto now = std::chrono::steady_clock::now();
                    std::chrono::duration<double> diff = now - this->getArrivalTime();
                    STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_RESPONSE_TIME_OUT, diff.count())

                    if (!this->hasAlerted()) {
                        STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_PDD_OUT, diff.count())
                    }
                }
            }
            else if( m_sipStatus >= 300 ) {
                setState( completed ) ;
            }

            if( m_sipStatus >= 200 && this->isInviteTransaction() ) {
                removeTimer(m_timerC, "timerC") ;
            }

            //determine whether to forward this response upstream
            if( 100 == m_sipStatus ) {
                DR_LOG(log_debug) << "ClientTransaction::processResponse - discarding 100 Trying since we are a stateful proxy" ;
                nta_msg_discard( NTA, msg ) ;
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
                msg_ref_create( m_msgFinal ) ;

                if( this->isInviteTransaction() ) {
                    ackResponse( msg ) ;                    
                }

                //TODO: move this up to ProxyCore??
                if( m_sipStatus >= 300 && m_sipStatus <= 399 && pCore->shouldFollowRedirects() && sip->sip_contact ) {
                    vector< std::shared_ptr<ClientTransaction> > vecNewTransactions  ;
                    sip_contact_t* contact = sip->sip_contact ;
                    for (sip_contact_t* m = sip->sip_contact; m; m = m->m_next) {
                        char buffer[URL_MAXLEN] = "" ;
                        url_e(buffer, URL_MAXLEN, m->m_url) ;

                        DR_LOG(log_debug) << "ClientTransaction::processResponse -- adding contact from redirect response " << buffer ;

                        std::shared_ptr<TimerQueueManager> pTQM = theProxyController->getTimerQueueManager() ;
                        std::shared_ptr<ClientTransaction> pClient = std::make_shared<ClientTransaction>(pCore, pTQM, buffer) ;
                        vecNewTransactions.push_back( pClient ) ;
                    }
                    pCore->addClientTransactions( vecNewTransactions, shared_from_this() ) ;
                }
            }     

            //write CDRS on the UAC side for final response to an INVITE
            if(this->isInviteTransaction() && m_sipStatus >= 200 ) {
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

                string data = s + "|||continue" + DR_CRLF + encodedMessage ; 

                theOneAndOnlyController->getClientController()->route_api_response( pCore->getClientMsgId(), "OK", data ) ;   
                if( (m_sipStatus >= 200 && m_sipStatus <= 299) || (pCore->isCanceled() && pCore->exhaustedAllTargets() ) ) {
                    theOneAndOnlyController->getClientController()->route_api_response( pCore->getClientMsgId(), "OK", "done" ) ;
                 }             
            }
        }
        else if( sip_method_cancel == sip->sip_cseq->cs_method ) {
            DR_LOG(log_debug) << "Received " << sip->sip_status->st_status << " response to CANCEL" ;
            nta_msg_discard( NTA, msg ); 
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
            nta_msg_discard( NTA, msg ) ;
        }      
        return true ;
    }
    int ProxyCore::ClientTransaction::cancelRequest(msg_t* msg) {

        //cancel retransmission timers 
        removeTimer( m_timerA, "timerA" ) ;
        removeTimer( m_timerB, "timerB" ) ;
        removeTimer( m_timerC, "timerC" ) ;
        removeTimer( m_timerC, "timerE" ) ;
        removeTimer( m_timerC, "timerF" ) ;
        removeTimer( m_timerC, "timerK" ) ;

        if( calling == m_state ) {
            DR_LOG(log_debug) << "ClientTransaction::cancelRequest - client request in CALLING state has not received a response so not sending CANCEL" ;
            setState(completed) ;
            m_canceled = true ;
            return 0 ;
        }
        if( not_started == m_state ) {
            DR_LOG(log_debug) << "ClientTransactioncancelRequest - client request in NOT_STARTED state will be removed" ;
            setState(terminated) ;
            return 0 ;
        }
        if( proceeding != m_state ) {
            DR_LOG(log_debug) << "ClientTransaction::cancelRequest - returning without canceling because state is not PROCEEDING it is " << 
                getStateName(m_state) ;
            return 0 ;
        }

        sip_t* sip = sip_object(msg) ;
        msg_t *cmsg = nta_msg_create(NTA, 0);
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

        if( nta_msg_tsend( NTA, cmsg, NULL, 
            NTATAG_BRANCH_KEY(m_branch.c_str()),
            TAG_END() ) < 0 )
 
            goto err ;

        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", "CANCEL"}})

        m_canceled = true ;
        return 0;

        err:
            if( cmsg ) msg_destroy(cmsg);
            return -1;
    }
    void ProxyCore::ClientTransaction::writeCdr( msg_t* msg, sip_t* sip ) {
        string encodedMessage ;
        EncodeStackMessage( sip, encodedMessage ) ;
        if( 200 == m_sipStatus ) {
            Cdr::postCdr( std::make_shared<CdrStart>( msg, "network", Cdr::proxy_uac ), encodedMessage );                
        }               
        else {
            Cdr::postCdr( std::make_shared<CdrStop>( msg, "network",  
                487 == m_sipStatus ? Cdr::call_canceled : Cdr::call_rejected ), encodedMessage );                
        }        
    }

    ///ProxyCore
    ProxyCore::ProxyCore(const string& clientMsgId, const string& transactionId, tport_t* tp,bool recordRoute, 
        bool fullResponse, bool simultaneous, const string& headers ) : 
        m_clientMsgId(clientMsgId), m_transactionId(transactionId), m_tp(tp), m_canceled(false), m_headers(headers),
        m_fullResponse(fullResponse), m_bRecordRoute(recordRoute), m_launchType(simultaneous ? ProxyCore::simultaneous : ProxyCore::serial), 
        m_searching(true), m_nProvisionalTimeout(0) {
    }
    ProxyCore::~ProxyCore() {
        DR_LOG(log_debug) << "ProxyCore::~ProxyCore" ;
    }
    void ProxyCore::initializeTransactions( msg_t* msg, const vector<string>& vecDestination ) {
        m_pServerTransaction = std::make_shared<ServerTransaction>( shared_from_this(), msg ) ;

        for( vector<string>::const_iterator it = vecDestination.begin(); it != vecDestination.end(); it++ ) {
            std::shared_ptr<TimerQueueManager> pTQM = theProxyController->getTimerQueueManager() ;
            std::shared_ptr<ClientTransaction> pClient = std::make_shared<ClientTransaction>( shared_from_this(), pTQM, *it ) ;
            m_vecClientTransactions.push_back( pClient ) ;
        }
        DR_LOG(log_debug) << "ProxyCore::initializeTransactions - added " << dec << m_vecClientTransactions.size() << " client transactions " ;
    }
    void ProxyCore::removeTerminated(bool alsoRemoveNotStarted) {
        int nBefore =  m_vecClientTransactions.size() ;
        m_vecClientTransactions.erase(
            std::remove_if( 
                m_vecClientTransactions.begin(), 
                m_vecClientTransactions.end(), 
                alsoRemoveNotStarted ? ClientTransactionIsTerminatedOrNotStarted : ClientTransactionIsTerminated),
                m_vecClientTransactions.end() 
            ) ;
        int nAfter = m_vecClientTransactions.size() ;
        DR_LOG(log_debug) << "ProxyCore::removeTerminated: removeTerminated - removed " << dec << nBefore-nAfter << " client transactions, leaving " << nAfter ;

        if( 0 == nAfter ) {
            DR_LOG(log_debug) << "ProxyCore::removeTerminated: removeTerminated - all client TUs are terminated, removing proxy core" ;
            theProxyController->removeProxy( shared_from_this() ) ;
        }
    }
    void ProxyCore::notifyForwarded200OK( std::shared_ptr<ClientTransaction> pClient ) {
        DR_LOG(log_debug) << "forwarded 200 OK terminating other clients" ;
        m_searching = false ;
        cancelOutstandingRequests() ;
        removeTerminated(true) ;
    }
    void ProxyCore::addClientTransactions( const vector< std::shared_ptr<ClientTransaction> >& vecClientTransactions, 
        std::shared_ptr<ClientTransaction> pClient ) {

        vector< std::shared_ptr< ClientTransaction > >::iterator it = std::find_if( m_vecClientTransactions.begin(), 
            m_vecClientTransactions.end(), has_target(pClient->getTarget()) ) ;
        assert( m_vecClientTransactions.end() != it ) ;

        m_vecClientTransactions.insert( ++it, vecClientTransactions.begin(), vecClientTransactions.end() ) ;
        DR_LOG(log_debug) << "ProxyCore::addClientTransactions: there are now " << dec << vecClientTransactions.size() << " client transactions";
    }
    void ProxyCore::setProvisionalTimeout(const string& t ) {
        try {
            std::regex re("^(\\d+)(ms|s)$");
            std:smatch mr;
            if (std::regex_search(t, mr, re) && mr.size() > 1) {
                string s = mr[1] ;
                m_nProvisionalTimeout = ::atoi( s.c_str() ) ;
                if( 0 == mr[2].compare("s") ) {
                    m_nProvisionalTimeout *= 1000 ;
                }
                DR_LOG(log_debug) << "provisional timeout is " << m_nProvisionalTimeout << "ms" ;
            }
            else if( t.length() > 0 ) {
                DR_LOG(log_error) << "Invalid timeout syntax: " << t ;
            }        
        } catch( std::regex_error& e) {
            DR_LOG(log_error) << "ProxyCore::setProvisionalTimeout - regex error " << e.what();
        }
    }

    //timer functions
    

    //retransmission timer
    void ProxyCore::timerA(std::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_info) << "timer A fired for a client transaction in " << pClient->getCurrentStateName() ;
        assert( pClient->getTransactionState() == ClientTransaction::calling ) ;
        pClient->clearTimerA() ;
        msg_t* msg = m_pServerTransaction->msgDup() ;
        pClient->retransmitRequest(msg, m_headers) ;
        //msg_destroy(msg) ;
    }
    //max retransmission timer
    void ProxyCore::timerB(std::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_info) << "timer B fired for a client transaction in " << pClient->getCurrentStateName() ;
        assert( pClient->getTransactionState() == ClientTransaction::calling ) ;
        pClient->clearTimerB() ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
        if( m_searching && exhaustedAllTargets() ) forwardBestResponse() ;
    }
    //final invite response timer
    void ProxyCore::timerC(std::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_info) << "timer C fired for a client transaction in " << pClient->getCurrentStateName() ;
        assert( pClient->getTransactionState() == ClientTransaction::proceeding || 
             pClient->getTransactionState() == ClientTransaction::calling ) ;
        pClient->clearTimerC() ;
        if( pClient->getTransactionState() == ClientTransaction::proceeding ) {
            msg_t* msg = m_pServerTransaction->msgDup() ;
            pClient->cancelRequest(msg) ;
            msg_ref_create( msg ) ;
        }
        m_pServerTransaction->generateResponse(408) ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
        if( m_searching && exhaustedAllTargets() ) forwardBestResponse() ;
    }
    //completed state timer
    void ProxyCore::timerD(std::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_debug) << "timer D fired for a client transaction" ;
        assert( pClient->getTransactionState() == ClientTransaction::completed ) ;
        pClient->clearTimerD() ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
    }
    //non-INVITE retransmission timer
    void ProxyCore::timerE(std::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_info) << "timer E fired for a client transaction in " << pClient->getCurrentStateName() ;
        assert( pClient->getTransactionState() == ClientTransaction::trying ) ;
        pClient->clearTimerE() ;
        msg_t* msg = m_pServerTransaction->msgDup() ;
        pClient->retransmitRequest(msg, m_headers) ;
        //msg_destroy(msg) ;
    }
    //non-INVITE transaction timeout timer
    void ProxyCore::timerF(std::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_info) << "timer F fired for a client transaction in " << pClient->getCurrentStateName() ;
        pClient->clearTimerF() ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
    }
    //wait time for non-INVITE response retransmits
    void ProxyCore::timerK(std::shared_ptr<ClientTransaction> pClient) {
        DR_LOG(log_info) << "timer K fired for a client transaction in " << pClient->getCurrentStateName() ;
        assert( pClient->getTransactionState() == ClientTransaction::completed ) ;
        pClient->clearTimerK() ;
        pClient->setState( ClientTransaction::terminated ) ;
        removeTerminated() ;
    }
    void ProxyCore::timerProvisional( std::shared_ptr<ClientTransaction> pClient ) {
        DR_LOG(log_info) << "timer Provisional fired for a client transaction in " << pClient->getCurrentStateName() ;
        assert( pClient->getTransactionState() == ClientTransaction::calling ) ;
        pClient->clearTimerProvisional() ;
        m_canceled = true ;        
        pClient->setState( ClientTransaction::completed ) ;
        removeTerminated() ;
        startRequests() ;
        if( m_searching && exhaustedAllTargets() ) forwardBestResponse() ;
    }

    int ProxyCore::startRequests() {
        
        if( !m_searching ) {
            DR_LOG(log_debug) << "startRequests: Proxy is completed so not starting any new requests"; 
            return 0 ;
        }

        int count = 0 ;
        int idx = 0 ;
        vector< std::shared_ptr<ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for( ; it != m_vecClientTransactions.end(); ++it, idx++ ) {
            DR_LOG(log_debug) << "startRequests: evaluating client " << idx ; 
            std::shared_ptr<ClientTransaction> pClient = *it ;
            if( ClientTransaction::not_started == pClient->getTransactionState() ) {
                DR_LOG(log_debug) << "launching client " << idx ;
                msg_t* msg = m_pServerTransaction->msgDup();
                bool sent = pClient->forwardRequest(msg, m_headers) ;
                //msg_destroy( msg ) ;
                if( sent ) count++ ;
                if( sent && ProxyCore::serial == getLaunchType() ) {
                    break ;
                }
            }
        }
        DR_LOG(log_debug) << "ProxyCore::startRequests started " << dec << count << " client transactions"; 
        return count ;
    }
    void ProxyCore::cancelOutstandingRequests() {
        m_searching = false ;
        vector< std::shared_ptr<ProxyCore::ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for( ; it != m_vecClientTransactions.end(); ++it ) {
            std::shared_ptr<ProxyCore::ClientTransaction> pClient = *it ;
            msg_t* msg = m_pServerTransaction->msgDup() ;
            pClient->cancelRequest(msg) ;
            msg_destroy(msg) ;
        }        
    }

    const string& ProxyCore::getTransactionId() { return m_transactionId; }
    tport_t* ProxyCore::getTport() { return m_tp; }
    bool ProxyCore::allClientsAreTerminated(void) {
        vector< std::shared_ptr<ProxyCore::ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for(; it != m_vecClientTransactions.end(); ++it ) {
            std::shared_ptr<ProxyCore::ClientTransaction> pClient = *it ;
            if( ClientTransaction::completed != pClient->getTransactionState() && 
                ClientTransaction::terminated != pClient->getTransactionState() ) return false ;
        }
        return true ;
    }
    bool ProxyCore::processResponse(msg_t* msg, sip_t* sip) {
        bool handled = false ;
        string target ;
        int status = sip->sip_status->st_status  ;
        vector< std::shared_ptr<ClientTransaction> >::const_iterator it = m_vecClientTransactions.begin() ;
        for( ; it != m_vecClientTransactions.end() && !handled; ++it ) {
            std::shared_ptr<ClientTransaction> pClient = *it ;
            if( pClient->processResponse( msg, sip ) ) {
                handled = true ;
                target = pClient->getTarget() ;
            }
        }

        // if we get a 401 Unauthorized or 407 Proxy Authorization required, then stop searching and send back
        if(    (401 == status || 407 == status)  
            && (NULL != sip->sip_www_authenticate || NULL != sip->sip_proxy_authenticate )
            && theProxyController->addChallenge( sip, target ) ) {
            
            removeTerminated(true) ;
            forwardBestResponse() ;
        }
        else {
            removeTerminated() ;
            if( status > 200 ) startRequests() ;

            if( m_searching && exhaustedAllTargets() ) {
                forwardBestResponse() ;
            }
        }
        DR_LOG(log_debug) << "ProxyCore::processResponse - done" ;
        return handled ;
    }
    bool ProxyCore::forwardPrack( msg_t* msg, sip_t* sip ) {
        uint32_t rseq = m_pServerTransaction->getReplacedRSeq( sip->sip_rack->ra_response ) ;
        std::shared_ptr< ClientTransaction > pClient ;
        vector< std::shared_ptr< ClientTransaction > >::const_iterator it = std::find_if( m_vecClientTransactions.begin(), 
            m_vecClientTransactions.end(), has_rseq(rseq) ) ;
        if( m_vecClientTransactions.end() != it ) {
            pClient = *it ;
            pClient->forwardPrack( msg, sip ) ;
            return true ;
        }
        return false ;
    }
    bool ProxyCore::forwardRequest( msg_t* msg, sip_t* sip ) {
        std::shared_ptr< ClientTransaction > pClient ;
        string headers ;
        vector< std::shared_ptr< ClientTransaction > >::const_iterator it = std::find_if( m_vecClientTransactions.begin(), 
            m_vecClientTransactions.end(), ClientTransactionIsCallingOrProceeding ) ;
        if( m_vecClientTransactions.end() != it ) {
            pClient = *it ;
            pClient->forwardRequest( msg, headers ) ;
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
        if( 0 == m_vecClientTransactions.size() || 
            ClientTransaction::completed != m_vecClientTransactions.at(0)->getTransactionState() ||
            0 == m_vecClientTransactions.at(0)->getSipStatus() ) {
            
            DR_LOG(log_debug) << "forwardBestResponse - sending 408 as there are no candidate final responses"  ;
            m_pServerTransaction->generateResponse( 408 ) ;
        }
        else {
            msg_t* msg = m_vecClientTransactions.at(0)->getFinalResponse() ;
            assert( msg ) ;

            DR_LOG(log_debug) << "forwardBestResponse - selected " << m_vecClientTransactions.at(0)->getSipStatus() << 
                " as best non-success status to return"  ;

            msg_ref_create(msg); 
            m_pServerTransaction->forwardResponse( msg, sip_object(msg) ) ;      
        }

        if( m_fullResponse  ) {
            theOneAndOnlyController->getClientController()->route_api_response( getClientMsgId(), "OK", "done" ) ;
         }             
    }


    ///SipProxyController
    SipProxyController::SipProxyController( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone), 
        m_agent(pController->getAgent()), m_timerQueue(pController->getRoot(), "challenges")   {

            assert(m_agent) ;
            theProxyController = this ;
            m_pTQM = std::make_shared<SipTimerQueueManager>( pController->getRoot() ) ;
    }
    SipProxyController::~SipProxyController() {
    }

    void SipProxyController::proxyRequest( const string& clientMsgId, const string& transactionId, bool recordRoute, 
        bool fullResponse, bool followRedirects, bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, 
        const vector<string>& vecDestinations, const string& headers )  {

        DR_LOG(log_debug) << "SipProxyController::proxyRequest - transactionId: " << transactionId ;
       
        su_msg_r m = SU_MSG_R_INIT ;
        int rv = su_msg_create( m, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneProxy, sizeof( SipProxyController::ProxyData ) );
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error allocating message") ;
            return  ;
        }
        void* place = su_msg_data( m ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        ProxyData* msgData = new(place) ProxyData( clientMsgId, transactionId, recordRoute, fullResponse, followRedirects, 
            simultaneous, provisionalTimeout, finalTimeout, vecDestinations, headers ) ;
        rv = su_msg_send(m);  
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error sending message") ;
            return  ;
        }
        
        return  ;
    } 
    void SipProxyController::doProxy( ProxyData* pData ) {
        string transactionId = pData->getTransactionId()  ;
        string clientMsgId = pData->getClientMsgId() ;

        DR_LOG(log_debug) << "SipProxyController::doProxy - transactionId: " << transactionId << " clientMsgId: " << clientMsgId ;

        std::shared_ptr<PendingRequest_t> p = m_pController->getPendingRequestController()->findAndRemove( transactionId ) ;
        if( !p) {
            string failMsg = "Unknown transaction id: " + transactionId ;
            DR_LOG(log_error) << "SipProxyController::proxyRequest - " << failMsg;  
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", failMsg) ;
        }
        else {
            msg_t* msg = p->getMsg() ;
            sip_t* sip = sip_object(msg);
            vector<string> vecDestination ;
            string target ;

            if( isResponseToChallenge( sip, target ) ) {
                DR_LOG(log_info) << "SipProxyController::doProxy - proxying request with credentials after challenge to " << target ;
                vecDestination.push_back( target ) ;
            }
            else {
                pData->getDestinations( vecDestination ) ;
            }
            std::shared_ptr<ProxyCore> pCore = addProxy( clientMsgId, transactionId, p->getMsg(), p->getSipObject(), p->getTport(), pData->getRecordRoute(), 
                pData->getFullResponse(), pData->getFollowRedirects(), pData->getSimultaneous(), pData->getProvisionalTimeout(), 
                pData->getFinalTimeout(), vecDestination, pData->getHeaders() ) ;


            if( sip->sip_max_forwards && sip->sip_max_forwards->mf_count <= 0 ) {
                DR_LOG(log_error) << "SipProxyController::doProxy rejecting request due to max forwards used up " << sip->sip_call_id->i_id ;
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                    "Rejected with 483 Too Many Hops due to Max-Forwards value of 0" ) ;

                msg_t* reply = nta_msg_create(NTA, 0) ;
                msg_ref_create(reply) ;
                nta_msg_mreply( NTA, reply, sip_object(reply), SIP_483_TOO_MANY_HOPS, 
                    msg_ref_create(msg), //because it will lose a ref in here
                    TAG_END() ) ;

                Cdr::postCdr( std::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );

                msg_destroy(reply) ;

                removeProxy( pCore )  ;
                pData->~ProxyData() ; 
                return ;
            }
 
            // some clients (e.g. Tandberg) "preload" a Route header on the initial request
            sip_route_remove( msg, sip) ;

            int clients = pCore->startRequests() ;

            //check to make sure we got at least one request out
            if( 0 == clients ) {
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", "error proxying request to " ) ;
                DR_LOG(log_error) << "Error proxying request; please check that this is a valid SIP Request-URI and retry" ;

                msg_t* reply = nta_msg_create(NTA, 0) ;
                msg_ref_create(reply) ;
                nta_msg_mreply( NTA, reply, sip_object(reply), 500, NULL, 
                    msg_ref_create(msg), //because it will lose a ref in here
                    TAG_END() ) ;

                Cdr::postCdr( std::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );

                msg_destroy(reply) ;

                removeProxy( pCore ) ;
             }
            else if( !pCore->wantsFullResponse() ) {
                m_pController->getClientController()->route_api_response( clientMsgId, "OK", "done" ) ;
            }
//          }
        }
        //N.B.: we must explicitly call the destructor of an object allocated with placement new
        pData->~ProxyData() ; 

    }
    bool SipProxyController::processResponse( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;
        DR_LOG(log_debug) << "SipProxyController::processResponse " << std::dec << sip->sip_status->st_status << " " << callId ;

        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_IN, {
            {"method", sip->sip_cseq->cs_method_name},
            {"code", boost::lexical_cast<std::string>(sip->sip_status->st_status)}
        }) 

        // responses to PRACKs we forward downstream
        if( sip_method_prack == sip->sip_cseq->cs_method ) {
            DR_LOG(log_debug)<< "processResponse - forwarding response to PRACK downstream " << callId ;
            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {
                {"method", sip->sip_cseq->cs_method_name},
                {"code", boost::lexical_cast<std::string>(sip->sip_status->st_status)}
            }) 
            nta_msg_tsend( NTA, msg, NULL, TAG_END() ) ;  
            return true ;                      
        }
        std::shared_ptr<ProxyCore> p = getProxy( sip ) ;

        if( !p ) return false ;

        //search for a matching client transaction to handle the response
        if( !p->processResponse( msg, sip ) ) {
            DR_LOG(log_debug)<< "processResponse - forwarding upstream (not handled by client transactions)" << callId ;
            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {
                {"method", sip->sip_cseq->cs_method_name},
                {"code", boost::lexical_cast<std::string>(sip->sip_status->st_status)}
            }) 
            nta_msg_tsend( NTA, msg, NULL, TAG_END() ) ;  
            return true ;          
        }
        DR_LOG(log_debug) << "SipProxyController::processResponse exiting " ;
        return true ;
    }
    bool SipProxyController::processRequestWithRouteHeader( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;
        string transactionId ;

        DR_LOG(log_debug) << "SipProxyController::processRequestWithRouteHeader " << callId ;

        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", sip->sip_request->rq_method_name}})

        sip_route_remove( msg, sip) ;

        //generate cdrs on BYE
        if( sip_method_bye == sip->sip_request->rq_method ) Cdr::postCdr(std::make_shared<CdrStop>(msg, "network", Cdr::normal_release ));

        if( sip_method_prack == sip->sip_request->rq_method ) {
            std::shared_ptr<ProxyCore> p = getProxyByCallId( sip ) ;
            if( !p ) {
                DR_LOG(log_error) << "SipProxyController::processRequestWithRouteHeader unknown call-id for PRACK " <<  
                    sip->sip_call_id->i_id ;
                nta_msg_discard( NTA, msg ) ;
                return true;                
            }
            p->forwardPrack( msg, sip ) ;
            return true ;
        }

        bool forceTport = false ;
        const tport_t* tp = NULL ;
        if( NULL != sip->sip_request && (sip_method_invite == sip->sip_request->rq_method || 
                sip_method_options == sip->sip_request->rq_method ||
                sip_method_notify == sip->sip_request->rq_method ||
                sip_method_message == sip->sip_request->rq_method) && 
                !tport_is_dgram(tp) ) {
            std::shared_ptr<UaInvalidData> pData = theOneAndOnlyController->findTportForSubscription( sip->sip_request->rq_url->url_user, sip->sip_request->rq_url->url_host ) ;
            if( NULL != pData ) {
                tp = pData->getTport() ;
                forceTport = true ;
                DR_LOG(log_debug) << "SipProxyController::processRequestWithRouteHeader forcing tport to reach client registered over non-udp " << std::hex << (void *) tp ;
           }
        }
        
        if (!tp) {
            char buffer[256];
            char szTransport[4];
            if (sip->sip_route && sip->sip_route->r_url->url_params && url_param(sip->sip_route->r_url->url_params, "lr", NULL, 0)) {
                url_e( buffer, 255, sip->sip_route->r_url ) ;
            }
            else {
                url_e( buffer, 255, sip->sip_request->rq_url ) ;
            }

            std::shared_ptr<SipTransport> p = SipTransport::findAppropriateTransport(buffer);
            assert(p) ;

            tp = p->getTport();
            forceTport = true ;
            DR_LOG(log_debug) << "SipProxyController::processRequestWithRouteHeader forcing tport to reach route " << buffer ;
        }

        int rc = nta_msg_tsend( NTA, msg_ref_create(msg), NULL,
            TAG_IF(forceTport, NTATAG_TPORT(tp)),
            TAG_END() ) ;

        if( rc < 0 ) {
            msg_destroy(msg) ;
            DR_LOG(log_error) << "SipProxyController::processRequestWithRouteHeader failed proxying request " << callId << ": error " << rc ; 
            return false ;
        }
        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", sip->sip_request->rq_method_name}})

        if( sip_method_bye == sip->sip_request->rq_method ) {
            Cdr::postCdr( std::make_shared<CdrStop>( msg, "application", Cdr::normal_release ) );            
        }

        msg_destroy(msg) ;

        return true ;
    }
    bool SipProxyController::processRequestWithoutRouteHeader( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;

        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", sip->sip_request->rq_method_name}})

        std::shared_ptr<ProxyCore> p = getProxy( sip ) ;
        if( !p ) {
            DR_LOG(log_error) << "SipProxyController::processRequestWithoutRouteHeader unknown call-id for " <<  sip->sip_request->rq_method_name << " " << callId;
            nta_msg_discard( NTA, msg ) ;
            return false ;
        }

        if( sip_method_ack == sip->sip_request->rq_method ) {
            //TODO: this is wrong:
            //1. We may get ACKs for success if we Record-Route'd...(except we'd be terminated immediately after sending 200OK)
            //2. What about PRACK ? (PRACK will either have route header or will not come through us)
            DR_LOG(log_debug) << "SipProxyController::processRequestWithoutRouteHeader discarding ACK for non-success response " <<  callId;
            nta_msg_discard( NTA, msg ) ;
            return true ;
        }

        bool bRetransmission = p->isRetransmission( sip ) ;

        if( bRetransmission ) {
            //TODO: augment ServerTransaction to retain last response sent and resend in this case (or 100 Trying if INVITE and no responses sent yet)
            DR_LOG(log_info) << "Discarding retransmitted request since we are a stateful proxy " << sip->sip_request->rq_method_name << " " << sip->sip_call_id->i_id ;
            nta_msg_discard( NTA, msg ) ;
            return false ;
        }

        if(  sip_method_cancel == sip->sip_request->rq_method ) {
            p->setCanceled(true) ;

            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {
                {"method", sip->sip_request->rq_method_name},
                {"code", "200"}
            }) 

            nta_msg_treply( NTA, msg, 200, NULL, TAG_END() ) ;  //200 OK to the CANCEL
            p->generateResponse( 487 ) ;   //487 to INVITE

            p->cancelOutstandingRequests() ;

            return true ;            
        }
        if( sip_method_info == sip->sip_request->rq_method ) {
            // example of an INFO message during call set up is that FS and asterisk with send INFO with media_control xml (fast picture update)
            DR_LOG(log_info) << "Forwarding request during call setup " << sip->sip_request->rq_method_name << " " << sip->sip_call_id->i_id ;
            p->forwardRequest( msg, sip ) ;
            return true ;
        }

        return true ;
    }
    bool SipProxyController::isProxyingRequest( msg_t* msg, sip_t* sip )  {
      string id ;
      makeUniqueSipTransactionIdentifier(sip, id) ;
      std::lock_guard<std::mutex> lock(m_mutex) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( id ) ;
      return it != m_mapCallId2Proxy.end() ;
    }

    std::shared_ptr<ProxyCore> SipProxyController::removeProxy( sip_t* sip ) {
      string id ;
      makeUniqueSipTransactionIdentifier(sip, id); 
      std::shared_ptr<ProxyCore> p ;
      std::lock_guard<std::mutex> lock(m_mutex) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( id ) ;
      if( it != m_mapCallId2Proxy.end() ) {
        p = it->second ;
        m_mapCallId2Proxy.erase(it) ;
      }
      DR_LOG(log_debug) << "SipProxyController::removeProxyByCallId - there are now " << dec << m_mapCallId2Proxy.size() << " proxy instances" ;
      return p ;
    }
    void SipProxyController::removeProxy( std::shared_ptr<ProxyCore> pCore ) {
        removeProxy( sip_object( pCore->msg() ) ) ;
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

    std::shared_ptr<ProxyCore>  SipProxyController::addProxy( const string& clientMsgId, const string& transactionId, 
        msg_t* msg, sip_t* sip, tport_t* tp, bool recordRoute, bool fullResponse, bool followRedirects,
        bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, vector<string> vecDestination, 
        const string& headers ) {

      string id ;
      makeUniqueSipTransactionIdentifier(sip, id) ;

      DR_LOG(log_debug) << "SipProxyController::addProxy - adding transaction id " << transactionId << ", id " << 
        id << " before insert there are "<< m_mapCallId2Proxy.size() << " proxy instances";

      std::shared_ptr<ProxyCore> p = std::make_shared<ProxyCore>( clientMsgId, transactionId, tp, recordRoute, 
        fullResponse, simultaneous, headers ) ;
      p->shouldFollowRedirects( followRedirects ) ;
      p->initializeTransactions( msg, vecDestination ) ;
      if( !provisionalTimeout.empty() ) p->setProvisionalTimeout( provisionalTimeout ) ;
      
      std::lock_guard<std::mutex> lock(m_mutex) ;
      m_mapCallId2Proxy.insert( mapCallId2Proxy::value_type(id, p) ) ;
      return p ;         
    }

    bool SipProxyController::addChallenge( sip_t* sip, const string& target ) {
        const char* nonce = NULL; 
        const char* realm = NULL;
        string remoteAddress ;
        bool added = false ;

        sip_www_authenticate_t* auth = sip->sip_www_authenticate ;
        if( auth ) {
            realm = msg_header_find_param(auth->au_common, "realm") ;
            nonce = msg_header_find_param(auth->au_common, "nonce") ;
        }
        else {
            sip_proxy_authenticate_t* auth2 = sip->sip_proxy_authenticate ;
            if( auth2 ) {
                realm = msg_header_find_param(auth2->au_common, "realm") ;
                nonce = msg_header_find_param(auth2->au_common, "nonce") ;
            }
        }
        if( nonce && realm ) {
            std::shared_ptr<ChallengedRequest> pChallenge = std::make_shared<ChallengedRequest>( realm, nonce, target) ;
            m_mapNonce2Challenge.insert( mapNonce2Challenge::value_type( nonce, pChallenge) );  

            // give client 2 seconds to resend with credentials before clearing state
            TimerEventHandle handle = m_timerQueue.add( std::bind(&SipProxyController::timeoutChallenge, shared_from_this(), 
                nonce), NULL, 2000 ) ;
            pChallenge->setTimerHandle( handle ) ;
   
            added = true ;       
        }

        return added ;
    }
    bool SipProxyController::isResponseToChallenge( sip_t* sip, string& target ) {
        const char* nonce = NULL ; 
        const char* realm = NULL;
        bool isResponse = false ;

        sip_authorization_t* auth = sip->sip_authorization ;
        if( auth ) {
            realm = msg_header_find_param(auth->au_common, "realm") ;
            nonce = msg_header_find_param(auth->au_common, "nonce") ;
        }
        else {
            sip_proxy_authorization_t* auth2 = sip->sip_proxy_authorization ;
            if( auth2 ) {
                realm = msg_header_find_param(auth2->au_common, "realm") ;
                nonce = msg_header_find_param(auth2->au_common, "nonce") ;
            }            
        }
        if( nonce && realm ) {
            mapNonce2Challenge::iterator it = m_mapNonce2Challenge.find( nonce ) ;
            if( m_mapNonce2Challenge.end() != it ) {
                std::shared_ptr<ChallengedRequest> pChallenge = it->second ;
                if( 0 == pChallenge->getRealm().compare( realm ) ) {
                    isResponse = true ;
                    target = pChallenge->getRemoteAddress() ;
                    m_mapNonce2Challenge.erase( it ) ;
                    m_timerQueue.remove( pChallenge->getTimerHandle() ) ;
                }
            }
        }
        return isResponse ;
    }
    void SipProxyController::timeoutChallenge(const char* nonce) {
        mapNonce2Challenge::iterator it = m_mapNonce2Challenge.find( nonce ) ;
        if( m_mapNonce2Challenge.end() != it ) {
            m_mapNonce2Challenge.erase( it ) ;
            DR_LOG(log_debug) << "SipProxyController::timeoutChallenge - after removing nonce: " << nonce
                << " there are " <<  m_mapNonce2Challenge.size() << " saved challenges "; 
        }
    }

    void SipProxyController::logStorageCount(bool bDetail)  {
        std::lock_guard<std::mutex> lock(m_mutex) ;
        
        DR_LOG(log_info) << "SipProxyController storage counts"  ;
        DR_LOG(log_info) << "----------------------------------"  ;
        DR_LOG(log_info) << "m_mapCallId2Proxy size:                                          " << m_mapCallId2Proxy.size()  ;
        if (bDetail) {
            for (const auto& kv : m_mapCallId2Proxy) {
                std::shared_ptr<ProxyCore> p = kv.second;
                DR_LOG(log_debug) << "    sip proxy txn id: " << std::hex << (kv.first).c_str() << ", call-id: " << p->getCallId();
            }
        }

        DR_LOG(log_info) << "m_mapNonce2Challenge size:                                       " << m_mapNonce2Challenge.size()  ;
        if (bDetail) {
            for (const auto& kv : m_mapNonce2Challenge) {
                std::shared_ptr<ChallengedRequest> p = kv.second;
                DR_LOG(log_debug) << "    nonce: " << std::hex << (kv.first).c_str() << ", remote address: " << p->getRemoteAddress().c_str();
            }
        }
        m_pTQM->logQueueSizes() ;

        STATS_GAUGE_SET(STATS_GAUGE_PROXY, m_mapCallId2Proxy.size())
    }


} ;
