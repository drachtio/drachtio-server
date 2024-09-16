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
#include <algorithm>
#include <regex>
#include <cstdlib> // For std::getenv

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <sofia-sip/su_alloc.h>

namespace drachtio {
    class SipDialogController ;
}

#define NTA_RELIABLE_MAGIC_T drachtio::SipDialogController

#include "controller.hpp"
#include "cdr.hpp"
#include "sip-dialog-controller.hpp"
#include "sip-transports.hpp"

namespace {

    const char* envSupportBestEffortTls = std::getenv("DRACHTIO_SUPPORT_BEST_EFFORT_TLS");

    std::string combineCallIdAndCSeq(nta_outgoing_t* orq) {
        string callIdAndCSeq = nta_outgoing_call_id(orq);
        callIdAndCSeq.append(" ");
        callIdAndCSeq.append(boost::lexical_cast<std::string>(nta_outgoing_cseq(orq)));
        return callIdAndCSeq;
    }

    void cloneRespondToSipRequest(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        pController->getDialogController()->doRespondToSipRequest( d ) ;
    }
    void cloneSendSipRequest(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        pController->getDialogController()->doSendRequestOutsideDialog( d ) ;
    }
    void cloneSendSipCancelRequest(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", "CANCEL"}})
        pController->getDialogController()->doSendCancelRequest( d ) ;
    }
    int uacLegCallback( nta_leg_magic_t* p, nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        if( sip && sip->sip_request ) STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", sip->sip_request->rq_method_name}})
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processRequestInsideDialog( leg, irq, sip) ;
    }
    int uasCancelOrAck( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) {
        if( sip && sip->sip_request ) STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", sip->sip_request->rq_method_name}})
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processCancelOrAck( p, irq, sip) ;
    }
    int uasPrack( drachtio::SipDialogController *pController, nta_reliable_t *rel, nta_incoming_t *prack, sip_t const *sip) {
        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", "PRACK"}})
        return pController->processPrack( rel, prack, sip) ;
    }
   int response_to_request_outside_dialog( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {  
        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_IN, {
            {"method", sip->sip_cseq->cs_method_name},
            {"code", boost::lexical_cast<std::string>(sip->sip_status->st_status)}
        }) 
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processResponseOutsideDialog( request, sip ) ;
    } 
   int response_to_request_inside_dialog( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {   
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_IN, {
            {"method", sip->sip_cseq->cs_method_name},
            {"code", boost::lexical_cast<std::string>(sip->sip_status->st_status)}
        }) 
        return pController->getDialogController()->processResponseInsideDialog( request, sip ) ;
    } 
    void cloneSendSipRequestInsideDialog(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogController::SipMessageData* d = reinterpret_cast<drachtio::SipDialogController::SipMessageData*>( arg ) ;
        pController->getDialogController()->doSendRequestInsideDialog( d ) ;
    }
    int response_to_refreshing_reinvite( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {   
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processResponseToRefreshingReinvite( request, sip ) ;
    }
}


namespace drachtio {
    RIP::RIP( const string& transactionId ) : m_transactionId(transactionId) {
            DR_LOG(log_debug) << "RIP::RIP txnId: " << transactionId  ;

        }
    RIP::RIP( const string& transactionId, const string& dialogId ) : m_transactionId(transactionId), m_dialogId(dialogId) {
            DR_LOG(log_debug) << "RIP::RIP txnId: " << transactionId << " dialogId " << dialogId  ;

        }
    RIP::RIP( const string& transactionId, const string& dialogId,  std::shared_ptr<SipDialog> dlg, bool clearDialogOnResponse) :
        m_transactionId(transactionId), m_dialogId(dialogId), m_dlg(dlg), m_bClearDialogOnResponse(clearDialogOnResponse) {
            DR_LOG(log_debug) << "RIP::RIP txnId: " << transactionId << " dialogId " << dialogId << " clearDialogOnResponse " << clearDialogOnResponse ;
        }

    RIP::~RIP() {
        DR_LOG(log_debug) << "RIP::~RIP dialog id: " << m_dialogId  ;
    }

	SipDialogController::SipDialogController( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone), 
        m_agent(pController->getAgent()), m_pClientController(pController->getClientController())  {

            assert(m_agent) ;
            assert(m_pClientController) ;
            m_pTQM = std::make_shared<SipTimerQueueManager>( pController->getRoot() ) ;
            m_timerDHandler.setTimerQueueManager(m_pTQM);
	}
	SipDialogController::~SipDialogController() {
	}
    bool SipDialogController::sendRequestInsideDialog( const string& clientMsgId, const string& dialogId, const string& startLine, const string& headers, const string& body, string& transactionId ) {

        assert( dialogId.length() > 0 ) ;

        if( 0 == transactionId.length() ) { generateUuid( transactionId ) ; }

        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipRequestInsideDialog, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error allocating message") ;
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", dialogId, startLine, headers, body ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "Internal server error sending message") ;
            return  false;
        }
        
        return true ;
    }
    ///client-initiated outgoing messages (stack thread)
    void SipDialogController::doSendRequestInsideDialog( SipMessageData* pData ) {                
        nta_leg_t* leg = NULL ;
        nta_outgoing_t* orq = NULL ;
        string myHostport ;
        string requestUri ;
        string name ;
        string routeUri;
        bool destroyOrq = false;
        tagi_t* tags = nullptr;

        DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog dialog id: " << pData->getDialogId()  ;

        sip_method_t method = parseStartLine( pData->getStartLine(), name, requestUri ) ;

        std::shared_ptr<SipDialog> dlg ;
 
        assert( pData->getDialogId() ) ;

        try {
            if (!SD_FindByDialogId(m_dialogs, pData->getDialogId(), dlg ) ) {
                if( sip_method_ack == method ) {
                    DR_LOG(log_debug) << "Can't send ACK for dialog id " << pData->getDialogId() 
                        << "; likely because stack already ACK'ed non-success final response" ;
                    throw std::runtime_error("ACK for non-success final response is automatically generated by server") ;
                }
                DR_LOG(log_debug) << "Can't find dialog for dialog id " << pData->getDialogId() ;
                //assert(false) ;
                throw std::runtime_error("unable to find dialog for dialog id provided") ;
            }

            string transport ;
            dlg->getTransportDesc(transport) ;
            tags = makeTags( pData->getHeaders(), transport) ;

            tport_t* tp = dlg->getTport() ; //DH: this does NOT take out a reference
            bool forceTport = NULL != tp ;  

            nta_leg_t *leg = const_cast<nta_leg_t *>(dlg->getNtaLeg());
            if( !leg ) {
                assert( leg ) ;
                throw std::runtime_error("unable to find active leg for dialog") ;
            }
            
            /* race condition: we are sending a BYE during a re-invite transaction.  Generate a cancel first */
            if (sip_method_bye == method) {
                std::shared_ptr<IIP> iip;
                nta_leg_t * leg = const_cast<nta_leg_t *>(dlg->getNtaLeg());
                //DR_LOG(log_info) << "SipDialogController::doSendRequestInsideDialog - sending BYE, leg is " << std::hex << (void *) leg;
                if (IIP_FindByLeg(m_invitesInProgress, leg, iip)) {
                    const nta_outgoing_t* orq = iip->orq();
                    if (orq) {
                        DR_LOG(log_info) << "SipDialogController::doSendRequestInsideDialog - sending BYE during re-invite on leg "
                        << std::hex << (void *) leg << ", so canceling orq " << (void *) orq;
                        nta_outgoing_cancel((nta_outgoing_t*) orq);
                        IIP_Clear(m_invitesInProgress, leg);
                        const string id = dlg->getDialogId();
                        clearRIPByDialogId(id);
                    }
                }
            }


            const sip_contact_t *target ;
            if( (sip_method_ack == method || string::npos != requestUri.find("placeholder")) && nta_leg_get_route( leg, NULL, &target ) >=0 ) {
                char buffer[256];

                if (nullptr ==target) {
                    throw std::runtime_error("unable to find route for dialog when sending ACK") ;
                }
                url_e( buffer, 255, target->m_url ) ;
                requestUri = buffer ;
                DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - defaulting request uri to " << requestUri  ;

                // we need to check if there was a mid-call network handoff, where this client jumped networks
                std::shared_ptr<UaInvalidData> pData = m_pController->findTportForSubscription( target->m_url->url_user, target->m_url->url_host ) ;
                if( NULL != pData ) {
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog found cached tport for this client " << std::hex << (void *) pData->getTport();
                    //DH: I am now holding a tport that I did not take out a reference for
                    //what if while I am holding it the registration expires and the tport is destroyed?
                    if (pData->getTport() != tp) {
                        DR_LOG(log_info) << "SipDialogController::doSendRequestInsideDialog client has done a mid-call handoff; tp is now " << std::hex << (void *) pData->getTport();
                        tp = pData->getTport();
                        forceTport = true ;
                    }
               }
            }

            if( method == sip_method_invalid || method == sip_method_unknown ) {
                throw std::runtime_error(string("invalid or missing method supplied on start line: ") + pData->getStartLine() ) ;
            }

            //set content-type if not supplied and body contains SDP
            string body = pData->getBody() ;
            string contentType ;
            if( body.length() && !searchForHeader( tags, siptag_content_type_str, contentType ) ) {
                if( 0 == body.find("v=0") ) {
                    contentType = "application/sdp" ;
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - automatically detecting content-type as application/sdp"  ;
                }
                else {
                    throw std::runtime_error("missing content-type") ;                   
                }
            }
            if( sip_method_invite == method && body.length() && 0 == contentType.compare("application/sdp")) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - updating local sdp to " << body ;
                dlg->setLocalSdp( body.c_str() ) ;
                dlg->setLocalContentType(contentType);
            }

            if (dlg->getRouteUri(routeUri)) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - sending request to nat'ed address using route " << routeUri ;
            }

