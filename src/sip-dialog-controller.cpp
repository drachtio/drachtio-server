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

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>


namespace drachtio {
    class SipDialogController ;
}

#define NTA_RELIABLE_MAGIC_T drachtio::SipDialogController

#include "controller.hpp"
#include "sofia-msg.hpp"
#include "sip-dialog-controller.hpp"

namespace {
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
        pController->getDialogController()->doSendCancelRequest( d ) ;
    }
    int uacLegCallback( nta_leg_magic_t* p, nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processRequestInsideDialog( leg, irq, sip) ;
    }
    int uasCancelOrAck( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) {
       drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processCancelOrAck( p, irq, sip) ;
    }
    int uasPrack( drachtio::SipDialogController *pController, nta_reliable_t *rel, nta_incoming_t *prack, sip_t const *sip) {
        return pController->processPrack( rel, prack, sip) ;
    }
   int response_to_request_outside_dialog( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {   
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processResponseOutsideDialog( request, sip ) ;
    } 
   int response_to_request_inside_dialog( nta_outgoing_magic_t* p, nta_outgoing_t* request, sip_t const* sip ) {   
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
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

	SipDialogController::SipDialogController( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone), 
        m_agent(pController->getAgent()), m_pClientController(pController->getClientController()) {

            assert(m_agent) ;
            assert(m_pClientController) ;

            m_my_contact = nta_agent_contact( m_agent ) ;
	}
	SipDialogController::~SipDialogController() {
	}
    int SipDialogController::sendRequestInsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid, boost::shared_ptr<SipDialog>& dlg ) {
       DR_LOG(log_debug) << "SipDialogController::sendSipRequestInsideDialog thread id: " << boost::this_thread::get_id() << endl ;

        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipRequestInsideDialog, 
            sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        string transactionId ;
        string dialogId = dlg->getDialogId() ;
        SipMessageData* msgData = new(place) SipMessageData( dialogId, transactionId, rid, pMsg ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }

        return true ;
    }

    void SipDialogController::doSendRequestInsideDialog( SipMessageData* pData ) {                
        boost::shared_ptr<JsonMsg> pMsg = pData->getMsg() ;
        ostringstream o ;
        string rid( pData->getRequestId() ) ;
        string dialogId( pData->getDialogId() ) ;
        boost::shared_ptr<SipDialog> dlg ;
        nta_outgoing_t* orq = NULL ;
 
        try {

            if( !findDialogById( dialogId, dlg ) ) {
                assert(false) ;
                throw std::runtime_error("unable to find dialog for dialog id provided") ;
            }

            nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
            if( !leg ) {
                assert( leg ) ;
                throw std::runtime_error("unable to find active leg for dialog") ;
            }

            DR_LOG(log_debug) << "SipDialogController::doSendSipRequestInsideDialog in thread " << boost::this_thread::get_id() << " with rid " << rid << endl ;

            const char *body=NULL, *method=NULL ;
            json_error_t err ;
            json_t* obj = NULL;
            if( json_unpack_ex( pMsg->value(), &err, 0, "{s:{s:{s:s,s?s,s:o}}}", "data","message","method", &method, "body",&body,"headers",&obj) ) {
                DR_LOG(log_error) << "SipDialogController::doSendSipRequestInsideDialog - failed parsing message: " << err.text << endl ;
                throw std::runtime_error(string("error parsing json message: ") + err.text) ;
            }
 
            if( !method ) {
                throw std::runtime_error("method is required") ;
            }

            sip_method_t mtype = methodType( method ) ;
            if( sip_method_unknown == mtype ) {
                throw std::runtime_error("unknown method") ;
            }

            if( obj && json_object_size( obj ) > 0 ) {
                tagi_t* tags = this->makeTags( obj ) ;

                orq = nta_outgoing_tcreate( leg, response_to_request_inside_dialog, (nta_outgoing_magic_t *) m_pController,
                                                            NULL,
                                                            mtype, method,
                                                            NULL,
                                                            TAG_IF(body, SIPTAG_PAYLOAD_STR(body)),
                                                            TAG_NEXT(tags),
                                                            TAG_END() ) ;
                deleteTags( tags ) ;
            }
            else {
                orq = nta_outgoing_tcreate( leg, response_to_request_inside_dialog, (nta_outgoing_magic_t *) m_pController,
                                                            NULL,
                                                            mtype, method,
                                                            NULL,
                                                            TAG_IF(body, SIPTAG_PAYLOAD_STR(body)),
                                                            TAG_END() ) ;
            }
           if( NULL == orq ) throw std::runtime_error("internal error attempting to create sip transaction") ;               
            
            msg_t* m = nta_outgoing_getrequest(orq) ;
            sip_t* sip = sip_object( m ) ;

            string transactionId ;
            generateUuid( transactionId ) ;

            boost::shared_ptr<RIP> p = boost::make_shared<RIP>( transactionId, rid, dialogId ) ;
            addRIP( orq, p ) ;

            SofiaMsg req( orq, sip ) ;
            json_t* json = json_pack_ex(&err, JSON_COMPACT, "{s:b,s:s,s:s,s:o}","success",true,"transactionId",transactionId.c_str(),
                "dialogId",dialogId.c_str(),"message",req.detach()) ;
            if( !json ) {
                string error = string("error packing message: ") + err.text ;
                DR_LOG(log_error) << "doSendRequestInsideDialog - " << error.c_str() << endl ;
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason",error.c_str()) ) ; 
                return ;
            }

             m_pController->getClientController()->sendResponseToClient( rid, json, transactionId ) ; 

            if( sip_method_bye == mtype ) {
                this->clearDialog( dialogId ) ;
            }

       } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << "SipDialogController::doSendSipRequestInsideDialog - Error " << err.what() << endl;
            json_t* data = json_pack("{s:b,s:s,s:s}","success",false,
                "dialogId",dialogId.c_str(),"reason",err.what()) ;
            m_pController->getClientController()->sendResponseToClient( rid, data ) ; 
       }                       

        /* we must explicitly delete an object allocated with placement new */
        pData->~SipMessageData() ;
    }
   
    bool SipDialogController::sendCancelRequest( boost::shared_ptr<JsonMsg> pMsg, const string& rid ) {
        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipCancelRequest, 
            sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        const char* transactionId=NULL ;
        json_error_t err ;
        if( 0 > json_unpack_ex( pMsg->value(), &err, 0, "{s:{s:s}}", "data","transactionId",&transactionId) ) {
            DR_LOG(log_error) << "SipDialogController::sendCancelRequest - error parsing message: " << err.text << endl ;
            return false ;
        }
        SipMessageData* msgData = new(place) SipMessageData( transactionId, rid, pMsg ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }
        return true ;
    }
    void SipDialogController::doSendCancelRequest( SipMessageData* pData ) {
        string rid( pData->getRequestId() ) ;
        string transactionId( pData->getTransactionId() ) ;
        boost::shared_ptr<IIP> iip ;
        json_t* json ;

        if( findIIPByTransactionId( transactionId, iip ) ) {
            int rc = nta_outgoing_cancel( iip->orq() ) ;
            json = json_pack("{s:b}","success",true) ;
        }
        else {
           DR_LOG(log_error) << "SipDialogController::doSendCancelRequest - unknown transaction id " << transactionId << endl;
            json = json_pack("{s:b,s:s}","success",false,"reason","request could not be found for specified transaction id") ;
        }
        m_pController->getClientController()->sendResponseToClient( rid, json ) ; 

        pData->~SipMessageData() ;
   }

    bool SipDialogController::sendRequestOutsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid ) {
        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipRequest, 
            sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        string transactionId ;
        SipMessageData* msgData = new(place) SipMessageData( transactionId, rid, pMsg ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }
        return true ;
    }
    void SipDialogController::doSendRequestOutsideDialog( SipMessageData* pData ) {
        //boost::lock_guard<boost::mutex> lock(m_mutex) ;
        nta_leg_t* leg = NULL ;
        nta_outgoing_t* orq = NULL ;
        string rid( pData->getRequestId() ) ;
        boost::shared_ptr<JsonMsg> pMsg = pData->getMsg() ;
        ostringstream o ;

        const char *from = NULL, *to = NULL, *request_uri = NULL, *method = NULL, *body = NULL, *content_type = NULL ;

        try {
            json_error_t error ;
            if( 0 > json_unpack_ex(pMsg->value(), &error, 0, "{s:{s:{s:s,s:s,s?s,s:{s?s,s?s,s?s}}}}",
                "data","message","method",&method,"request_uri",&request_uri,"body",&body,
                "headers","content-type",&content_type,"to",&to,"from",&from) ) {

                DR_LOG(log_error) << "SipDialogController::doSendRequestOutsideDialog - failed parsing message: " << error.text <<  endl ;
                return  ;
            }

            if( !method || 0 == strlen(method)) {
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason","method is required") ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - method is required") ;
            }
     
            sip_method_t mtype = methodType( method ) ;
            if( sip_method_unknown == mtype ) {
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason","unknown method") ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - unknown method") ;
            }

            if( !request_uri || 0 == strlen(request_uri))  {
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason","request_uri is required") ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - request_uri is required") ;

            }

            /* if body was provided, content-type is required */
            if( body && (!content_type || 0 == strlen(content_type) ) ) {
                if( body == strstr( body, "v=0") ) {
                    content_type = "application/sdp" ;
                    DR_LOG(log_debug) << "SipDialogController::doSendSipRequestOutsideDialog - automatically detecting content-type as application/sdp" << endl ;
                }
                else {
                    m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason","missing content-type header") ) ; 
                    throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - missing content-type") ;                   
                }
             }

            string strRequestUri = request_uri ;
            normalizeSipUri( strRequestUri ) ;
            if( isLocalSipUri( strRequestUri ) ) {
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason","request_uri loop detected") ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - can not send request to myself") ;
            }

            /* if we got a from header, replace the hostport with the correct one */
            string myHostport ;
            string strFrom ;
            m_pController->getMyHostport( myHostport ) ;
            if( from ) {
                strFrom = from ;
                if( !replaceHostInUri( strFrom, myHostport ) ) {
                    throw std::runtime_error(string("SipDialogController::doSendSipRequestOutsideDialog - invalid from value provided by client: ") + from ) ;
                }
            }
            else {
                /* set a default From, since none provided */
                strFrom = "sip:" ;
                strFrom.append( myHostport ) ;
            }

            /* default To header to the request uri, if no To provided */
            if( !to ) to = request_uri ;

            // TODO: set other headers where a user value only may be supplied, e.g. P-Asserted-Identity, P-Charge-Info

            leg = nta_leg_tcreate( m_pController->getAgent(),
                uacLegCallback, (nta_leg_magic_t *) m_pController,
                SIPTAG_FROM_STR(strFrom.c_str()),
                SIPTAG_TO_STR(to),
                TAG_END() );

            if( !leg ) {
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason","internal error attempting to create sip leg") ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - Error creating leg for uac invite") ;
            }
            nta_leg_tag( leg, NULL ) ;

            json_t* obj ;
            if( 0 > json_unpack_ex(pMsg->value(), &error, 0, "{s:{s:{s:o}}}","data","message", "headers", &obj) ) {
                string err = string("error parsing message: ") + error.text ;
                DR_LOG(log_error) << "SipDialogController::doSendRequestOutsideDialog - " << err << endl ;
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason",err.c_str()) ) ; 
                return  ;
            }
            //if( json_object_size(obj) > 0 ) {

                tagi_t* tags = this->makeTags( obj ) ;

                orq = nta_outgoing_tcreate( leg, response_to_request_outside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    NULL, mtype, method
                    ,URL_STRING_MAKE(strRequestUri.c_str())
                    ,TAG_IF( 0 == strcmp(method,"INVITE"), SIPTAG_CONTACT( m_my_contact ) )
                    ,TAG_IF( body, SIPTAG_PAYLOAD_STR(body))
                    ,TAG_NEXT(tags) ) ;

                 deleteTags( tags ) ;
            /*
            }
            
            else {
               orq = nta_outgoing_tcreate( leg, response_to_request_outside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    NULL, mtype, method
                    ,URL_STRING_MAKE(strRequestUri.c_str())
                    ,TAG_IF( 0 == strcmp(method,"INVITE"), SIPTAG_CONTACT( m_my_contact ))
                    ,TAG_IF( body, SIPTAG_PAYLOAD_STR(body))
                    ,TAG_END() ) ;
            }
            */
            if( NULL == orq ) {
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason","internal error attempting to create sip transaction") )  ;  
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - Error creating sip transaction for uac invite") ;               
            }
            string transactionId ;
            generateUuid( transactionId ) ;

            msg_t* m = nta_outgoing_getrequest(orq) ;
            sip_t* sip = sip_object( m ) ;

            boost::shared_ptr<SipDialog> dlg = boost::make_shared<SipDialog>( leg, orq, sip ) ;
            dlg->setTransactionId( transactionId ) ;
            addOutgoingInviteTransaction( leg, orq, sip, transactionId, dlg ) ;

            SofiaMsg req( orq, sip ) ;
            json_t* json = json_pack_ex(&error, JSON_COMPACT, "{s:b,s:s,s:o}", 
                    "success", true, "transactionId",transactionId.c_str(),"message",req.detach() ) ;
            if( !json ) {
                string err = string("error packing message: ") + error.text ;
                DR_LOG(log_error) << "doSendRequestOutsideDialog - " << err.c_str() << endl ;
                m_pController->getClientController()->sendResponseToClient( rid, json_pack("{s:b,s:s}", 
                    "success", false, "reason",err.c_str()) ) ; 
              return ;
            }

            m_pController->getClientController()->sendResponseToClient( rid, json, transactionId ) ; 

        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << err.what() << endl;
        }                       

        /* we must explicitly call the destructor of an object allocated with placement new */

        pData->~SipMessageData() ;
    }

    int SipDialogController::processResponseOutsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
 
        boost::shared_ptr<IIP> iip ;
        if( !findIIPByOrq( orq, iip ) ) {
            DR_LOG(log_error) << "SipDialogController::processResponse - unable to match response with callid " << sip->sip_call_id->i_id << endl ;
            //TODO: do I need to destroy this transaction?
            return -1 ; //TODO: check meaning of return value           
        }
        m_pController->getClientController()->route_response_inside_transaction( orq, sip, iip->transactionId() ) ;

        bool bClear = false ;
        iip->dlg()->setSipStatus( sip->sip_status->st_status ) ;

        /* send PRACK if this is a reliable response */
        if( sip->sip_rseq ) {
            nta_leg_t* leg = iip->leg() ;
            nta_leg_rtag( leg, sip->sip_to->a_tag) ;
            nta_leg_client_reroute( leg, sip->sip_record_route, sip->sip_contact, true );

            ostringstream rack ;
            rack << sip->sip_rseq->rs_response << " " << sip->sip_cseq->cs_seq << " " << sip->sip_cseq->cs_method_name ;

            nta_outgoing_t* prack = nta_outgoing_prack( leg, orq, NULL, NULL, NULL, NULL, 
                SIPTAG_RACK_STR( rack.str().c_str() ),
                TAG_END() ) ;

            nta_outgoing_destroy( prack ) ;
        }

        if( sip->sip_status->st_status >= 200 ) {

            bClear = true ;
            if( sip->sip_cseq->cs_method ==  sip_method_invite ) {

                if( sip->sip_payload ) {
                    iip->dlg()->setRemoteSdp( sip->sip_payload->pl_data, sip->sip_payload->pl_len ) ;
                }
                
                /* we need to send ACK in the case of success response (stack handles it for us in non-success) */
                if( 200 == sip->sip_status->st_status ) {

                    sip_session_expires_t* se = sip_session_expires(sip) ;
                    if( se ) {
                        iip->dlg()->setSessionTimer( std::max((unsigned long) 90, se->x_delta), !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? SipDialog::we_are_refresher : SipDialog::they_are_refresher ) ;
                    }
 
                   // TODO: don't send the ACK if the INVITE had no body: the ACK must have a body in that case, and the client must supply it
                    nta_leg_t* leg = iip->leg() ;
                    nta_leg_rtag( leg, sip->sip_to->a_tag) ;
                    nta_leg_client_reroute( leg, sip->sip_record_route, sip->sip_contact, false );

                    nta_outgoing_t* ack_request = nta_outgoing_tcreate(leg, NULL, NULL, NULL,
                                                                   SIP_METHOD_ACK,
                                                                   (url_string_t*) sip->sip_contact->m_url ,
                                                                   TAG_END());
                    nta_outgoing_destroy( ack_request ) ;

                }
              
                //TODO: handle redirection (should this be client specified?)

                //NB: the above cases could cause us to reset bClear back to false
   
            }
        }
        if( bClear ) {
            boost::shared_ptr<SipDialog> dlg = this->clearIIP( iip->leg() ) ;
            if(  sip->sip_cseq->cs_method ==  sip_method_invite && 200 == sip->sip_status->st_status )  m_pController->notifyDialogConstructionComplete( dlg ) ;
        } 


        return 0 ;
    }

    void SipDialogController::respondToSipRequest( const string& transactionId, boost::shared_ptr<JsonMsg> pMsg  ) {
        DR_LOG(log_debug) << "respondToSipRequest thread id: " << boost::this_thread::get_id() << endl ;

        json_t* hdrs=NULL ;
        json_error_t err ;

        if( 0 > json_unpack_ex( pMsg->value(), &err, 0, "{s:{s:{s:o}}}", "data", "message", "headers", &hdrs) ) {
            DR_LOG(log_debug) << "SipDialogController::respondToSipRequest - failed to parse sip headers: " << err.text  << endl ;
        }
 
        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneRespondToSipRequest, 
        	sizeof( SipDialogController::SipMessageData ) );
        if( rv < 0 ) {
            return  ;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        string rid ;
        SipMessageData* msgData = new(place) SipMessageData( transactionId, rid, pMsg ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  ;
        }
    }
    void SipDialogController::doRespondToSipRequest( SipMessageData* pData ) {
        string transactionId( pData->getTransactionId() ) ;
        boost::shared_ptr<JsonMsg> pMsg = pData->getMsg() ;

        int code ;
        const char* status = NULL ;
        json_error_t err ;
        json_t* obj ;
        json_t* json = pMsg->value() ;
        if( 0 > json_unpack_ex( json, &err, 0, "{s:{s:i,s?s,s:{s:o}}}", "data","code",&code,"status",&status,"message","headers",&obj) ) {
            DR_LOG(log_error) << "SipDialogController::doRespondToSipRequest - failed unpacking json message: " << err.text << endl ;
            return ;
        }

        DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest - code: " << code << " number of tags: " << json_object_size(obj) << endl ;
        tagi_t* tags = this->makeTags( obj ) ;

        nta_incoming_t* irq = NULL ;
        int rc = -1 ;

        boost::shared_ptr<IIP> iip ;

        /* search for invites in progress first, then requests within a dialog (could check to see if this is an INVITE here) */
        if( findIIPByTransactionId( transactionId, iip ) ) {
            nta_leg_t* leg = iip->leg() ;
            irq = iip->irq() ;
            boost::shared_ptr<SipDialog> dlg = iip->dlg() ;

            dlg->setSipStatus( code ) ;

            /* if the client included Require: 100rel on a provisional, send it reliably */
            bool bReliable = false ;
            if( code > 100 && code < 200 ) {
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
             if( searchForHeader( tags, siptag_payload_str, strLocalSdp ) ) {
                dlg->setLocalSdp( strLocalSdp.c_str() ) ;
                string strLocalContentType ;
                if( searchForHeader( tags, siptag_content_type_str, strLocalContentType ) ) {
                    dlg->setLocalContentType( strLocalContentType ) ;
                }
             }

             /* set session timer if required */
             if( 200 == code ) {
                string strSessionExpires ;
                if( searchForHeader( tags, siptag_session_expires_str, strSessionExpires ) ) {
                    sip_session_expires_t* se = sip_session_expires_make(m_pController->getHome(), strSessionExpires.c_str() );

                    dlg->setSessionTimer( std::max((unsigned long) 90, se->x_delta), !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? SipDialog::they_are_refresher : SipDialog::we_are_refresher ) ;
                    su_free( m_pController->getHome(), se ) ;
                }
             }

            /* iterate through data.opts.headers, adding headers to the response */
            if( bReliable ) {
                DR_LOG(log_debug) << "Sending " << code << " response reliably" << endl ;
                nta_reliable_t* rel = nta_reliable_treply( irq, uasPrack, this, code, status
                    ,SIPTAG_CONTACT(m_pController->getMyContact())
                    ,TAG_NEXT(tags)
                    ,TAG_END() ) ; 
                assert( rel ) ;
                iip->setReliable( rel ) ;
                addReliable( rel, iip ) ;
            }
            else {
                DR_LOG(log_debug) << "Sending " << code << " response (not reliably)" << endl ;
                rc = nta_incoming_treply( irq, code, status
                    ,TAG_IF( code >= 200 && code < 300, SIPTAG_CONTACT(m_pController->getMyContact()))
                    ,TAG_NEXT(tags)
                    ,TAG_END() ) ; 
                assert(0 == rc) ;
            }
        }
        else {
            nta_incoming_t* irq = findAndRemoveTransactionIdForIncomingRequest( transactionId ) ;
            if( irq ) {
                //TODO: if we have already generated a response (BYE, INFO with msml) then don't try again
                rc = nta_incoming_treply( irq, code, status
                    ,TAG_NEXT(tags), TAG_END() ) ;                                 
                assert( 0 == rc ) ; 
                DR_LOG(log_debug) << "SipDialogController::doRespondToSipRequest destroying irq " << irq << endl ;
                nta_incoming_destroy(irq) ;                           
            }
            else {
                 DR_LOG(log_debug) << "silently discarding sip response from client; was probably a 200 OK to BYE; transactionId: " << transactionId << endl ;
            }
        }

        if( tags ) deleteTags( tags );

        /* we must explicitly delete an object allocated with placement new */
        pData->~SipMessageData() ;
    }

    int SipDialogController::processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "SipDialogController::processRequestInsideDialog: " << sip->sip_request->rq_method_name << " irq " << irq << endl ;
        int rc = 0 ;
        string transactionId ;
        generateUuid( transactionId ) ;

        switch (sip->sip_request->rq_method ) {
            case sip_method_ack:
            {
                /* ack to 200 OK, now we are all done */
                boost::shared_ptr<IIP> iip ;
                if( !findIIPByLeg( leg, iip ) ) {
                    DR_LOG(log_debug) << "SipDialogController::processRequestInsideDialog - unable to find IIP for ACK, must be for reINVITE" << endl ;
                }
                else {
                    string transactionId = iip->transactionId() ;

                    boost::shared_ptr<SipDialog> dlg = this->clearIIP( leg ) ;
                    m_pController->notifyDialogConstructionComplete( dlg ) ;
                    m_pController->getClientController()->route_ack_request_inside_dialog( irq, sip, transactionId, dlg->getTransactionId(), dlg->getDialogId() ) ;
                }
                nta_incoming_destroy(irq) ;
                break ;
            }
            case sip_method_bye:
            {
                boost::shared_ptr<SipDialog> dlg ;
                if( !this->findDialogByLeg( leg, dlg ) ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg" << endl ;
                    assert(0) ;
                    return 200 ;
                }
                m_pController->getClientController()->route_request_inside_dialog( irq, sip, transactionId, dlg->getDialogId() ) ;
 
                this->clearDialog( leg ) ;
                nta_incoming_destroy( irq ) ;

                rc = 200 ; //we generate 200 OK to BYE in all cases, any client responses will be discarded
                break ;
            }
            case sip_method_invite: {
                boost::shared_ptr<SipDialog> dlg ;
                if( !this->findDialogByLeg( leg, dlg ) ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg" << endl ;
                    rc = 481 ;
                    assert(0) ;
                }

                /* TODO: reject if session timer requested is less than minSE seconds */
                if( sip->sip_session_expires && sip->sip_session_expires->x_delta < dlg->getMinSE() ) {
                    ostringstream o ;
                    o << dlg->getMinSE() ;
                    nta_incoming_treply( irq, SIP_422_SESSION_TIMER_TOO_SMALL, 
                        SIPTAG_MIN_SE_STR(o.str().c_str()),
                        TAG_END() ) ;  
                    return 0 ;             
                }

                /* check to see if this is a refreshing re-INVITE; i.e., same SDP as before */
                if( dlg->hasSessionTimer() ) dlg->cancelSessionTimer() ;

                bool bRefreshing = false ;
                if( sip->sip_payload ) {
                    string strSdp( sip->sip_payload->pl_data, sip->sip_payload->pl_len ) ;
                    if( 0 == strSdp.compare( dlg->getRemoteEndpoint().m_strSdp ) ) {
                      bRefreshing = true ;
                    }
                    else {
                        DR_LOG(log_debug) << "sdp in invite " << strSdp << endl << "previous sdp " << dlg->getRemoteEndpoint().m_strSdp << endl ;
                    }
                }                
                DR_LOG(log_debug) << "SipDialogController::processRequestInsideDialog: received " << 
                    (bRefreshing ? "refreshing " : "") << "re-INVITE" << endl ;


                int result = nta_incoming_treply( irq, SIP_200_OK
                    ,SIPTAG_CONTACT(m_my_contact)
                    ,SIPTAG_PAYLOAD_STR(dlg->getLocalEndpoint().m_strSdp.c_str())
                    ,SIPTAG_CONTENT_TYPE_STR(dlg->getLocalEndpoint().m_strContentType.c_str())
                    ,TAG_IF(sip->sip_session_expires, SIPTAG_SESSION_EXPIRES(sip->sip_session_expires))
                    ,TAG_END() ) ; 
                assert( 0 == result ) ;

               if( sip->sip_session_expires ) {
                    dlg->setSessionTimer( sip->sip_session_expires->x_delta, 
                        !sip->sip_session_expires->x_refresher || 0 == strcmp( sip->sip_session_expires->x_refresher, "uac") ? SipDialog::they_are_refresher : SipDialog::we_are_refresher) ;
                }
                
                //m_pClientController->route_event_inside_dialog( bRefreshing ? "{\"eventName\": \"refresh\"}" :  "{\"eventName\": \"modify\"}"
                //    ,dlg->getTransactionId(), dlg->getDialogId() ) ;   
                nta_incoming_destroy( irq ) ;
                break ;             
               
            }
            default:
            {
                if( sip_method_info == sip->sip_request->rq_method && 0 == strcmp( sip->sip_content_type->c_type, "application/msml+xml") ) {
                    /* msml INFO messages are notifications of events, and there is no reason (that I know of) to send anything but a 200 in response*/
                    rc = 200 ;
                }
                boost::shared_ptr<SipDialog> dlg ;
                if( !this->findDialogByLeg( leg, dlg ) ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg" << endl ;
                    return 481 ;
                    assert(0) ;
                }

                //TODO: here is the problem: we send this to client and don't save the irq - it never gets cleared
                m_pController->getClientController()->route_request_inside_dialog( irq, sip, transactionId, dlg->getDialogId() ) ;

            }
        }
        return rc ;
    }
    int SipDialogController::processResponseInsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
        DR_LOG(log_debug) << "SipDialogController::processResponseInsideDialog: " << endl ;
    	ostringstream o ;
        boost::shared_ptr<RIP> rip  ;

        if( findRIPByOrq( orq, rip ) ) {
            m_pController->getClientController()->route_response_inside_transaction( orq, sip, rip->transactionId(), rip->dialogId() ) ;
            clearRIP( orq ) ;          
        }
        nta_outgoing_destroy( orq ) ;
  
		return 0 ;
    }
    int SipDialogController::processResponseToRefreshingReinvite( nta_outgoing_t* orq, sip_t const* sip ) {
        DR_LOG(log_debug) << "SipDialogController::processResponseToRefreshingReinvite: " << endl ;
        ostringstream o ;
        boost::shared_ptr<RIP> rip  ;

        nta_leg_t* leg = nta_leg_by_call_id(m_pController->getAgent(), sip->sip_call_id->i_id);
        assert(leg) ;
        boost::shared_ptr<SipDialog> dlg ;
        if( !findDialogByLeg( leg, dlg ) ) {
            assert(0) ;
        }
        if( findRIPByOrq( orq, rip ) ) {
            clearRIP( orq ) ;          

            nta_outgoing_t* ack_request = nta_outgoing_tcreate(leg, NULL, NULL, NULL,
                   SIP_METHOD_ACK,
                   (url_string_t*) sip->sip_contact->m_url ,
                   TAG_END());

            nta_outgoing_destroy( ack_request ) ;

            if( sip->sip_status->st_status != 200 ) {
                //TODO: notify client that call has failed, send BYE
            }
            else {
                /* reset session expires timer, if provided */
                sip_session_expires_t* se = sip_session_expires(sip) ;
                if( se ) {                
                    //TODO: if session-expires value is less than min-se ACK and then BYE with Reason header    
                    dlg->setSessionTimer( se->x_delta, !se->x_refresher || 0 == strcmp( se->x_refresher, "uac") ? SipDialog::we_are_refresher : SipDialog::they_are_refresher ) ;
                }
             }
        }
        nta_outgoing_destroy( orq ) ;
        
        return 0 ;
        
    }

    int SipDialogController::processCancelOrAck( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) {
        if( !sip ) {
            DR_LOG(log_warning) << "SipDialogController::processCancelOrAck - received null sip message, irq is " << irq << endl ;
            return -1 ;
        }
        DR_LOG(log_debug) << "SipDialogController::processCancelOrAck: " << sip->sip_request->rq_method_name << endl ;
        if( !sip ) {
            DR_LOG(log_debug) << "SipDialogController::processCancel called with null sip pointer" << endl ;
            return -1 ;
        }

        if( sip->sip_request->rq_method == sip_method_cancel ) {
            boost::shared_ptr<IIP> iip ;
            if( !findIIPByIrq( irq, iip ) ) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for CANCEL with call-id " << sip->sip_call_id->i_id << endl ;
                return 0 ;
            }
            boost::shared_ptr<SipDialog> dlg = iip->dlg() ;

            if( !dlg ) {
                DR_LOG(log_error) << "No dialog exists for invite-in-progress for CANCEL with call-id " << sip->sip_call_id->i_id << endl ;
                return 0 ;
            }

            DR_LOG(log_debug) << "Received CANCEL for call-id " << sip->sip_call_id->i_id << ", sending to client" << endl ;
            m_pController->getClientController()->route_request_inside_invite( irq, sip, dlg->getTransactionId() ) ;
            this->clearIIP( iip->leg() ) ;
        }
        else if( sip->sip_request->rq_method == sip_method_ack ) {
            boost::shared_ptr<IIP> iip ;
            if( !findIIPByIrq( irq, iip ) ) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for ACK with call-id " << sip->sip_call_id->i_id << endl ;
                return 0 ;
            }
            boost::shared_ptr<SipDialog> dlg = this->clearIIP( iip->leg() ) ;
             DR_LOG(log_debug) << "Most recent sip response on this dialog was " << dlg->getSipStatus() << endl ;
            //NB: when we get a CANCEL sofia sends the 487 response to the INVITE itself, so our latest sip status will be a provisional
            //not sure that we need to do anything particular about that however....though it we write cdrs we would want to capture the 487 final response
        }
        else {
            DR_LOG(log_debug) << "Received " << sip->sip_request->rq_method_name << " for call-id " << sip->sip_call_id->i_id << ", discarding" << endl ;
        }
        return 0 ;
    }
    int SipDialogController::processPrack( nta_reliable_t *rel, nta_incoming_t *prack, sip_t const *sip) {
        DR_LOG(log_debug) << "SipDialogController::processPrack: " << endl ;
        boost::shared_ptr<IIP> iip ;
        if( findIIPByReliable( rel, iip ) ) {
            boost::shared_ptr<SipDialog> dlg = iip->dlg() ;
            assert( dlg ) ;
            m_pClientController->addDialogForTransaction( dlg->getTransactionId(), dlg->getDialogId() ) ;      
            m_pController->getClientController()->route_request_inside_invite( prack, sip, iip->transactionId(), dlg->getDialogId() ) ;
            iip->destroyReliable() ;
            nta_incoming_destroy( prack ) ;
        }
        else {
            assert(0) ;
        }
        return 200 ;
    }
    void SipDialogController::notifyRefreshDialog( boost::shared_ptr<SipDialog> dlg ) {
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
                                            SIPTAG_CONTACT( m_my_contact ),
                                            SIPTAG_CONTENT_TYPE_STR(strContentType.c_str()),
                                            SIPTAG_PAYLOAD_STR(strSdp.c_str()),
                                            TAG_END() ) ;
            
            string transactionId ;
            generateUuid( transactionId ) ;

            boost::shared_ptr<RIP> p = boost::make_shared<RIP>( transactionId, "undefined" ) ; //because no client instructed us to send this
            addRIP( orq, p ) ;

            m_pClientController->route_event_inside_dialog( "{\"eventName\": \"refresh\"}",dlg->getTransactionId(), dlg->getDialogId() ) ;
        }
    }
    void SipDialogController::notifyTerminateStaleDialog( boost::shared_ptr<SipDialog> dlg ) {
        nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
        if( leg ) {
            nta_outgoing_t* orq = nta_outgoing_tcreate( leg, NULL, NULL,
                                            NULL,
                                            SIP_METHOD_BYE,
                                            NULL,
                                            SIPTAG_REASON_STR("SIP ;cause=200 ;text=\"Session timer expired\""),
                                            TAG_END() ) ;
            nta_outgoing_destroy(orq) ;
            m_pClientController->route_event_inside_dialog( "{\"eventName\": \"terminate\",\"eventData\":\"session expired\"}",dlg->getTransactionId(), dlg->getDialogId() ) ;
        }
        clearDialog( dlg->getDialogId() ) ;
    }
    void SipDialogController::deleteTags( tagi_t* tags ) {
        int i = 0 ;
        while( tags[i].t_tag != tag_null ) {
            if( tags[i].t_value ) {
                char *p = (char *) tags[i].t_value ;
                delete [] p ;
            }
             i++ ;
        }       
        delete [] tags ; 
    }
 	tagi_t* SipDialogController::makeTags( json_t*  hdrs ) {
        int nHdrs = json_object_size( hdrs ) ;
        tagi_t *tags = new tagi_t[nHdrs+1] ;
        int i = 0; 
        const char* k ;
        json_t* v ;

        DR_LOG(log_debug) << "SipDialogController::makeTags - " << nHdrs << " tags were provided" << endl ;

        string myHostport ;
        m_pController->getMyHostport( myHostport ) ;

        json_object_foreach(hdrs, k, v) {

            /* default to skip, as this may not be a header we are allowed to set, or value might not be provided correctly (as a string) */
            tags[i].t_tag = tag_skip ;
            tags[i].t_value = (tag_value_t) 0 ;                     

            string strKey( k ) ;

            string hdr = boost::to_lower_copy( boost::replace_all_copy( strKey, "-", "_" ) );

            /* check to make sure this isn't a header that is not client-editable */
            if( isImmutableHdr( hdr ) ) {
                if( 0 == hdr.compare("from") || 0 == hdr.compare("to") ) DR_LOG(log_warning) << "SipDialogController::makeTags - to or from header " << endl ;
                else DR_LOG(log_warning) << "SipDialogController::makeTags - client provided a value for header '" << strKey << "' which is not modfiable by client" << endl;
                i++ ;
                continue ;                       
            }

            try {
                string value = json_string_value( v ) ;

                tag_type_t tt ;
                if( getTagTypeForHdr( hdr, tt ) ) {
                	/* well-known header */

                    if( 0 == hdr.compare("p_asserted_identity") || 
                        0 == hdr.compare("p_preferred_identity") || 
                        string::npos != value.find("localhost") ) {

                        replaceHostInUri( value, myHostport ) ;
                    }
                    int len = value.length() ;
                    char *p = new char[len+1] ;
                    memset(p, '\0', len+1) ;
                    strncpy( p, value.c_str(), len ) ;
                    tags[i].t_tag = tt;
                    tags[i].t_value = (tag_value_t) p ;
                    DR_LOG(log_debug) << "SipDialogController::makeTags - Adding well-known header '" << strKey << "' with value '" << p << "'" << endl ;
                }
                else {
                    /* custom header */
                    if( string::npos != strKey.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890-_") ) {
                       DR_LOG(log_error) << "SipDialogController::makeTags - client supplied invalid custom header name '" << strKey << "'" << endl;
                       i++ ;
                       continue ;
                    }
                    else if( string::npos != value.find("\r\n") ) {
                        DR_LOG(log_error) << "SipDialogController::makeTags - client supplied invalid custom header value (contains CR or LF) for header '" << strKey << "'" << endl;
                        i++ ;
                        continue ;
                    }
                    int nLength = strKey.length() + 2 + value.length() + 1;
                    char *p = new char[nLength] ;
                    memset(p, '\0', nLength) ;
                    strcpy( p, k ) ;
                    strcat( p, ": ") ;
                    if(  string::npos != value.find("localhost") ) {
                        replaceHostInUri( value, myHostport ) ;                       
                    }
                    strcat( p, value.c_str() ) ;

                    tags[i].t_tag = siptag_unknown_str ;
                    tags[i].t_value = (tag_value_t) p ;
                    DR_LOG(log_debug) << "Adding custom header '" << strKey << "' with value '" << value << "'" << endl ;
                }
                i++ ;
            } catch( std::runtime_error& err ) {
                DR_LOG(log_error) << "SipDialogController::makeTags - Error attempting to read string value for header " << hdr << ": " << err.what() << endl;
            }                       
        }

        tags[nHdrs].t_tag = tag_null ;
        tags[nHdrs].t_value = (tag_value_t) 0 ;       

        return tags ;	//NB: caller responsible to delete after use to free memory      
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
    void SipDialogController::logStorageCount(void)  {
        boost::lock_guard<boost::mutex> lock(m_mutex) ;

        DR_LOG(log_debug) << "m_mapIrq2IIP size " << m_mapIrq2IIP.size() << endl ;
        DR_LOG(log_debug) << "m_mapOrq2IIP size " << m_mapOrq2IIP.size() << endl ;
        DR_LOG(log_debug) << "m_mapTransactionId2IIP size " << m_mapTransactionId2IIP.size() << endl ;
        DR_LOG(log_debug) << "m_mapLeg2Dialog size " << m_mapLeg2Dialog.size() << endl ;
        DR_LOG(log_debug) << "m_mapId2Dialog size " << m_mapId2Dialog.size() << endl ;
        DR_LOG(log_debug) << "m_mapOrq2RIP size " << m_mapOrq2RIP.size() << endl ;
    }

}