            if( sip_method_ack == method ) {
                if( 200 == dlg->getSipStatus() ) {
                    char cseq[32];
                    memset(cseq, 0, 32);
                    uint32_t seq = dlg->getSeq();
                    dlg->clearSeq();
                    if (seq > 0) {
                        snprintf(cseq, 31, "%u ACK", seq);
                        DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - setting CSeq to  " << seq ;
                    }
                    orq = nta_outgoing_tcreate(leg, NULL, NULL, 
                        routeUri.empty() ? NULL : URL_STRING_MAKE(routeUri.c_str()),                     
                        method, name.c_str(),
                        URL_STRING_MAKE(requestUri.c_str()),
                        TAG_IF( *cseq, SIPTAG_CSEQ_STR(cseq)),
                        TAG_IF( body.length(), SIPTAG_PAYLOAD_STR(body.c_str())),
                        TAG_IF( contentType.length(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str())),
                        TAG_IF(forceTport, NTATAG_TPORT(tp)),
                        TAG_NEXT(tags) ) ;
                    
                    tport_t* orq_tp = nta_outgoing_transport( orq );  // takes a reference on the tport
                    if (tport_is_dgram(orq_tp)) m_timerDHandler.addAck(orq);
                    else if (orq_tp) destroyOrq = true;
                    if (orq_tp) {
                        dlg->setTport(orq_tp) ;
                        tport_unref(orq_tp) ; // handed reference to dlg
                    }
                    else {
                        DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - sending ACK but nta_outgoing_transport is null, delayed for DNS resolver";
                        dlg->setOrqAck(orq, !tport_is_dgram(orq_tp));
                        destroyOrq = false;
                        tport_unref(orq_tp);  // release the reference
                    }
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - clearing IIP that we generated as uac" ;
                    IIP_Clear(m_invitesInProgress, leg);  

                    DR_LOG(log_info) << "SipDialogController::doSendRequestInsideDialog (ack) - created orq " << std::hex << (void *) orq;

                }
            }
            else if( sip_method_prack == method ) {
                std::shared_ptr<IIP> iip;
                if(!IIP_FindByLeg(m_invitesInProgress, leg, iip)) {
                    throw std::runtime_error("unable to find IIP when sending PRACK") ;
                }
                orq = nta_outgoing_prack(leg, const_cast<nta_outgoing_t *>(iip->orq()), response_to_request_inside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    //NULL, 
                    routeUri.empty() ? NULL : URL_STRING_MAKE(routeUri.c_str()),
                    NULL, TAG_NEXT(tags) ) ;
                DR_LOG(log_info) << "SipDialogController::doSendRequestInsideDialog (prack) - created orq " << std::hex << (void *) orq;

            }
            else {
                string contact ;
                bool addContact = false ;
                if( (method == sip_method_invite || method == sip_method_subscribe || method == sip_method_refer) && !searchForHeader( tags, siptag_contact_str, contact ) ) {
                    contact = dlg->getLocalContactHeader();
                    addContact = true ;
                }
                orq = nta_outgoing_tcreate( leg, response_to_request_inside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    routeUri.empty() ? NULL : URL_STRING_MAKE(routeUri.c_str()),                     
                    method, name.c_str()
                    ,URL_STRING_MAKE(requestUri.c_str())
                    ,TAG_IF( addContact, SIPTAG_CONTACT_STR( contact.c_str() ) )
                    ,TAG_IF( body.length(), SIPTAG_PAYLOAD_STR(body.c_str()))
                    ,TAG_IF( contentType.length(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                    ,TAG_IF(forceTport, NTATAG_TPORT(tp))
                    ,TAG_NEXT(tags) ) ;

                if( orq ) {
                    DR_LOG(log_info) << "SipDialogController::doSendRequestInsideDialog - created orq " << std::hex << (void *) orq << " sending " << nta_outgoing_method_name(orq) << " to " << requestUri ;
                }
            }

            if( NULL == orq && sip_method_ack != method ) {
                throw std::runtime_error("Error creating sip transaction for request") ;               
            }

            if( sip_method_ack == method && 200 != dlg->getSipStatus() ) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog - clearing uac dialog that had final response " <<  dlg->getSipStatus() ;
                SD_Clear(m_dialogs, dlg->getDialogId()) ;
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                    "ACK for non-success responses is automatically generated by the stack" ) ;
            }
            else {
                msg_t* m = nta_outgoing_getrequest(orq) ;  // adds a reference
                sip_t* sip = sip_object( m ) ;

                STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", sip->sip_request->rq_method_name}})

                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta(m, orq) ;
                string s ;
                meta.toMessageFormat(s) ;
                string data = s + "|" + pData->getTransactionId() + "|Msg sent:|" + DR_CRLF + encodedMessage ;

                if( sip_method_ack == method ) {
                    if( dlg->getSipStatus() > 200 ) {
                        m_pClientController->removeDialog( dlg->getDialogId() ) ;
                    }
                }
                else {
                    bool clearDialogOnResponse = false ;
                    if( sip_method_bye == method || 
                        ( !dlg->isInviteDialog() && sip_method_notify == method && NULL != sip->sip_subscription_state && NULL != sip->sip_subscription_state->ss_substate &&
                            NULL != strstr(sip->sip_subscription_state->ss_substate, "terminated") ) ) {
                        clearDialogOnResponse = true ;
                    }

                    std::shared_ptr<RIP> p = std::make_shared<RIP>( pData->getTransactionId(), pData->getDialogId(), dlg, clearDialogOnResponse ) ;
                    addRIP( orq, p ) ;       
                }
                if( sip_method_invite == method ) {
                    addOutgoingInviteTransaction( leg, orq, sip, dlg ) ;
                }

                if (sip_method_bye == method) {
                    Cdr::postCdr( std::make_shared<CdrStop>( m, "application", Cdr::normal_release ) );
                }
     
                msg_destroy(m) ; //releases reference
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", data ) ; 
            }

            if (sip_method_ack == method && dlg->getSipStatus() == 200 && dlg->isAckBye()) {
                this->notifyTerminateStaleDialog(dlg, true);
            }
        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << "SipDialogController::doSendRequestInsideDialog - Error: " << err.what() ;
            string msg = string("Server error: ") + err.what() ;
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", msg ) ;
            m_pController->getClientController()->removeAppTransaction( pData->getTransactionId() ) ;
        }                       

        /* we must explicitly delete an object allocated with placement new */
        pData->~SipMessageData() ;
        if (orq && destroyOrq) nta_outgoing_destroy(orq);
        deleteTags( tags ) ;
    }

//send request outside dialog
    //client thread
    bool SipDialogController::sendRequestOutsideDialog( const string& clientMsgId, const string& startLine, const string& headers, const string& body, string& transactionId, string& dialogId, string& routeUrl ) {
        if( 0 == transactionId.length() ) { generateUuid( transactionId ) ; }
        if( string::npos != startLine.find("INVITE") ) {
            generateUuid( dialogId ) ;
        }

        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipRequest, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", dialogId, startLine, headers, body, routeUrl ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }
        return true ;
    }
    //stack thread
     void SipDialogController::doSendRequestOutsideDialog( SipMessageData* pData ) {
        nta_leg_t* leg = NULL ;
        nta_outgoing_t* orq = NULL ;
        string requestUri ;
        string name ;
        string sipOutboundProxy ;
        tport_t* tp = NULL ;
        std::shared_ptr<SipTransport> pSelectedTransport ;
        bool forceTport = false ;
        string host, port, proto, contact, desc ;
        tagi_t* tags = nullptr;

        try {
            bool useOutboundProxy = false ;
            const char *szRouteUrl = pData->getRouteUrl() ;
            if (*szRouteUrl != '\0') {
                useOutboundProxy = true ;
                sipOutboundProxy.assign(szRouteUrl);
            }
            else {
                useOutboundProxy = m_pController->getConfig()->getSipOutboundProxy( sipOutboundProxy ) ;
            }
            if (useOutboundProxy) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog sending request to route url: " << sipOutboundProxy ;
            }

            sip_request_t *sip_request = sip_request_make(m_pController->getHome(), pData->getStartLine() ) ;
            if( NULL == sip_request || 
                url_invalid == sip_request->rq_url[0].url_type || 
                url_unknown == sip_request->rq_url[0].url_type  ||
                sip_method_invalid == sip_request->rq_method ||
                sip_method_unknown == sip_request->rq_method  ) {

                throw std::runtime_error(string("invalid request-uri: ") + pData->getStartLine() ) ;
            }
            sip_method_t method = parseStartLine( pData->getStartLine(), name, requestUri ) ;

            int rc = 0 ;
            if( (sip_method_invite == sip_request->rq_method || 
                sip_method_options == sip_request->rq_method ||
                sip_method_notify == sip_request->rq_method ||
                sip_method_message == sip_request->rq_method) && 
                !tport_is_dgram(tp) /*&& NULL != strstr( sip_request->rq_url->url_host, ".invalid")*/ ) {

                std::shared_ptr<UaInvalidData> pData = 
                    m_pController->findTportForSubscription( sip_request->rq_url->url_user, sip_request->rq_url->url_host ) ;

                if( NULL != pData ) {
                    forceTport = true ;
                    tp = pData->getTport() ;
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog selecting existing secondary transport " << std::hex << (void *) tp ;

                    getTransportDescription( tp, desc ) ;
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - selected transport " << std::hex << (void*)tp << ": " << desc << " for request-uri " << requestUri  ;            

                    const tp_name_t* tpn = tport_name( tport_parent( tp ) );
                    string host = tpn->tpn_host ;
                    string port = tpn->tpn_port ;
                    string proto = tpn->tpn_proto ;

                    contact = "<sip:" + host + ":" + port + ";transport=" + proto + ">";
               }
            }
            if( NULL == tp ) {
                pSelectedTransport = SipTransport::findAppropriateTransport( useOutboundProxy ? sipOutboundProxy.c_str() : requestUri.c_str()) ;
                if (!pSelectedTransport) {
                    throw std::runtime_error(string("requested protocol/transport not available"));
                }

                pSelectedTransport->getDescription(desc);
                pSelectedTransport->getContactUri( contact, true ) ;
                contact = "<" + contact + ">" ;
                host = pSelectedTransport->getHost() ;
                port = pSelectedTransport->getPort() ;

                tp = (tport_t *) pSelectedTransport->getTport() ;
                DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog selected transport " << std::hex << (void*)tp << desc ;
                forceTport = true ;
            }
            su_free( m_pController->getHome(), sip_request ) ;

            if (pSelectedTransport && pSelectedTransport->hasExternalIp()) {
                tags = makeTags( pData->getHeaders(), desc, pSelectedTransport->getExternalIp().c_str()) ;
            }
            else {
                tags = makeTags( pData->getHeaders(), desc, NULL) ;
            }
           
            //if user supplied all or part of the From use it
            string from, to, callid ;
            if( searchForHeader( tags, siptag_from_str, from ) ) {
                if( string::npos != from.find("localhost") ) {
                    if( !replaceHostInUri( from, host.c_str(), port.c_str() ) ) {
                        throw std::runtime_error(string("invalid from value provided by client: ") + from ) ;
                    }                    
                }
            } 
            else {
                from = contact ;
            }

            //default To header to request uri if not provided
            if( !searchForHeader( tags, siptag_to_str, to ) ) {
                to = requestUri ;
            } 

            DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - from: " << from   ;            
            DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - to: " << to ;            
            DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - contact: " << contact  ;            

            // use call-id if supplied
            if( searchForHeader( tags, siptag_call_id_str, callid ) ) {
                DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - using client-specified call-id: " << callid  ;            
            }

            //set content-type if not supplied and body contains SDP
            string body = pData->getBody() ;
            string contentType ;
            if( body.length() && !searchForHeader( tags, siptag_content_type_str, contentType ) ) {
                if( 0 == body.find("v=0") ) {
                    contentType = "application/sdp" ;
                    DR_LOG(log_debug) << "SipDialogController::doSendRequestOutsideDialog - automatically detecting content-type as application/sdp"  ;
                }
                else {
                    throw std::runtime_error("missing content-type") ;                   
                }
             }

            //prevent looping messages
            if (!normalizeSipUri( requestUri, 0 )) {
                throw std::runtime_error(string("invalid request-uri: ") + requestUri ) ;
            }
            if( isLocalSipUri( requestUri ) ) {
                throw std::runtime_error("can not send request to myself") ;
            }

            if( !(leg = nta_leg_tcreate( m_pController->getAgent(),
                uacLegCallback, (nta_leg_magic_t *) m_pController,
                SIPTAG_FROM_STR(from.c_str()),
                SIPTAG_TO_STR(to.c_str()),
                TAG_IF( callid.length(), SIPTAG_CALL_ID_STR(callid.c_str())),
                TAG_IF( method == sip_method_register, NTATAG_NO_DIALOG(1)),
                TAG_END() ) ) ) {

                throw std::runtime_error("Error creating leg") ;
            }
            nta_leg_tag( leg, NULL ) ;

            orq = nta_outgoing_tcreate( leg, 
                response_to_request_outside_dialog, 
                (nta_outgoing_magic_t*) m_pController, 
                useOutboundProxy ? URL_STRING_MAKE( sipOutboundProxy.c_str() ) : NULL, 
                method, 
                name.c_str()
                ,URL_STRING_MAKE(requestUri.c_str())
                ,TAG_IF( (method == sip_method_invite || method == sip_method_subscribe) && 
                    !searchForHeader( tags, siptag_contact_str, contact ), SIPTAG_CONTACT_STR( contact.c_str() ) )
                ,TAG_IF( body.length(), SIPTAG_PAYLOAD_STR(body.c_str()))
                ,TAG_IF( contentType.length(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                ,TAG_IF( forceTport, NTATAG_TPORT(tp))
                ,TAG_NEXT(tags) ) ;

            if( NULL == orq ) {
                throw std::runtime_error("Error creating sip transaction for uac request") ;               
            }

            msg_t* m = nta_outgoing_getrequest(orq) ; //adds a reference
            sip_t* sip = sip_object( m ) ;

            DR_LOG(log_info) << "SipDialogController::doSendRequestOutsideDialog - created orq " << std::hex << (void *) orq  <<
                " call-id " << sip->sip_call_id->i_id << " / transaction id: " << pData->getTransactionId();

            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", sip->sip_request->rq_method_name}})

            if( method == sip_method_invite || method == sip_method_subscribe ) {
                std::shared_ptr<SipDialog> dlg = std::make_shared<SipDialog>(pData->getTransactionId(), 
                    leg, orq, sip, m, desc) ;
                string customContact ;
                bool hasCustomContact = searchForHeader( tags, siptag_contact_str, customContact ) ;
                dlg->setLocalContactHeader(hasCustomContact ? customContact.c_str() : contact.c_str());

                addOutgoingInviteTransaction( leg, orq, sip, dlg ) ;
                if (method == sip_method_invite) {
                  Cdr::postCdr( std::make_shared<CdrAttempt>(m, "application"));
                }
            }
            else {
                std::shared_ptr<RIP> p = std::make_shared<RIP>( pData->getTransactionId(), pData->getDialogId() ) ;
                addRIP( orq, p ) ;
                nta_leg_destroy( leg ) ;
            }

            string encodedMessage ;
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta(m, orq) ;
            string s ;
            meta.toMessageFormat(s) ;
            msg_destroy(m) ;    // releases reference

            string data = s + "|" + pData->getTransactionId() + "|Msg sent:|" + DR_CRLF + encodedMessage ;

            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", data ) ;
 
        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << "SipDialogController::doSendRequestOutsideDialog - " << err.what() ;
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", err.what() ) ;  
            m_pController->getClientController()->removeAppTransaction( pData->getTransactionId() ) ;
        }                       

        //N.B.: we must explicitly call the destructor of an object allocated with placement new
        pData->~SipMessageData() ;

        deleteTags(tags);
    }

    bool SipDialogController::sendCancelRequest( const string& clientMsgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) {
        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipCancelRequest, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", "", startLine, headers, body ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }
        return true ;
    }
    bool SipDialogController::respondToSipRequest( const string& clientMsgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) {
       su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneRespondToSipRequest, sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false ;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        string rid ;
        SipMessageData* msgData = new(place) SipMessageData( clientMsgId, transactionId, "", "", startLine, headers, body ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false ;
        }

        return true ;
    }
   
    void SipDialogController::doSendCancelRequest( SipMessageData* pData ) {
        string transactionId( pData->getTransactionId() ) ;
        std::shared_ptr<IIP> iip ;
        tagi_t* tags = nullptr;

        if (IIP_FindByTransactionId(m_invitesInProgress, transactionId, iip)) {
            iip->setCanceled();
            tags = makeSafeTags( pData->getHeaders()) ;
            nta_outgoing_t *cancel = nta_outgoing_tcancel(const_cast<nta_outgoing_t *>(iip->orq()), NULL, NULL, TAG_NEXT(tags));
            if( NULL != cancel ) {
                msg_t* m = nta_outgoing_getrequest(cancel) ;    // adds a reference
                sip_t* sip = sip_object( m ) ;

                string cancelTransactionId ;
                generateUuid( cancelTransactionId ) ;

                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta(m, cancel) ;
                string s ;
                meta.toMessageFormat(s) ;
                string data = s + "|" + cancelTransactionId + "|Msg sent:|" + DR_CRLF + encodedMessage ;
                msg_destroy(m) ;    // releases reference

                //Note: not adding an RIP because the 200 OK to the CANCEL is not passed up to us

                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "OK", data ) ;     
                deleteTags(tags);
                return ;           
            }
            else {
                DR_LOG(log_error) << "SipDialogController::doSendCancelRequest - internal server error canceling transaction id " << 
                    transactionId << " / orq: " << std::hex << (void *) iip->orq();
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                    string("internal server error canceling transaction id: ") + transactionId ) ; 
            }
        }
        else {
            DR_LOG(log_error) << "SipDialogController::doSendCancelRequest - unknown transaction id " << transactionId ;
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                string("unable to cancel unknown transaction id: ") + transactionId ) ; 
        }
        pData->~SipMessageData() ;
        deleteTags(tags);
   }

    int SipDialogController::processResponseOutsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
        DR_LOG(log_debug) << "SipDialogController::processResponseOutsideDialog"  ;
        string transactionId ;
        std::shared_ptr<SipDialog> dlg ;

        string encodedMessage ;
        bool truncated ;
        msg_t* msg = nta_outgoing_getresponse(orq) ;    //adds a reference
        SipMsgData_t meta( msg, orq, "network") ;

        EncodeStackMessage( sip, encodedMessage ) ;

        if( sip->sip_cseq->cs_method == sip_method_invite || sip->sip_cseq->cs_method == sip_method_subscribe ) {
            std:shared_ptr<IIP> iip;

            //check for retransmission 
            if (sip->sip_cseq->cs_method == sip_method_invite && m_timerDHandler.resendIfNeeded(orq)) {
                DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - retransmitted ACK for callid: " << sip->sip_call_id->i_id  <<
                    " for invite with orq: " << std::hex << (void *) orq;
                msg_destroy( msg ) ; 
                return 0;
            }

            if (!IIP_FindByOrq(m_invitesInProgress, orq, iip)) {
                DR_LOG(log_error) << "SipDialogController::processResponseOutsideDialog - unable to match invite response with callid: " << sip->sip_call_id->i_id  ;
                //TODO: do I need to destroy this transaction?
                msg_destroy( msg ) ; 
                return -1 ; //TODO: check meaning of return value           
            }      
            transactionId = iip->getTransactionId() ;   
            dlg = iip->dlg() ;   


            //update dialog variables            
            dlg->setSipStatus( sip->sip_status->st_status ) ;
            if( sip->sip_payload ) {
                iip->dlg()->setRemoteSdp( sip->sip_payload->pl_data, sip->sip_payload->pl_len ) ;
            }

            // stats
            if (theOneAndOnlyController->getStatsCollector().enabled()) {
                // post-dial delay
                if (sip->sip_cseq->cs_method == sip_method_invite && dlg->getSipStatus() <= 200) {
                    auto now = std::chrono::steady_clock::now();
                    std::chrono::duration<double> diff = now - dlg->getArrivalTime();
                    if (!dlg->hasAlerted()) {
                        dlg->alerting();
                        STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_PDD_OUT, diff.count())
                    }
                    if (200 == dlg->getSipStatus()) {
                        STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_RESPONSE_TIME_OUT, diff.count())
                    }
                }
            }


            //UAC Dialog is added when we receive a final response from the network, or a reliable provisional response
            //for non-success responses, it will subsequently be removed when we receive the ACK from the client

            if( (sip->sip_cseq->cs_method == sip_method_invite && 
                    (200 == sip->sip_status->st_status || (sip->sip_status->st_status > 100 && sip->sip_status->st_status < 200 && sip->sip_rseq))) || 
                (sip->sip_cseq->cs_method == sip_method_subscribe && 
                    (202 == sip->sip_status->st_status || 200 == sip->sip_status->st_status) ) )  {

                DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - adding dialog id: " << dlg->getDialogId()  ;
                nta_leg_t* leg = const_cast<nta_leg_t *>(iip->leg()) ;
                nta_leg_rtag( leg, sip->sip_to->a_tag) ;
                nta_leg_client_reroute( leg, sip->sip_record_route, sip->sip_contact, 1 );

                bool nat = false;
                const sip_route_t* route = NULL;
                if (nta_leg_get_route(leg, &route, NULL) >= 0 && route && route->r_url->url_host && isRfc1918(route->r_url->url_host)) {
                    DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - (UAC) detected possible natted downstream client at RFC1918 address: " << route->r_url->url_host  ;
                    nat = true;
                }
                else if(sip->sip_cseq->cs_method == sip_method_invite && !route && sip->sip_contact && 
                    0 != strcmp(sip->sip_contact->m_url->url_host, meta.getAddress().c_str())) {

                    DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - (UAC) detected downstream client; contact ip: " << 
                        sip->sip_contact->m_url->url_host << " differs from recv address: " << meta.getAddress().c_str();
                    nat = true;
                }
                else if (theOneAndOnlyController->isAggressiveNatEnabled() && sipMsgHasNatEqualsYes(sip, true, true)) {
                        DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - (UAC) detected possible natted downstream client advertising nat=yes";
                    nat = true;
                }

                if (nat && theOneAndOnlyController->isNatDetectionDisabled()) {
                    nat = false;
                    DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - (UAC) detected possible natted downstream client, but ignoring because disable-nat-detection is on";
                }
                if (nat) {
                    url_t const * uri = nta_outgoing_request_uri(orq);
                    if (uri && 0 == strcmp(uri->url_host, "feature-server")) {
                      DR_LOG(log_debug) << "SipDialogController::processResponseOutsideDialog - (UAC) detected jambonz k8s feature-server destination, no nat";
                      nat = false;
                    }
                    else {
                      url_t const * url = nta_outgoing_route_uri(orq);
                      string routeUri = string((url ? url->url_scheme : "sip")) + ":" + meta.getAddress() + ":" + meta.getPort();
                      dlg->setRouteUri(routeUri);
                      DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - (UAC) detected nat setting route to: " <<   routeUri;
                    }
                }
                else {
                    dlg->clearRouteUri();
                }
                if (iip->isCanceled()) {
                    DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - ACK/BYE race condition - received 200 OK to INVITE that was previously CANCELED";
                    dlg->doAckBye();
                }
                addDialog( dlg ) ;
            }
            tport_t* tp = nta_outgoing_transport(orq);
            if (sip->sip_cseq->cs_method == sip_method_invite && sip->sip_status->st_status == 200 && tport_is_dgram(tp)) {
                // for successful uac invites, we need to handle retransmits
                m_timerDHandler.addInvite(orq);
            }
            tport_unref(tp);

            if (sip->sip_cseq->cs_method == sip_method_invite && sip->sip_status->st_status == 200 && sip->sip_session_expires) {
                DR_LOG(log_info) << "SipDialogController::processResponseOutsideDialog - (UAC) detected session timer header: ";
                dlg->setSessionTimer( sip->sip_session_expires->x_delta, 
                    !sip->sip_session_expires->x_refresher || 0 == strcmp( sip->sip_session_expires->x_refresher, "uac") ? 
                    SipDialog::we_are_refresher : 
                    SipDialog::they_are_refresher) ;
            }
            else if (sip->sip_status->st_status > 200) {
                IIP_Clear(m_invitesInProgress, iip);
            }
        }
        else {
            std::shared_ptr<RIP> rip ;
            if( !findRIPByOrq( orq, rip ) ) {
                DR_LOG(log_error) << "SipDialogController::processResponseOutsideDialog - unable to match response with callid for a non-invite request we sent: " << sip->sip_call_id->i_id  ;
                //TODO: do I need to destroy this transaction?
                return -1 ; //TODO: check meaning of return value                           
            }
            transactionId = rip->getTransactionId() ;
            if( sip->sip_status->st_status >= 200 ) this->clearRIP( orq ) ;
        }
        
        if( !dlg ) {
            m_pController->getClientController()->route_response_inside_transaction( encodedMessage, meta, orq, sip, transactionId ) ;
        }
        else {
            m_pController->getClientController()->route_response_inside_transaction( encodedMessage, meta, orq, sip, transactionId, dlg->getDialogId() ) ;            
        }

        if( sip->sip_cseq->cs_method == sip_method_invite) {
          if (sip->sip_status->st_status >= 300) {
            Cdr::postCdr( std::make_shared<CdrStop>( msg, "network",
                487 == sip->sip_status->st_status ? Cdr::call_canceled : Cdr::call_rejected ) );
          }
          else if (sip->sip_status->st_status == 200) {
            Cdr::postCdr( std::make_shared<CdrStart>( msg, "network", Cdr::uac ) );                
          }
        }
        if( sip->sip_cseq->cs_method == sip_method_invite && sip->sip_status->st_status > 200 ) {
            assert( dlg ) ;
            m_pController->getClientController()->removeDialog( dlg->getDialogId() ) ;
        }

        msg_destroy( msg ) ;                            //releases reference

        return 0 ;
    }
    void SipDialogController::doRespondToSipRequest( SipMessageData* pData ) {
        string transactionId( pData->getTransactionId() );
        string startLine( pData->getStartLine()) ;
        string headers( pData->getHeaders() );
        string body( pData->getBody()) ;
        string clientMsgId( pData->getClientMsgId()) ;
        string contentType ;
        string dialogId ;
        string contact, transportDesc ;
        std::shared_ptr<SipTransport> pSelectedTransport ;
        bool bSentOK = true ;
        string failMsg ;
        bool bDestroyIrq = false ;
        bool bClearIIP = false ;
        bool existingDialog = false;
        bool transportGone = false;
        tagi_t* tags = nullptr;

        //decode status 
        sip_status_t* sip_status = sip_status_make( m_pController->getHome(), startLine.c_str() ) ;
        int code = sip_status->st_status ;
        const char* status = sip_status->st_phrase ;
  
        nta_incoming_t* irq = NULL ;
        int rc = -1 ;
        std::shared_ptr<IIP> iip;

        DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest thread " << std::this_thread::get_id() ;

        /* search for requests within a dialog first */
        irq = findAndRemoveTransactionIdForIncomingRequest( transactionId ) ;
        if( !irq ) {
            if (!IIP_FindByTransactionId(m_invitesInProgress, transactionId, iip)) {
                /* could be a new incoming request that hasn't been responded to yet */
                
                /* we allow the app to set the local tag (ie tag on the To) */
                string toValue;
                string tag;
                if (GetValueForHeader( headers, "to", toValue)) {
                    std::regex re("tag=(.*)");
                    std::smatch mr;
                    if (std::regex_search(toValue, mr, re) && mr.size() > 1) {
                        tag = mr[1] ;
                    }
                }

                if( m_pController->setupLegForIncomingRequest( transactionId, tag ) ) {
                    if (!IIP_FindByTransactionId(m_invitesInProgress, transactionId, iip)) {
                        irq = findAndRemoveTransactionIdForIncomingRequest(transactionId)  ;
                    }
                }
             }
        }
        else {
            existingDialog = true;
        }

        if( irq ) {

            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest found incoming transaction " << std::hex << irq  ;

            msg_t* msg = nta_incoming_getrequest( irq ) ;   //adds a reference
            sip_t *sip = sip_object( msg );

            tport_t *tp = nta_incoming_transport(m_agent, irq, msg) ; 

            if (!tp || tport_is_shutdown(tp)) {
                failMsg = "transport for response has been shutdown or closed";
                DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - unable to forward response as transport has been closed or shutdown "
                    << sip->sip_call_id->i_id << " " << sip->sip_cseq->cs_seq;
                bSentOK = false;
                transportGone = true;
                msg_destroy(msg);
            }
            else {
                tport_t *tport = tport_parent( tp ) ;

                pSelectedTransport = SipTransport::findTransport( tport ) ;
                if (!pSelectedTransport) {
                    bSentOK = false;
                    failMsg = "Unable to find transport for transaction";
                    DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - unable to find transport for response to "
                        << sip->sip_call_id->i_id << " " << sip->sip_cseq->cs_seq;
                }
                else {
                    pSelectedTransport->getContactUri(contact, true);
                    contact = "<" + contact + ">" ;

                    pSelectedTransport->getDescription(transportDesc);

                    tport_unref( tp ) ;
            
                    //create tags for headers
                    tags = makeTags( headers, transportDesc ) ;

                    if( body.length() && !searchForHeader( tags, siptag_content_type, contentType ) ) {
                        if( 0 == body.find("v=0") ) {
                            contentType = "application/sdp" ;
                            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - automatically detecting content-type as application/sdp"  ;
                        }
                    }

                    rc = nta_incoming_treply( irq, code, status
                        ,TAG_IF( (sip_method_invite == sip->sip_request->rq_method || sip->sip_request->rq_method == sip_method_subscribe) &&
                            !searchForHeader( tags, siptag_contact_str, contact ), SIPTAG_CONTACT_STR(contact.c_str()) )
                        ,TAG_IF(!body.empty(), SIPTAG_PAYLOAD_STR(body.c_str()))
                        ,TAG_IF(!contentType.empty(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                        ,TAG_NEXT(tags)
                        ,TAG_END() ) ;
                    if( 0 != rc ) {
                        bSentOK = false ;
                        failMsg = "Unknown server error sending response" ;
                    }
                }
            }

            //  we need to cache source address / port / transport for successful REGISTER or SUBSCRIBE requests from webrtc clients so we can 
            //  later send INVITEs and NOTIFYs
            if( bSentOK && ((sip->sip_request->rq_method == sip_method_subscribe && (202 == code || 200 ==code) ) ||
                (sip->sip_request->rq_method == sip_method_register && 200 == code) ) ) {

                sip_contact_t* contact = sip->sip_contact ;
                if( contact ) {
                    if( !tport_is_dgram(tp) /*&& NULL != strstr( contact->m_url->url_host, ".invalid") */) {
                        bool add = true ;
                        unsigned long expires = 0 ;

                        msg_t *msgResponse = nta_incoming_getresponse( irq ) ;    // adds a reference
                        if (msg) {
                            sip_t *sipResponse = sip_object( msgResponse ) ;
                            if (sipResponse) {
                                if( sip->sip_request->rq_method == sip_method_subscribe ) {
                                    if (sipResponse->sip_expires && sipResponse->sip_expires->ex_delta) {
                                        expires = sipResponse->sip_expires->ex_delta;
                                    }
                                    else {
                                        DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - 200-class response to SUBSCRIBE must include expires (rfc3265 3.1.1)"  ;
                                    }
                                }
                                else {
                                    if( NULL != sipResponse->sip_contact && NULL != sipResponse->sip_contact->m_expires ) {
                                        expires = ::atoi( sipResponse->sip_contact->m_expires ) ;
                                    }
                                    else if (NULL != sipResponse->sip_expires && sipResponse->sip_expires->ex_delta) {
                                        expires = sipResponse->sip_expires->ex_delta;
                                    }  
                                }
                                
                                add = expires > 0 ;
                                if( add ) {
                                    theOneAndOnlyController->cacheTportForSubscription( contact->m_url->url_user, contact->m_url->url_host, expires, tp ) ;
                                }
                                else {
                                    theOneAndOnlyController->flushTportForSubscription( contact->m_url->url_user, contact->m_url->url_host ) ;                        
                                }
                            }
                            else {
                                bSentOK = false ;
                                failMsg = "connection error: remote side may have closed socket";                                
                            }
                            msg_destroy( msgResponse ) ;    // releases the reference                            
                        }
                        else {
                            bSentOK = false ;
                            failMsg = "connection error: remote side may have closed socket";
                        }
                    }
                }
            }

            if (existingDialog) {
                nta_leg_t* leg = nta_leg_by_call_id(m_pController->getAgent(), sip->sip_call_id->i_id);
                if (leg) {
                    std::shared_ptr<SipDialog> dlg ;
                    if(findDialogByLeg( leg, dlg )) {
                        dialogId = dlg->getDialogId();
                        dlg->removeIncomingRequestTransaction(transactionId);
                        DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest retrieved dialog id for existing dialog " << dialogId  ;
                        if (sip->sip_request->rq_method == sip_method_invite && body.length() && bSentOK) {
                            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest updating local sdp for dialog " << dialogId  ;
                            dlg->setLocalSdp( body.c_str() ) ;
                        }
                    }
                }
            }
            msg_destroy( msg ); //release the reference

            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest destroying irq " << irq  ;
            bDestroyIrq = true ;                        
        }
        else if( iip ) {
            DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest found invite or subscribe in progress " << std::hex << iip  ;
           /* invite in progress */
            nta_leg_t* leg = const_cast<nta_leg_t *>(iip->leg()) ;
            irq = const_cast<nta_incoming_t *>(iip->irq()) ;         
            std::shared_ptr<SipDialog> dlg = iip->dlg() ;

            if (dlg->getSipStatus() >= 200) {
                DR_LOG(log_warning) << "SipDialogController::doRespondToSipRequest: iip " << std::hex << iip  << 
                    ": application attempting to send final response " << std::dec << dlg->getSipStatus() << 
                    " when a final response has already been sent; discarding" ;
            }
            else {
                msg_t* msg = nta_incoming_getrequest( irq ) ;   //allocates a reference
                sip_t *sip = sip_object( msg );

                tport_t *tp = nta_incoming_transport(m_agent, irq, msg) ; 
                if (!tp || tport_is_shutdown(tp)) {
                    failMsg = "transport for response has been shutdown or closed";
                    DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - unable to forward response as transport has been closed or shutdown "
                        << sip->sip_call_id->i_id << " " << sip->sip_cseq->cs_seq;
                    bSentOK = false;
                    transportGone = true;
                    msg_destroy(msg);
                }
                else {
                    tport_t *tport = tport_parent( tp ) ;

                    pSelectedTransport = SipTransport::findTransport( tport ) ;
                    assert(pSelectedTransport); 

                    pSelectedTransport->getContactUri(contact, true);

                    /* is far end requesting "best effort" tls ?*/
                    if (envSupportBestEffortTls && atoi(envSupportBestEffortTls) == 1 &&
                      pSelectedTransport->isSips() && sip->sip_contact && sip->sip_contact->m_url &&
                      0 == strcmp(sip->sip_contact->m_url->url_scheme, "sip")) {
                        contact.replace(0, 5, "sip:");
                        DR_LOG(log_info) << "SipDialogController::doRespondToSipRequest - far end wants best effort tls, replacing sips with sip in Contact";
                    }

                    contact = "<" + contact + ">" ;

                    pSelectedTransport->getDescription(transportDesc);

                    tport_unref( tp ) ;
            
                    //create tags for headers
                    tags = makeTags( headers, transportDesc ) ;
                    string customContact ;
                    bool hasCustomContact = searchForHeader( tags, siptag_contact_str, customContact ) ;
                    if( hasCustomContact ) {
                        DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - client provided contact header so we wont include our internally-generated one"  ;
                    }

                    dlg->setLocalContactHeader(hasCustomContact ? customContact.c_str() : contact.c_str());

                    dialogId = dlg->getDialogId() ;

                    dlg->setSipStatus( code ) ;

                    /* if the client included Require: 100rel on a provisional, send it reliably */
                    bool bReliable = false ;
                    if( code > 100 && code < 200 && sip->sip_request->rq_method == sip_method_invite) {
                        int i = 0 ;
                        while( tags[i].t_tag != tag_null ) {
                            if( tags[i].t_tag == siptag_require_str && NULL != strstr( (const char*) tags[i].t_value, "100rel") ) {
                                bReliable = true ;
                                break ;
                            }
                            i++ ;
                        }
                    }

                    /* update local sdp if provided */
                    string strLocalSdp ;
                    if( !body.empty()  ) {
                        dlg->setLocalSdp( body.c_str() ) ;
                        string strLocalContentType ;
                        if( searchForHeader( tags, siptag_content_type_str, strLocalContentType ) ) {
                            dlg->setLocalContentType( strLocalContentType ) ;
                        }
                        else {
                            /* set content-type if we can detect it */
                            if( 0 == body.find("v=0") ) {
                                contentType = "application/sdp" ;
                                dlg->setLocalContentType( contentType ) ;
                            }
                        }
                    }

                    /* set session timer if required */
                    sip_session_expires_t *sessionExpires = nullptr;
                    if( 200 == code && sip->sip_request->rq_method == sip_method_invite ) {
                        string strSessionExpires ;
                        if( searchForHeader( tags, siptag_session_expires_str, strSessionExpires ) ) {
                            sip_session_expires_t* se = sip_session_expires_make(m_pController->getHome(), strSessionExpires.c_str() );
                            unsigned long interval = std::max((unsigned long) 90, se->x_delta);
                            SipDialog::SessionRefresher_t who = !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? SipDialog::they_are_refresher : SipDialog::we_are_refresher;

                            if (who == SipDialog::we_are_refresher) {
                                DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - per app we are refresher, interval will be " << interval  ;
                            }
                            else {
                                DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - per app UAC is refresher, interval will be " << interval  ;
                            }
                            dlg->setSessionTimer(interval, who) ;
                            su_free( m_pController->getHome(), se ) ;
                        }
                        else if (sip->sip_session_expires && sip->sip_session_expires->x_refresher) {
                            sip_session_expires_t* se = sip->sip_session_expires;
                            sessionExpires = sip_session_expires_copy(m_pController->getHome(), se);
                            unsigned long interval = std::max((unsigned long) 90, se->x_delta);
                            SipDialog::SessionRefresher_t who = !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? SipDialog::they_are_refresher : SipDialog::we_are_refresher;

                            if (who == SipDialog::we_are_refresher) {
                                DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - UAC asked us to refresh, interval will be " << interval  ;
                            }
                            else {
                                DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - UAC is refresher, interval will be " << interval  ;
                            }
                            dlg->setSessionTimer(interval, who) ;
                        }
                    }

                    /* iterate through data.opts.headers, adding headers to the response */
                    if( bReliable ) {
                        DR_LOG(log_debug) << "Sending " << dec << code << " response reliably"  ;
                        nta_reliable_t* rel = nta_reliable_treply( irq, uasPrack, this, code, status
                            ,TAG_IF( !hasCustomContact, SIPTAG_CONTACT_STR(contact.c_str()))
                            ,TAG_IF(!body.empty(), SIPTAG_PAYLOAD_STR(body.c_str()))
                            ,TAG_IF(!contentType.empty(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                            ,TAG_IF(sessionExpires, SIPTAG_SESSION_EXPIRES(sessionExpires))
                            ,TAG_NEXT(tags)
                            ,TAG_END() ) ;

                        if( !rel ) {
                            bSentOK = false ;
                            failMsg = "Remote endpoint does not support 100rel" ;
                            DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - failed sending reliable provisional response; most likely remote endpoint does not support 100rel"  ;
                        } 
                        else {
                            IIP_SetReliable(m_invitesInProgress, iip, rel);
                        }
                        //TODO: should probably set timer here
                    }
                    else {
                        DR_LOG(log_debug) << "Sending " << dec << code << " response (not reliably)  on irq " << hex << irq  ;
                        rc = nta_incoming_treply( irq, code, status
                            ,TAG_IF( code >= 200 && code < 300 && !hasCustomContact, SIPTAG_CONTACT_STR(contact.c_str()))
                            ,TAG_IF(!body.empty(), SIPTAG_PAYLOAD_STR(body.c_str()))
                            ,TAG_IF(!contentType.empty(), SIPTAG_CONTENT_TYPE_STR(contentType.c_str()))
                            ,TAG_IF(sessionExpires, SIPTAG_SESSION_EXPIRES(sessionExpires))
                            ,TAG_NEXT(tags)
                            ,TAG_END() ) ; 
                        if( 0 != rc ) {
                            DR_LOG(log_error) << "Error " << dec << rc << " sending response on irq " << hex << irq <<
                                " - this is usually because the application provided a syntactically-invalid header";
                            bSentOK = false ;
                            failMsg = "Unknown server error sending response" ;
                        }
                        else {
                            if( sip_method_subscribe == nta_incoming_method(irq) ) {
                                bClearIIP = true ;

                                // add dialog for SUBSCRIBE dialogs
                                if( 202 == code || 200 == code ) {
                                DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest: adding dialog for subscribe with dialog id " <<  dlg->getDialogId()  ;
                                this->addDialog( dlg ) ;
                                }
                            }

                            // sofia handles retransmits for us for final failures, but not for success
                            // TODO: figure out why this is
                            if( sip_method_invite == nta_incoming_method(irq) && code == 200 ) {
                                
                                this->addDialog( dlg ) ;

                                if (tport_is_dgram(tp)) {
                                    // set timer G to retransmit 200 OK if we don't get ack
                                    TimerEventHandle t = m_pTQM->addTimer("timerG",
                                        std::bind(&SipDialogController::retransmitFinalResponse, this, irq, tp, dlg), NULL, NTA_SIP_T1 ) ;
                                    dlg->setTimerG(t) ;
                                }
                                // set timer H, which sets the time to stop these retransmissions
                                TimerEventHandle t = m_pTQM->addTimer("timerH",
                                    std::bind(&SipDialogController::endRetransmitFinalResponse, this, irq, tp, dlg), NULL, TIMER_H_MSECS ) ;
                                dlg->setTimerH(t) ;
                            }

                            // stats
                            if (theOneAndOnlyController->getStatsCollector().enabled()) {
            
                                // response time to incoming INVITE request
                                if (sip_method_invite == nta_incoming_method(irq) && code <= 200) {
                                    auto now = std::chrono::steady_clock::now();
                                    std::chrono::duration<double> diff = now - dlg->getArrivalTime();
                                    if (!dlg->hasAlerted()) {
                                        dlg->alerting();
                                        STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_PDD_IN, diff.count())
                                    }
                                    if (code == 200) {
                                        STATS_HISTOGRAM_OBSERVE_NOCHECK(STATS_HISTOGRAM_INVITE_RESPONSE_TIME_IN, diff.count())
                                    }
                                }
                            }
                        }
                    }
                    msg_destroy( msg ); //release the reference
                    if (sessionExpires) su_free(m_pController->getHome(), sessionExpires);
                }
            }
        }
        else {

            DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - unable to find irq or iip for transaction id " << transactionId  ;

            /* failed */
            string strMethod ;
            bSentOK = false ;
            failMsg = "Response not sent due to unknown transaction" ;  

            if( FindCSeqMethod( headers, strMethod ) ) {
                DR_LOG(log_debug) << "silently discarding response to " << strMethod  ;

                if( 0 == strMethod.compare("CANCEL") ) {
                    failMsg = "200 OK to incoming CANCEL is automatically generated by the stack" ;
                }
            }                     
        }

        if( bSentOK ) {
            string encodedMessage ;
            msg_t* msg = nta_incoming_getresponse( irq ) ;  // adds a ref

            // we can get an rc=0 from nta_incoming_treply above, but have it actually fail
            // in the case of a websocket that closed immediately after sending us a BYE
            if (msg) {
                sip_t *sip = sip_object( msg );
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta( msg, irq, "application" ) ;

                STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {
                    {"method", sip->sip_cseq->cs_method_name},
                    {"code", boost::lexical_cast<std::string>(code)}})

                string s ;
                meta.toMessageFormat(s) ;
                string data = s + "|" + transactionId + "|" + dialogId + "|" + "|Msg sent:|" + DR_CRLF + encodedMessage ;

                m_pController->getClientController()->route_api_response( clientMsgId, "OK", data) ;

                if( iip && code >= 300 ) {
                    Cdr::postCdr( std::make_shared<CdrStop>( msg, "application", Cdr::call_rejected ) );
                }
                else if (iip && code == 200) {
                    Cdr::postCdr( std::make_shared<CdrStart>( msg, "application", Cdr::uas ) );                
                }

                msg_destroy(msg) ;      // release the ref                          
            }
            else {
                m_pController->getClientController()->route_api_response( clientMsgId, "NOK", "failed sending, possibly due to far end closing socket") ;
            }
        }
        else {
            m_pController->getClientController()->route_api_response( clientMsgId, "NOK", failMsg) ;
        }
        /* tell client controller to flush transaction data on any final response to a non-INVITE */
        if( sip_method_invite != nta_incoming_method(irq) && code >= 200 ) {
            m_pController->getClientController()->removeNetTransaction( transactionId ) ;
        }
        else if( sip_method_invite == nta_incoming_method(irq) && code > 200 ) {
            m_pController->getClientController()->removeNetTransaction( transactionId ) ;            
        }
        else if(sip_method_invite == nta_incoming_method(irq) && code == 200 && existingDialog) {
            m_pController->getClientController()->removeNetTransaction( transactionId ) ;            
        }

        if( bClearIIP && iip) {
            IIP_Clear(m_invitesInProgress, iip);
        }

        if( bDestroyIrq && !transportGone) nta_incoming_destroy(irq) ;    

        pData->~SipMessageData() ;

        deleteTags( tags );

        su_free(m_pController->getHome(), sip_status);

    }

    int SipDialogController::processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "SipDialogController::processRequestInsideDialog: " << sip->sip_request->rq_method_name << " irq " << irq  ;
        int rc = 0 ;
        string transactionId ;
        generateUuid( transactionId ) ;

        switch (sip->sip_request->rq_method) {
            case sip_method_ack:
            {
                /* ack to 200 OK comes here  */
                std::shared_ptr<IIP> iip ;
                std::shared_ptr<SipDialog> dlg ;       
                if (!IIP_FindByLeg(m_invitesInProgress, leg, iip)) {
                    
                    /* not a new INVITE, so it should be found as an existing dialog; i.e. a reINVITE */
                    if( !findDialogByLeg( leg, dlg ) ) {
                        DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg"  ;
                        assert(0) ;
                        return -1 ;
                    }
                }
                else {
                    transactionId = iip->getTransactionId() ;
                    dlg = iip->dlg();
                    IIP_Clear(m_invitesInProgress, iip);
                    this->clearSipTimers(dlg);
                    //addDialog( dlg ) ;  now adding when we send the 200 OK
                }
                string encodedMessage ;
                msg_t* msg = nta_incoming_getrequest( irq ) ; // adds a reference
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta( msg, irq ) ;
                msg_destroy(msg) ;      // releases the reference

                m_pController->getClientController()->route_ack_request_inside_dialog(  encodedMessage, meta, irq, sip, transactionId, dlg->getTransactionId(), dlg->getDialogId() ) ;

                nta_incoming_destroy(irq) ;
                break ;
            }
            case sip_method_cancel:
            {
                // this should only happen in a race condition, where we've sent the 200 OK but not yet received an ACK 
                //  in this case, send a 481 to the CANCEL and then generate a BYE
                std::shared_ptr<SipDialog> dlg ;
                if( !this->findDialogByLeg( leg, dlg ) ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg"  ;
                    return 481 ;
                    assert(0) ;
                    return -1;
                }
                DR_LOG(log_warning) << "SipDialogController::processRequestInsideDialog - received CANCEL after 200 OK; reply 481 and tear down dialog"  ;
                this->clearSipTimers(dlg);

                // 481 to the CANCEL
                nta_incoming_treply( irq, SIP_481_NO_TRANSACTION, TAG_END() ) ;  

                // BYE to the far end
                nta_outgoing_t* orq = nta_outgoing_tcreate( leg, NULL, NULL,
                                        NULL,
                                        SIP_METHOD_BYE,
                                        NULL,
                                        TAG_IF(dlg->getTport(), NTATAG_TPORT(dlg->getTport())),
                                        SIPTAG_REASON_STR("SIP ;cause=200 ;text=\"CANCEL after 200 OK\""),
                                        TAG_END() ) ;

                msg_t* m = nta_outgoing_getrequest(orq) ;  // adds a reference
                sip_t* sip = sip_object( m ) ;

                DR_LOG(log_info) << "SipDialogController::processRequestInsideDialog - (cancel) created orq " << std::hex << (void *) orq  <<
                    " call-id " << sip->sip_call_id->i_id;

                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta(m, orq) ;
                string s ;
                meta.toMessageFormat(s) ;

                m_pController->getClientController()->route_request_inside_dialog( encodedMessage, meta, sip, "unsolicited", dlg->getDialogId() ) ;
                msg_destroy(m);      // releases the reference

                DR_LOG(log_info) << "SipDialogController::processRequestInsideDialog - destroying orq from BYE";
                nta_outgoing_destroy(orq) ;
                DR_LOG(log_info) << "SipDialogController::processRequestInsideDialog - clearing dialog";
                SD_Clear(m_dialogs, leg ) ;
                DR_LOG(log_info) << "SipDialogController::processRequestInsideDialog - clearing IIP";
                IIP_Clear(m_invitesInProgress, leg);

            }
            default:
            {
                std::shared_ptr<SipDialog> dlg ;
                if( !this->findDialogByLeg( leg, dlg ) ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg"  ;
                    return 481 ;
                    assert(0) ;
                }

                if (sip_method_invite == sip->sip_request->rq_method) {
                    nta_incoming_treply(irq, SIP_100_TRYING, TAG_END());
                }
                
                /* we are relying on the client to eventually respond. Clients should be treated as unreliable in this sense.
                 Store txnId with dlg so we can clear them if client has not responded by the time we tear down the dialog. 
                 
                 BYE is an exception, because we clear the dialog when we receive the BYE (not when we send the 200 OK)
                 and as a result if we added it below we would immediately delete the irq and generate a 500 before
                 the client had a chance to respond.
                 */
                if (sip_method_bye != sip->sip_request->rq_method) {
                  dlg->addIncomingRequestTransaction(transactionId);
                }

                /* if this is a re-INVITE or an UPDATE deal with session timers */
                if( sip_method_invite == sip->sip_request->rq_method || sip_method_update == sip->sip_request->rq_method ) {
                    bool weAreRefresher = false;
                    if( dlg->hasSessionTimer() ) { 
                        DR_LOG(log_info) << "SipDialogController::processRequestInsideDialog - canceling session expires timer due to re-invite"  ;
                        weAreRefresher = dlg->areWeRefresher();
                        dlg->cancelSessionTimer() ;
                    }

                    /* reject if session timer is too small */
                    if( sip->sip_session_expires && sip->sip_session_expires->x_delta < dlg->getMinSE() ) {
                        ostringstream o ;
                        o << dlg->getMinSE() ;
                        nta_incoming_treply( irq, SIP_422_SESSION_TIMER_TOO_SMALL, 
                            SIPTAG_MIN_SE_STR(o.str().c_str()),
                            TAG_END() ) ;  
                        return 0 ;             
                    }
                    if( sip->sip_session_expires ) {
                        dlg->setSessionTimer( sip->sip_session_expires->x_delta, 
                            (!sip->sip_session_expires->x_refresher && weAreRefresher) ||(sip->sip_session_expires->x_refresher && 0 == strcmp( sip->sip_session_expires->x_refresher, "uac")) ? 
                            SipDialog::they_are_refresher : 
                            SipDialog::we_are_refresher) ;
                    }

                }

                string encodedMessage ;
                msg_t* msg = nta_incoming_getrequest( irq ) ;   //adds a reference
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta( msg, irq ) ;
                msg_destroy( msg ); // release the reference

                bool routed = m_pController->getClientController()->route_request_inside_dialog( encodedMessage, meta, sip, transactionId, dlg->getDialogId() ) ;
                if (!routed && dlg->getSipStatus() < 200) {
                    // got a request before we sent a 200 OK to the initial INVITE, treat as an out-of-dialog request
                    switch (sip->sip_request->rq_method) {
                        case sip_method_notify:
                        case sip_method_options:
                        case sip_method_info:
                        case sip_method_message:
                        case sip_method_publish:
                        case sip_method_subscribe:
                            DR_LOG(log_debug) << "SipDialogController::processRequestInsideDialog: received irq " << std::hex << (void *) irq << " for out-of-dialog request"  ;
                            rc = m_pController->processMessageStatelessly( msg, (sip_t*) sip);
                            return rc;
                        default:
                        break;
                    }
                }

                addIncomingRequestTransaction( irq, transactionId) ;
    
                if( sip_method_bye == sip->sip_request->rq_method || 
                    (sip_method_notify == sip->sip_request->rq_method && !dlg->isInviteDialog() &&
                        NULL != sip->sip_subscription_state && 
                        NULL != sip->sip_subscription_state->ss_substate &&
                        NULL != strstr(sip->sip_subscription_state->ss_substate, "terminated") ) 
                ) {

                    this->clearSipTimers(dlg);

                    //clear dialog when we send a 200 OK response to BYE
                    SD_Clear(m_dialogs, leg ) ;
                    if( !routed ) {
                        nta_incoming_treply( irq, SIP_481_NO_TRANSACTION, TAG_END() ) ;                
                    }
                    
                    // check for race condition where we received a BYE with a re-INVITE we sent still outstanding
                    auto txnId = dlg->getTransactionId();
                    std::shared_ptr<IIP> iip ;
                    if (IIP_FindByTransactionId(m_invitesInProgress, txnId, iip)) {
                        IIP_Clear(m_invitesInProgress, iip);
                        nta_outgoing_t* orq = const_cast<nta_outgoing_t *>(iip->orq());
                        DR_LOG(log_info) << "SipDialogController::processRequestInsideDialog: cleared IIP for reinvite due to recv BYE";
                        if (orq) {
                            std::shared_ptr<RIP> pRIP;
                            if (findRIPByOrq(orq, pRIP)) {
                                DR_LOG(log_info) << "SipDialogController::processRequestInsideDialog: cleared outstanding RIP due to recv BYE, dialogId is " << txnId  ;
                                m_pClientController->removeAppTransaction(pRIP->getTransactionId());
                                clearRIP(orq);
                            }
                        }
                    }
                 }

                if (sip_method_bye == sip->sip_request->rq_method) {
                  Cdr::postCdr( std::make_shared<CdrStop>( msg, "network", Cdr::normal_release ) );
                }
            }
        }
        return rc ;
    }
    int SipDialogController::processResponseInsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
        DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: "  ;
    	ostringstream o ;
        std::shared_ptr<RIP> rip  ;
        sip_method_t method = sip->sip_cseq->cs_method;
        int statusCode = sip->sip_status->st_status ;

        if( findRIPByOrq( orq, rip ) ) {
            DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: found request for "  << sip->sip_cseq->cs_method_name << " sip status " << statusCode ;

            string encodedMessage ;
            bool truncated ;
            msg_t* msg = nta_outgoing_getresponse(orq) ;  // adds a reference
            SipMsgData_t meta( msg, orq, "network") ;
            EncodeStackMessage( sip, encodedMessage ) ;
            
            m_pController->getClientController()->route_response_inside_transaction( encodedMessage, meta, orq, sip, rip->getTransactionId(), rip->getDialogId() ) ;            

            if (method == sip_method_invite && 200 == statusCode) {
                tport_t *tp = nta_outgoing_transport(orq) ; // takes a ref on the tport..
                // start a timerD for this successful reINVITE
                if (tp) {
                  if (tport_is_dgram(tp)) m_timerDHandler.addInvite(orq);
                  tport_unref(tp);  // ..releases it
                }
                
                /* reset session expires timer, if provided */
                sip_session_expires_t* se = sip_session_expires(sip) ;
                if( se ) {
                    std::shared_ptr<SipDialog> dlg ;
                    nta_leg_t* leg = nta_leg_by_call_id(m_pController->getAgent(), sip->sip_call_id->i_id);
                    DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: searching for dialog by leg " << std::hex << (void *) leg;
                    if(leg && findDialogByLeg( leg, dlg )) {
                        DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: (re)setting session expires timer to " <<  se->x_delta;
                        //TODO: if session-expires value is less than min-se ACK and then BYE with Reason header    
                        dlg->setSessionTimer( se->x_delta, 
                            !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? 
                                SipDialog::we_are_refresher : 
                                SipDialog::they_are_refresher ) ;
                    }
                    else {
                        DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: unable to find dialog for leg " << std::hex << (void *) leg;
                    }
                }
                else {
                    DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: no session expires header found";
                }
            }
            if (rip->shouldClearDialogOnResponse()) {
                string dialogId = rip->getDialogId() ;
                if (sip->sip_cseq->cs_method == sip_method_bye && (sip->sip_status->st_status == 407 || sip->sip_status->st_status == 401)) {
                    DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: NOT clearing dialog after receiving 401/407 response to BYE"  ;
                }
                else if( dialogId.length() > 0 ) {
                    DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: clearing dialog after receiving response to BYE or notify w/ subscription-state terminated"  ;
                    SD_Clear(m_dialogs, dialogId ) ;
                }
                else {
                    DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: got 200 OK to BYE but don't have dialog id"  ;
                    assert(false) ;
                }
            }
            clearRIP( orq ) ;     
            msg_destroy(msg) ;   // releases reference
        }
        else {
            DR_LOG(log_error) << "SipDialogController::processResponseInsideDialog: unable to find request associated with response"  ;            
            nta_outgoing_destroy( orq ) ;
        }
  
		return 0 ;
    }
    int SipDialogController::processResponseToRefreshingReinvite( nta_outgoing_t* orq, sip_t const* sip ) {
        DR_LOG(log_debug) << "SipDialogController::processResponseToRefreshingReinvite: "  ;
        ostringstream o ;
        std::shared_ptr<RIP> rip  ;

        nta_leg_t* leg = nta_leg_by_call_id(m_pController->getAgent(), sip->sip_call_id->i_id);
        assert(leg) ;
        std::shared_ptr<SipDialog> dlg ;
        if( !findDialogByLeg( leg, dlg ) ) {
            assert(0) ;
        }
        if( findRIPByOrq( orq, rip ) ) {

            if( sip->sip_status->st_status != 200 ) {
                DR_LOG(log_info) << "SipDialogController::processResponseToRefreshingReinvite: reinvite failed (status="
                                 << sip->sip_status->st_status << ") - clearing dialog";
                notifyTerminateStaleDialog( dlg );
                clearRIP( orq );
                return 0;
            }
            else {
                /* reset session expires timer, if provided */
                sip_session_expires_t* se = sip_session_expires(sip) ;
                if( se ) {                
                    //TODO: if session-expires value is less than min-se ACK and then BYE with Reason header    
                    dlg->setSessionTimer( se->x_delta, 
                        !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? 
                            SipDialog::we_are_refresher : 
                            SipDialog::they_are_refresher ) ;
                }
             }

            nta_outgoing_t* ack_request = nta_outgoing_tcreate(leg, NULL, NULL, NULL,
                   SIP_METHOD_ACK,
                   (url_string_t*) sip->sip_contact->m_url ,
                   TAG_END());

            nta_outgoing_destroy( ack_request ) ;
            clearRIP( orq ) ;

            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", "ACK"}})
            return 0;
        }
        nta_outgoing_destroy( orq ) ;
        return 0 ;
        
    }

    int SipDialogController::processCancelOrAck( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) {
        std::shared_ptr<IIP> iip ;
        if( !sip ) {
            DR_LOG(log_debug) << "SipDialogController::processCancel with null sip pointer; irq " << 
                hex << (void*) irq << ", most probably timerH indicating end of final response retransmissions" ;
            //nta_incoming_destroy(irq);
            std::shared_ptr<IIP> iip ;
            if (!IIP_FindByIrq(m_invitesInProgress, irq, iip)) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for irq " << hex << (void*) irq;
            }
            else {
                DR_LOG(log_debug) << "SipDialogController::processCancelOrAck - clearing IIP for leg " << hex << (void*) iip->leg();   ;
                IIP_Clear(m_invitesInProgress, iip);
            }
            return -1 ;
        }
        DR_LOG(log_debug) << "SipDialogController::processCancelOrAck: " << sip->sip_request->rq_method_name  ;
        string transactionId ;
        generateUuid( transactionId ) ;

        if( sip->sip_request->rq_method == sip_method_cancel ) {
            if (!IIP_FindByIrq(m_invitesInProgress, irq, iip)) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for CANCEL with call-id " << sip->sip_call_id->i_id  ;
                return 0 ;
            }
            std::shared_ptr<SipDialog> dlg = iip->dlg() ;

            if( !dlg ) {
                DR_LOG(log_error) << "No dialog exists for invite-in-progress for CANCEL with call-id " << sip->sip_call_id->i_id  ;
                return 0 ;
            }

            DR_LOG(log_debug) << "SipDialogController::processCancelOrAck - Received CANCEL for call-id " << sip->sip_call_id->i_id << ", sending to client"  ;

            string encodedMessage ;
            msg_t* msg = nta_incoming_getrequest( irq ) ;   // adds a reference
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta( msg, irq ) ;
            Cdr::postCdr( std::make_shared<CdrStop>( msg, "network", Cdr::call_canceled ) );
            msg_destroy(msg);                               // releases reference

            m_pClientController->route_request_inside_invite( encodedMessage, meta, irq, sip, iip->getTransactionId(), dlg->getDialogId() ) ;
            
            //TODO: sofia has already sent 200 OK to cancel and 487 to INVITE.  Do we need to keep this irq around?
            //addIncomingRequestTransaction( irq, transactionId) ;

            DR_LOG(log_debug) << "SipDialogController::processCancelOrAck - clearing IIP "   ;
            IIP_Clear(m_invitesInProgress, iip);
            DR_LOG(log_debug) << "SipDialogController::processCancelOrAck - done clearing IIP "   ;

        }
        else if( sip->sip_request->rq_method == sip_method_ack ) {
            if (!IIP_FindByIrq(m_invitesInProgress, irq, iip)) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for ACK with call-id " << sip->sip_call_id->i_id  ;
                return 0 ;
            }
            std::shared_ptr<SipDialog> dlg = iip->dlg(); 
            IIP_Clear(m_invitesInProgress, iip);
            this->clearSipTimers(dlg);

            string transactionId ;
            generateUuid( transactionId ) ;

            string encodedMessage ;
            msg_t* msg = nta_incoming_getrequest( irq ) ;  // adds a reference
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta( msg, irq ) ;
            msg_destroy( msg ) ;    //release the reference

            m_pController->getClientController()->route_ack_request_inside_dialog( encodedMessage, meta, irq, sip, transactionId, dlg->getTransactionId(), dlg->getDialogId() ) ;   
            
            //NB: when we get a CANCEL sofia sends the 487 response to the INVITE itself, so our latest sip status will be a provisional
            //not sure that we need to do anything particular about that however....though it we write cdrs we would want to capture the 487 final response
        
            //another issue is that on any non-success response sent to an incoming INVITE the subsequent ACK is not sent to the client
            //because there is no dialog created and thus no routing available.  Should fix this.
        }
        else {
            DR_LOG(log_debug) << "Received " << sip->sip_request->rq_method_name << " for call-id " << sip->sip_call_id->i_id << ", discarding"  ;
        }
        return 0 ;
    }
    int SipDialogController::processPrack( nta_reliable_t *rel, nta_incoming_t *prack, sip_t const *sip) {
        DR_LOG(log_debug) << "SipDialogController::processPrack: "  ;
        std::shared_ptr<IIP> iip  ;
        if (IIP_FindByReliable(m_invitesInProgress, rel, iip)) {
            std::string transactionId ;
            generateUuid( transactionId ) ;
            std::shared_ptr<SipDialog> dlg = iip->dlg() ;
            assert( dlg ) ;

            m_pClientController->addDialogForTransaction( dlg->getTransactionId(), dlg->getDialogId() ) ;  

            string encodedMessage ;
            msg_t* msg = nta_incoming_getrequest( prack ) ; // adds a reference
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta( msg, prack ) ;
            msg_destroy(msg);                               // releases the reference

            m_pClientController->route_request_inside_dialog( encodedMessage, meta, sip, transactionId, dlg->getDialogId() ) ;

            iip->destroyReliable() ;

            addIncomingRequestTransaction( prack, transactionId) ;
        }
        else {
            assert(0) ;
        }
        return 0 ;
    }
    void SipDialogController::notifyRefreshDialog( std::shared_ptr<SipDialog> dlg ) {
        nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
        if( leg ) {
            string strSdp = dlg->getLocalEndpoint().m_strSdp ;
            string strContentType = dlg->getLocalEndpoint().m_strContentType ;

            assert( dlg->getSessionExpiresSecs() ) ;
            ostringstream o,v ;
            o << dlg->getSessionExpiresSecs() << "; refresher=uac" ;
            v << dlg->getMinSE() ;

            nta_outgoing_t* orq = nta_outgoing_tcreate( leg,  response_to_refreshing_reinvite, (nta_outgoing_magic_t *) m_pController,
                                            NULL,
                                            SIP_METHOD_INVITE,
                                            NULL,
                                            SIPTAG_SESSION_EXPIRES_STR(o.str().c_str()),
                                            SIPTAG_MIN_SE_STR(v.str().c_str()),
                                            SIPTAG_CONTACT_STR( dlg->getLocalContactHeader().c_str() ),
                                            SIPTAG_CONTENT_TYPE_STR(strContentType.c_str()),
                                            SIPTAG_PAYLOAD_STR(strSdp.c_str()),
                                            TAG_END() ) ;
            
            string transactionId ;
            generateUuid( transactionId ) ;

            std::shared_ptr<RIP> p = std::make_shared<RIP>( transactionId ) ; 
            addRIP( orq, p ) ;

            DR_LOG(log_info) << "SipDialogController::notifyRefreshDialog - created orq " << std::hex << (void *) orq;

            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", "INVITE"}})

            //m_pClientController->route_event_inside_dialog( "{\"eventName\": \"refresh\"}",dlg->getTransactionId(), dlg->getDialogId() ) ;
        }
    }
    void SipDialogController::notifyTerminateStaleDialog( std::shared_ptr<SipDialog> dlg, bool ackbye ) {
        nta_leg_t* leg = const_cast<nta_leg_t *>(dlg->getNtaLeg()) ;
        const char* reason = ackbye ? "SIP ;cause=200 ;text=\"ACK-BYE due to cancel race condition\"" : "SIP ;cause=200 ;text=\"Session timer expired\"";
        if( leg ) {
            nta_outgoing_t* orq = nta_outgoing_tcreate( leg, NULL, NULL,
                                            NULL,
                                            SIP_METHOD_BYE,
                                            NULL,
                                            SIPTAG_REASON_STR(reason),
                                            TAG_END() ) ;
            msg_t* m = nta_outgoing_getrequest(orq) ;    // adds a reference
            sip_t* sip = sip_object( m ) ;

            DR_LOG(log_info) << "SipDialogController::notifyTerminateStaleDialog - created orq " << std::hex << (void *) orq;

            string byeTransactionId  = "unsolicited";

            string encodedMessage ;
            EncodeStackMessage( sip, encodedMessage ) ;
            SipMsgData_t meta(m, orq) ;
            string s ;
            meta.toMessageFormat(s) ;
            string data = s + "|" + byeTransactionId + "|Msg sent:|" + DR_CRLF + encodedMessage ;
            msg_destroy(m) ;    // releases reference::process

            // this is slightly inaccurate: we are telling the app we received a BYE when we are in fact generating it
            // the impact is minimal though, and currently there is no message type to inform the app we generated a BYE on our own
            bool routed = m_pController->getClientController()->route_request_inside_dialog( encodedMessage, meta, sip, byeTransactionId, dlg->getDialogId() ) ;

            Cdr::postCdr( std::make_shared<CdrStop>( m, "application", ackbye ? Cdr::ackbye : Cdr::session_expired ) );
            nta_outgoing_destroy(orq) ;

            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", "BYE"}})
        }
        SD_Clear(m_dialogs, dlg) ;
    }
    void SipDialogController::notifyCancelTimeoutReachedIIP( std::shared_ptr<IIP> iip ) {
        DR_LOG(log_info) << "SipDialogController::notifyCancelTimeoutReachedIIP - tearing down transaction id " << iip->getTransactionId() ;
        m_pController->getClientController()->removeAppTransaction( iip->getTransactionId() ) ;
        IIP_Clear(m_invitesInProgress, iip) ;
    }

    void SipDialogController::bindIrq( nta_incoming_t* irq ) {
        nta_incoming_bind( irq, uasCancelOrAck, (nta_incoming_magic_t *) m_pController ) ;
    }
    bool SipDialogController::searchForHeader( tagi_t* tags, tag_type_t header, string& value ) {
        int i = 0 ;
        while( tags[i].t_tag != tag_null ) {
            if( tags[i].t_tag == header ) {
                value.assign( (const char*) tags[i].t_value );
                return true ;
            }
            i++ ;
        }
        return false ;
    }
    void SipDialogController::addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, const string& transactionId, std::shared_ptr<SipDialog> dlg, const string& tag ) {
        const char* a_tag = nta_incoming_tag( irq, tag.length() == 0 ? NULL : tag.c_str()) ;
        nta_leg_tag( leg, a_tag ) ;
        dlg->setLocalTag( a_tag ) ;

        IIP_Insert(m_invitesInProgress, leg, irq, transactionId, dlg);

        this->bindIrq( irq ) ;
    }
    void SipDialogController::addOutgoingInviteTransaction( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip, std::shared_ptr<SipDialog> dlg ) {
        DR_LOG(log_debug) << "SipDialogController::addOutgoingInviteTransaction:  adding leg " << std::hex << leg  ;
        IIP_Insert(m_invitesInProgress, leg, orq, dlg->getTransactionId(), dlg);
    }

    void SipDialogController::addRIP( nta_outgoing_t* orq, std::shared_ptr<RIP> rip) {
        DR_LOG(log_debug) << "SipDialogController::addRIP adding orq " << std::hex << (void*) orq  ;
        std::lock_guard<std::mutex> lock(m_mutex) ;
        m_mapOrq2RIP.insert( mapOrq2RIP::value_type(orq,rip)) ;
    }
    bool SipDialogController::findRIPByOrq( nta_outgoing_t* orq, std::shared_ptr<RIP>& rip ) {
        DR_LOG(log_debug) << "SipDialogController::findRIPByOrq orq " << std::hex << (void*) orq  ;
        std::lock_guard<std::mutex> lock(m_mutex) ;
        mapOrq2RIP::iterator it = m_mapOrq2RIP.find( orq ) ;
        if( m_mapOrq2RIP.end() == it ) return false ;
        rip = it->second ;
        return true ;                       
    }
    void SipDialogController::clearRIP( nta_outgoing_t* orq ) {
        DR_LOG(log_debug) << "SipDialogController::clearRIP clearing orq " << std::hex << (void*) orq  ;
        std::lock_guard<std::mutex> lock(m_mutex) ;
        mapOrq2RIP::iterator it = m_mapOrq2RIP.find( orq ) ;
        nta_outgoing_destroy( orq ) ;
        if( m_mapOrq2RIP.end() == it ) return  ;
        m_mapOrq2RIP.erase( it ) ;                      
    }
    void SipDialogController::clearRIPByDialogId( const std::string dialogId) {
        DR_LOG(log_debug) << "SipDialogController::clearRIPByDialogId - searching for RIP for dialog id " <<  dialogId  ;
        for (const auto& pair : m_mapOrq2RIP) {
            nta_outgoing_t* orq = pair.first;
            std::shared_ptr<RIP> p = pair.second;
            if (0 == dialogId.compare(p->getDialogId())) {
                DR_LOG(log_debug) << "SipDialogController::clearRIPByDialogId - found for RIP for dialog id, orq to destroy is " <<
                std::hex << (void *) orq;
                m_mapOrq2RIP.erase(orq);
                nta_outgoing_destroy( orq ) ;
                return;
            }
        }
        return;
    }
        
    void SipDialogController::retransmitFinalResponse( nta_incoming_t* irq, tport_t* tp, std::shared_ptr<SipDialog> dlg) {
        DR_LOG(log_debug) << "SipDialogController::retransmitFinalResponse irq:" << std::hex << (void*) irq;
        incoming_retransmit_reply(irq, tp);

        // set next timer
        uint32_t ms = dlg->bumpTimerG() ;
        TimerEventHandle t = m_pTQM->addTimer("timerG", 
            std::bind(&SipDialogController::retransmitFinalResponse, this, irq, tp, dlg), NULL, ms ) ;
        dlg->setTimerG(t) ;
    }

    /**
     * timer H went off.  Stop timer G (retransmits of 200 OK) and clear it and timer H
     */
    void SipDialogController::endRetransmitFinalResponse( nta_incoming_t* irq, tport_t* tp, std::shared_ptr<SipDialog> dlg) {
        DR_LOG(log_error) << "SipDialogController::endRetransmitFinalResponse - never received ACK for final response to incoming INVITE; irq:" << 
            std::hex << (void*) irq << " source address was " << dlg->getSourceAddress() ;

        nta_leg_t* leg = const_cast<nta_leg_t *>(dlg->getNtaLeg());
        TimerEventHandle h = dlg->getTimerG() ;
        if( h ) {
            m_pTQM->removeTimer( h, "timerG");
            dlg->clearTimerG();
        }
        h = dlg->getTimerH() ;
        if (h) {
            dlg->clearTimerH();
        }

        IIP_Clear(m_invitesInProgress, leg);


        // we never got the ACK, so now we should tear down the call by sending a BYE
        // TODO: also need to remove dialog from hash table
        nta_outgoing_t* orq = nta_outgoing_tcreate( leg, NULL, NULL,
                                NULL,
                                SIP_METHOD_BYE,
                                NULL,
                                SIPTAG_REASON_STR("SIP ;cause=200 ;text=\"ACK timeout\""),
                                TAG_END() ) ;

        msg_t* m = nta_outgoing_getrequest(orq) ;  // adds a reference
        sip_t* sip = sip_object( m ) ;

        DR_LOG(log_info) << "SipDialogController::endRetransmitFinalResponse - created orq " << std::hex << (void *) orq 
            << " for BYE on leg " << (void *)leg;

        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_OUT, {{"method", "BYE"}})

        string encodedMessage ;
        EncodeStackMessage( sip, encodedMessage ) ;
        SipMsgData_t meta(m, orq) ;
        string s ;
        meta.toMessageFormat(s) ;

        m_pController->getClientController()->route_request_inside_dialog( encodedMessage, meta, sip, "unsolicited", dlg->getDialogId() ) ;

        msg_destroy( m ); // release the reference

        nta_outgoing_destroy(orq) ;
        SD_Clear(m_dialogs, leg);
    }
    void SipDialogController::addIncomingRequestTransaction( nta_incoming_t* irq, const string& transactionId) {
        DR_LOG(log_debug) << "SipDialogController::addIncomingRequestTransaction - adding transactionId " << transactionId << " for irq:" << std::hex << (void*) irq;
        std::lock_guard<std::mutex> lock(m_mutex) ;
        m_mapTransactionId2Irq.insert( mapTransactionId2Irq::value_type(transactionId, irq)) ;
    }
    bool SipDialogController::findIrqByTransactionId( const string& transactionId, nta_incoming_t*& irq ) {
        std::lock_guard<std::mutex> lock(m_mutex) ;
        mapTransactionId2Irq::iterator it = m_mapTransactionId2Irq.find( transactionId ) ;
        if( m_mapTransactionId2Irq.end() == it ) return false ;
        irq = it->second ;
        return true ;                       
    }
    nta_incoming_t* SipDialogController::findAndRemoveTransactionIdForIncomingRequest( const string& transactionId ) {
        DR_LOG(log_debug) << "SipDialogController::findAndRemoveTransactionIdForIncomingRequest - searching transactionId " << transactionId ;
        std::lock_guard<std::mutex> lock(m_mutex) ;
        nta_incoming_t* irq = nullptr ;
        mapTransactionId2Irq::iterator it = m_mapTransactionId2Irq.find( transactionId ) ;
        if( m_mapTransactionId2Irq.end() != it ) {
            irq = it->second ;
            m_mapTransactionId2Irq.erase( it ) ;
        }
        else {
            DR_LOG(log_debug) << "SipDialogController::findAndRemoveTransactionIdForIncomingRequest - failed to find transactionId " << transactionId << 
                ", most likely this is a response to an invite we sent";
        }
        return irq ;
    }
    void SipDialogController::clearDanglingIncomingRequests(std::vector<std::string> txnIds) {
        auto count = txnIds.size();
        for (const std::string& txnId : txnIds) {
            auto* irq = findAndRemoveTransactionIdForIncomingRequest(txnId);
            DR_LOG(log_info) << "SipDialogController::clearDanglingIncomingRequests txn / irq: " << txnId << std::hex << " : " << (void *) irq;
            m_pController->getClientController()->removeNetTransaction(txnId);
            if (irq != nullptr) {
                nta_incoming_destroy(irq);
            }
        }
    }
    void SipDialogController::clearSipTimers(std::shared_ptr<SipDialog>& dlg) {
        DR_LOG(log_debug) << "SipDialogController::clearSipTimers for " << dlg->getCallId()  ;
        TimerEventHandle h = dlg->getTimerG() ;
        if( h ) {
            m_pTQM->removeTimer( h, "timerG");  
            dlg->clearTimerG();
        }
        h = dlg->getTimerH() ;
        if( h ) {
            m_pTQM->removeTimer( h, "timerH"); 
            dlg->clearTimerH();
        }
    }

    bool SipDialogController::stopTimerD(nta_outgoing_t* invite) {
        return m_timerDHandler.clearTimerD(invite);
    }

    // TimerDHandler

    // when we get a 200 OK to an INVITE we sent, call this to prepare handling timerD
    void TimerDHandler::addInvite(nta_outgoing_t* invite) {
        string callIdAndCSeq = combineCallIdAndCSeq(invite);
        
        // should never see this twice
        assert(m_mapCallIdAndCSeq2Invite.end() == m_mapCallIdAndCSeq2Invite.find(callIdAndCSeq));

        // we are waiting for the ACK from the app
        m_mapCallIdAndCSeq2Invite.insert(mapCallIdAndCSeq2Invite::value_type(callIdAndCSeq, invite));

        // start timerD
        TimerEventHandle t = m_pTQM->addTimer("timerD", std::bind(&TimerDHandler::timerD, this, invite, callIdAndCSeq), NULL, TIMER_D_MSECS ) ;

        DR_LOG(log_info) << "TimerDHandler::addInvite orq " << hex << (void *)invite << ", " << callIdAndCSeq;

    }

    // ..then, when the app gives us the ACK to send out, call this to save for possible retransmits
    void TimerDHandler::addAck(nta_outgoing_t* ack) {
        string callIdAndCSeq = combineCallIdAndCSeq(ack);

        mapCallIdAndCSeq2Invite::const_iterator it = m_mapCallIdAndCSeq2Invite.find(callIdAndCSeq);
        if (m_mapCallIdAndCSeq2Invite.end() != it) {
            m_mapInvite2Ack.insert(mapInvite2Ack::value_type(it->second, ack));
            m_mapCallIdAndCSeq2Invite.erase(it);
            DR_LOG(log_info) << "TimerDHandler::addAck " << hex << (void *)ack << ", " << callIdAndCSeq;
        }
        else {
            DR_LOG(log_error) << "TimerDHandler::addAck - failed to find outbound invite we sent for callid " << nta_outgoing_call_id(ack);
        }
    }

    // call this when we received a response to check it if is a retransmitted response
    bool TimerDHandler::resendIfNeeded(nta_outgoing_t* invite) {
        mapInvite2Ack::const_iterator it = m_mapInvite2Ack.find(invite);
        if (it != m_mapInvite2Ack.end()) {
            outgoing_retransmit(it->second) ;
            return true;
        }
        else if (m_mapCallIdAndCSeq2Invite.size() > 0) {
            string callIdAndCSeq = combineCallIdAndCSeq(invite);
            if (m_mapCallIdAndCSeq2Invite.find(callIdAndCSeq) != m_mapCallIdAndCSeq2Invite.end()) {
                DR_LOG(log_error) << "TimerDHandler::resendIfNeeded - cannot retransmit ACK because app has not yet provided it " << nta_outgoing_call_id(invite);
                return true;
            }
        }
        return false;
    }

    // this will automatically remove the transactions at the proper time, after timer D has expired
    void TimerDHandler::timerD(nta_outgoing_t* invite, const string& callIdAndCSeq) {
        mapCallIdAndCSeq2Invite::const_iterator it = m_mapCallIdAndCSeq2Invite.find(callIdAndCSeq);
        if (it != m_mapCallIdAndCSeq2Invite.end()) {
            DR_LOG(log_error) << "TimerDHandler::timerD - app never sent ACK for successful uac INVITE"  ;
            m_mapCallIdAndCSeq2Invite.erase(it);
        }
        else {
            mapInvite2Ack::const_iterator it = m_mapInvite2Ack.find(invite);
            if (it != m_mapInvite2Ack.end()) {
                DR_LOG(log_info) << "TimerDHandler::timerD - freeing ACK orq " << hex << (void *) it->second <<
                    " associated with invite orq " << invite << " for call-id/cseq " << callIdAndCSeq;
                nta_outgoing_destroy(it->second);
                m_mapInvite2Ack.erase(it);
            }
        }
    }

    bool TimerDHandler::clearTimerD(nta_outgoing_t* invite) {
        bool success = false;
        string callIdAndCSeq = combineCallIdAndCSeq(invite);
        mapCallIdAndCSeq2Invite::const_iterator it = m_mapCallIdAndCSeq2Invite.find(callIdAndCSeq);
        if (it != m_mapCallIdAndCSeq2Invite.end()) {
            DR_LOG(log_error) << "TimerDHandler::clearTimerD - app never sent ACK for successful uac INVITE"  ;
            m_mapCallIdAndCSeq2Invite.erase(it);
        }
        else {
            mapInvite2Ack::const_iterator it = m_mapInvite2Ack.find(invite);
            if (it != m_mapInvite2Ack.end()) {
                DR_LOG(log_info) << "TimerDHandler::clearTimerD - freeing ACK orq " << hex << (void *) it->second <<
                    " associated with invite orq " << invite << " for call-id/cseq " << callIdAndCSeq;
                nta_outgoing_destroy(it->second);
                m_mapInvite2Ack.erase(it);
                success = true;
            }
        }
        return success;
    }

    void SipDialogController::logRIP(bool bDetail) {
        DR_LOG(bDetail ? log_info : log_debug) << "RIP size:                                                        " <<
            m_mapOrq2RIP.size();
        if (bDetail) {
            for (const auto& pair : m_mapOrq2RIP) {
                nta_outgoing_t* orq = pair.first;
                std::shared_ptr<RIP> p = pair.second;
                DR_LOG(log_debug) << "    orq: " << std::hex << (void *) orq << " dialog id " << p->getDialogId() << " txn id " << p->getTransactionId();
            }
        }
    }

    // logging / metrics
    void SipDialogController::logStorageCount(bool bDetail)  {

        DR_LOG(bDetail ? log_info : log_debug) << "SipDiaSD_LoglogController storage counts"  ;
        DR_LOG(bDetail ? log_info : log_debug) << "----------------------------------"  ;
        IIP_Log(m_invitesInProgress, bDetail);
        SD_Log(m_dialogs, bDetail);

        std::lock_guard<std::mutex> lock(m_mutex) ;
        DR_LOG(bDetail ? log_info : log_debug) << "m_mapTransactionId2Irq size:                                     " << m_mapTransactionId2Irq.size()  ;
        DR_LOG(bDetail ? log_info : log_debug) << "number of outgoing transactions held for timerD:                 " << m_timerDHandler.countTimerD()  ;
        DR_LOG(bDetail ? log_info : log_debug) << "number of outgoing transactions waiting for ACK from app:        " << m_timerDHandler.countPending()  ;
        logRIP(bDetail);
        m_pTQM->logQueueSizes() ;

        // stats
        if (theOneAndOnlyController->getStatsCollector().enabled()) {

            size_t nUas = 0, nUac = 0;
            size_t total = SD_Size(m_dialogs, nUac, nUas);
            STATS_GAUGE_SET_NOCHECK(STATS_GAUGE_STABLE_DIALOGS, nUas, {{"type", "inbound"}})
            STATS_GAUGE_SET_NOCHECK(STATS_GAUGE_STABLE_DIALOGS, nUac, {{"type", "outbound"}})
        }
    }

}
