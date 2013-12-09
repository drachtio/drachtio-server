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
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <sofia-sip/nta.h>

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
    int uacLegCallback( nta_leg_magic_t* p, nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processRequestInsideDialog( leg, irq, sip) ;
    }
    int uasCancel( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        return pController->getDialogController()->processCancel( irq, sip) ;
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
}


namespace drachtio {

	SipDialogController::SipDialogController( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone) {
	}
	SipDialogController::~SipDialogController() {
	}

    bool SipDialogController::sendRequestOutsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid ) {
        DR_LOG(log_debug) << "sendSipRequest thread id: " << boost::this_thread::get_id() << endl ;

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
        nta_leg_t* leg = NULL ;
        nta_outgoing_t* orq = NULL ;
        string rid( pData->getRequestId() ) ;
        boost::shared_ptr<JsonMsg> pMsg = pData->getMsg() ;
        ostringstream o ;

        DR_LOG(log_debug) << "doSendSipRequestOutsideDialog in thread " << boost::this_thread::get_id() << " with rid " << rid << endl ;

        string strFrom, strTo, strRequestUri, strMethod, strBody, strContentType ;
        json_spirit::mObject obj ;

        try {

            if( !pMsg->get<string>("data.request_uri", strRequestUri ) )  {
                o << "{\"success\": false, \"reason\": \"request_uri is required\"}" ;
                m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - request_uri is required") ;

            }
            if( !pMsg->get<string>("data.method", strMethod) ) {
                o << "{\"success\": false, \"reason\": \"method is required\"}" ;
                m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - method is required") ;
            }
     
            sip_method_t mtype = methodType( strMethod ) ;
            if( sip_method_unknown == mtype ) {
                o << "{\"success\": false, \"reason\": \"unknown method\"}" ;
                m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - unknown method") ;
            }

            /* if body was provided, content-type is required */
            if( pMsg->get<string>("data.body", strBody ) ) {
                if( !pMsg->get<string>("data.headers.content-type", strContentType) ) {
                    o << "{\"success\": false, \"reason\": \"missing content-type header\"}" ;
                    m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
                    throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - missing content-type") ;
                }
            }

     
            normalizeSipUri( strRequestUri ) ;

            if( isLocalSipUri( strRequestUri ) ) {
                o << "{\"success\": false, \"reason\": \"request_uri loop detected\"}" ;
                m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - can not send request to myself") ;
            }

            /* if we got a from header, replace the hostport with the correct one */
            string myHostport ;
            m_pController->getMyHostport( myHostport ) ;
            if( pMsg->get<string>("data.headers.from", strFrom) ) {
                replaceHostInUri( strFrom, myHostport ) ; 
            }
            else {
                /* set a default From, since none provided */
                strFrom = "sip:" ;
                strFrom.append( myHostport ) ;
            }

            /* default To header to the request uri, if no To provided */
            strTo = pMsg->get_or_default<string>("data.headers.to", strRequestUri.c_str() ) ;

            leg = nta_leg_tcreate( m_pController->getAgent(),
                uacLegCallback, (nta_leg_magic_t *) m_pController,
                SIPTAG_FROM_STR(strFrom.c_str()),
                SIPTAG_TO_STR(strTo.c_str()),
                TAG_END() );

            if( !leg ) {
                o << "{\"success\": false, \"reason\": \"internal error attempting to create sip leg\"}" ;
                m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
                throw std::runtime_error("SipDialogController::doSendSipRequestOutsideDialog - Error creating leg for uac invite") ;
            }
            nta_leg_tag( leg, NULL ) ;

            if( pMsg->get<json_spirit::mObject>("data.headers", obj) ) {

                tagi_t* tags = this->makeTags( obj ) ;

                orq = nta_outgoing_tcreate( leg, response_to_request_outside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    NULL, mtype, strMethod.c_str()
                    ,URL_STRING_MAKE(strRequestUri.c_str())
                    ,TAG_IF( 0 == strMethod.compare("INVITE"), SIPTAG_CONTACT( m_pController->getMyContact()))
                    ,TAG_IF( !strBody.empty(), SIPTAG_PAYLOAD_STR(strBody.c_str()))
                    ,TAG_NEXT(tags) ) ;

                 delete[] tags ;
            }
            else {
               orq = nta_outgoing_tcreate( leg, response_to_request_outside_dialog, (nta_outgoing_magic_t*) m_pController, 
                    NULL, mtype, strMethod.c_str()
                    ,URL_STRING_MAKE(strRequestUri.c_str())
                    ,TAG_IF( 0 == strMethod.compare("INVITE"), SIPTAG_CONTACT( m_pController->getMyContact()))
                    ,TAG_IF( !strBody.empty(), SIPTAG_PAYLOAD_STR(strBody.c_str()))
                    ,TAG_END() ) ;
            }
            if( NULL == orq ) {
                o << "{\"success\": false, \"reason\": \"internal error attempting to create sip transaction\"}" ;
                m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
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
            o << "{\"success\": true, \"transactionId\": \"" << transactionId << "\",\"message\": " << req.str() << "}" ;
            m_pController->getClientController()->sendResponseToClient( rid, o.str(), transactionId ) ; 

        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << err.what() << endl;
        }                       

        /* we must explicitly delete an object allocated with placement new */

        pData->~SipMessageData() ;
    }

    int SipDialogController::processResponseOutsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
        mapOrq2IIP::iterator it = m_mapOrq2IIP.find( orq ) ;
        if( m_mapOrq2IIP.end() == it ) {
            DR_LOG(log_error) << "SipDialogController::processResponse - unable to match response with callid " << sip->sip_call_id->i_id << endl ;
            //TODO: do I need to destroy this transaction?
            return -1 ; //TODO: check meaning of return value
        }

        /* send to client */
        boost::shared_ptr<IIP> iip = it->second ;
        m_pController->getClientController()->route_response_inside_transaction( orq, sip, iip->transactionId() ) ;

        bool bClear = false ;
        iip->dlg()->setSipStatus( sip->sip_status->st_status ) ;

        if( sip->sip_status->st_status >= 200 ) {

            bClear = true ;
            if( sip->sip_cseq->cs_method ==  sip_method_invite ) {

                /* we need to send ACK in the case of success response (stack handles it for us in non-success) */
                if( 200 == sip->sip_status->st_status ) {
                   // TODO: don't send the ACK if the INVITE had no body: the ACK must have a body in that case, and the client must supply it
                    nta_leg_t* leg = iip->leg() ;
                    nta_leg_rtag( leg, sip->sip_to->a_tag) ;
                    nta_leg_client_reroute( leg, sip->sip_record_route, sip->sip_contact, true );

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

        DR_LOG(log_debug) << "responding to sip request in thread " << boost::this_thread::get_id() << " with transactionId " << transactionId << endl ;

        mapTransactionId2IIP::iterator it = m_mapTransactionId2IIP.find( transactionId ) ;
        if( m_mapTransactionId2IIP.end() != it ) {
            boost::shared_ptr<IIP> iip = it->second ;
            nta_leg_t* leg = iip->leg() ;
            nta_incoming_t* irq = iip->irq() ;
            boost::shared_ptr<SipDialog> dlg = iip->dlg() ;

            int code ;
            string status ;
            pMsg->get<int>("data.code", code ) ;
            pMsg->get<string>("data.status", status);

            dlg->setSipStatus( code ) ;

            /* iterate through data.opts.headers, adding headers to the response */
            json_spirit::mObject obj ;
            if( pMsg->get<json_spirit::mObject>("data.msg.headers", obj) ) {
            	tagi_t* tags = this->makeTags( obj ) ;

	            nta_incoming_treply( irq, code, status.empty() ? NULL : status.c_str()
                    ,TAG_IF( code >= 200, SIPTAG_CONTACT(m_pController->getMyContact()))
                    ,TAG_NEXT(tags) ) ;           	

            	delete[] tags ;
            }
            else {
	            nta_incoming_treply( irq, code, status.empty() ? NULL : status.c_str()
                    ,TAG_IF( code >= 200, SIPTAG_CONTACT(m_pController->getMyContact()))
                    ,TAG_END() ) ;           	
            }
          }
        else {
            DR_LOG(log_warning) << "Unable to find invite-in-progress with transactionId " << transactionId << endl ;
        }

        /* we must explicitly delete an object allocated with placement new */
        pData->~SipMessageData() ;
    }

    int SipDialogController::processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "SipDialogController::processRequestInsideDialog" << endl ;
        int rc = 0 ;
        switch (sip->sip_request->rq_method ) {
            case sip_method_ack:
            {
                /* all done */
                mapLeg2IIP::iterator it = m_mapLeg2IIP.find( leg ) ;
                if( m_mapLeg2IIP.end() == it ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find IIP for leg" << endl ;
                    assert(false) ;
                }
                else {
                    boost::shared_ptr<IIP> iip = it->second ;
                    string transactionId = iip->transactionId() ;

                    boost::shared_ptr<SipDialog> dlg = this->clearIIP( leg ) ;
                    m_pController->notifyDialogConstructionComplete( dlg ) ;
                    m_pController->getClientController()->route_request_inside_dialog( irq, sip, dlg->getDialogId() ) ;
                }
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

                m_pController->getClientController()->route_request_inside_dialog( irq, sip, dlg->getDialogId() ) ;
 
                this->clearDialog( leg ) ;
                rc = 200 ;
                break ;
            }
            case sip_method_info:
            {
                if( 0 == strcmp( sip->sip_content_type->c_type, "application/msml+xml") ) {
                    /* msml INFO messages are notifications of events, and there is no reason (that I know of) to send anything but a 200 in response*/
                    rc = 200 ;
                }
                boost::shared_ptr<SipDialog> dlg ;
                if( !this->findDialogByLeg( leg, dlg ) ) {
                    DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - unable to find Dialog for leg" << endl ;
                    assert(0) ;
                    return 200 ;
                }

                m_pController->getClientController()->route_request_inside_dialog( irq, sip, dlg->getDialogId() ) ;

            }

            default:
                DR_LOG(log_error) << "SipDialogController::processRequestInsideDialog - method not implemented " << sip->sip_request->rq_method_name << endl ;
                break ;
        }
        return rc ;
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

            string strBody ;
            pMsg->get<string>("data.msg.body", strBody) ;
            DR_LOG(log_debug) << "SipDialogController::doSendRequestInsideDialog body is " << strBody << endl ;
 
            string strMethod ;
            if( !pMsg->get<string>("data.method", strMethod) ) {
                throw std::runtime_error("method is required") ;
            }

			sip_method_t mtype = methodType( strMethod ) ;
			if( sip_method_unknown == mtype ) {
			    throw std::runtime_error("unknown method") ;
			}

			json_spirit::mObject obj ;
			if( pMsg->get<json_spirit::mObject>("data.msg.headers", obj) ) {
				tagi_t* tags = this->makeTags( obj ) ;

			    orq = nta_outgoing_tcreate( leg, response_to_request_inside_dialog, (nta_outgoing_magic_t *) m_pController,
                                                            NULL,
                                                            mtype, strMethod.c_str(),
                                                            NULL,
                                                            TAG_IF(!strBody.empty(), SIPTAG_PAYLOAD_STR(strBody.c_str())),
                                                            TAG_NEXT(tags),
                                                            TAG_END() ) ;
				delete[] tags ;
			}
			else {
			    orq = nta_outgoing_tcreate( leg, response_to_request_inside_dialog, (nta_outgoing_magic_t *) m_pController,
                                                            NULL,
                                                            mtype, strMethod.c_str(),
                                                            NULL,
                                                            TAG_IF(!strBody.empty(), SIPTAG_PAYLOAD_STR(strBody.c_str())),
                                                            TAG_END() ) ;
			}
           if( NULL == orq ) throw std::runtime_error("internal error attempting to create sip transaction") ;               
            
            msg_t* m = nta_outgoing_getrequest(orq) ;
            sip_t* sip = sip_object( m ) ;

            string transactionId ;
            generateUuid( transactionId ) ;

            boost::shared_ptr<RIP> p = boost::make_shared<RIP>( transactionId, rid ) ;
            m_mapOrq2RIP.insert( mapOrq2RIP::value_type(orq,p)) ;

            SofiaMsg req( orq, sip ) ;
            o << "{\"success\": true, \"transactionId\": \"" << transactionId << "\",\"message\": " << req.str() << "}" ;
            m_pController->getClientController()->sendResponseToClient( rid, o.str(), transactionId ) ; 

       } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << "SipDialogController::doSendSipRequestInsideDialog - Error " << err.what() << endl;
			o << "{\"success\": false, \"reason\": \"" << err.what() << "\"}" ;
			m_pController->getClientController()->sendResponseToClient( rid, o.str() ) ; 
       }                       

        /* we must explicitly delete an object allocated with placement new */
        pData->~SipMessageData() ;
    }
    int SipDialogController::processResponseInsideDialog( nta_outgoing_t* orq, sip_t const* sip )  {
    	ostringstream o ;

    	mapOrq2RIP::iterator it = m_mapOrq2RIP.find( orq ) ;

    	assert( m_mapOrq2RIP.end() != it ) ;

    	boost::shared_ptr<RIP> rip = it->second ;

        m_pController->getClientController()->route_response_inside_transaction( orq, sip, rip->transactionId() ) ;

    	m_mapOrq2RIP.erase( it ) ;

		return 0 ;
    }
    int SipDialogController::processCancel( nta_incoming_t* irq, sip_t const *sip ) {
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
            m_pController->getClientController()->route_cancel_transaction( irq, sip, dlg->getTransactionId() ) ;

        }
        else if( sip->sip_request->rq_method == sip_method_ack ) {
            boost::shared_ptr<IIP> iip ;
            if( !findIIPByIrq( irq, iip ) ) {
                DR_LOG(log_error) << "Unable to find invite-in-progress for ACK with call-id " << sip->sip_call_id->i_id << endl ;
                return 0 ;
            }
            boost::shared_ptr<SipDialog> dlg = this->clearIIP( iip->leg() ) ;
            DR_LOG(log_debug) << "After clearing UAS IIP for ACK to non-success response, there are " << m_mapLeg2IIP.size() << " invites in progress" << endl ;
            DR_LOG(log_debug) << "Most recent sip response on this dialog was " << dlg->getSipStatus() << endl ;
            //NB: when we get a CANCEL sofia sends the 487 response to the INVITE itself, so our latest sip status will be a provisional
            //not sure that we need to do anything particular about that however....though it we write cdrs we would want to capture the 487 final response
        }
        else {
            DR_LOG(log_debug) << "Received " << sip->sip_request->rq_method_name << " for call-id " << sip->sip_call_id->i_id << ", discarding" << endl ;
        }
        return 0 ;
    }


	void SipDialogController::addDialog( boost::shared_ptr<SipDialog> dlg ) {
		const string strDialogId = dlg->getDialogId() ;
		DR_LOG(log_debug) << "SipDialogController::addDialog call-id: " << dlg->getCallId() << " dialog id: " << strDialogId << endl ;
		nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
		assert( leg ) ;

        m_mapLeg2Dialog.insert( mapLeg2Dialog::value_type(leg,dlg)) ;	
        m_mapId2Dialog.insert( mapId2Dialog::value_type(strDialogId, dlg)) ;

        m_pController->getClientController()->addDialogForTransaction( dlg->getTransactionId(), strDialogId ) ;
	}

	bool SipDialogController::findDialogByLeg( nta_leg_t* leg, boost::shared_ptr<SipDialog>& dlg ) {
		/* look in invites-in-progress first */
        mapLeg2IIP::iterator it = m_mapLeg2IIP.find( leg ) ;
        if( m_mapLeg2IIP.end() == it ) {

        	/* if not found, look in stable dialogs */
			mapLeg2Dialog::iterator itLeg = m_mapLeg2Dialog.find( leg ) ;
			if( m_mapLeg2Dialog.end() == itLeg ) return false ;
			dlg = itLeg->second ;
			return true ;
        }

        dlg = it->second->dlg() ;
        return true ;

	}
	bool SipDialogController::findDialogById( const string& strDialogId, boost::shared_ptr<SipDialog>& dlg ) {
		mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
		if( m_mapId2Dialog.end() == it ) return false ;
		dlg = it->second ;
		return true ;
	}

	void SipDialogController::clearDialog( const string& strDialogId ) {
		mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
		if( m_mapId2Dialog.end() == it ) {
			assert(0) ;
			return ;
		}
		boost::shared_ptr<SipDialog> dlg = it->second ;
		nta_leg_t* leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
		m_mapId2Dialog.erase( it ) ;

		mapLeg2Dialog::iterator itLeg = m_mapLeg2Dialog.find( leg ) ;
		if( m_mapLeg2Dialog.end() == itLeg ) {
			assert(0) ;
			return ;
		}
		m_mapLeg2Dialog.erase( itLeg ) ;

	}
	void SipDialogController::clearDialog( nta_leg_t* leg ) {
		mapLeg2Dialog::iterator it = m_mapLeg2Dialog.find( leg ) ;
		if( m_mapLeg2Dialog.end() == it ) {
			assert(0) ;
			return ;
		}
		boost::shared_ptr<SipDialog> dlg = it->second ;
		string strDialogId = dlg->getDialogId() ;
		m_mapLeg2Dialog.erase( it ) ;

		mapId2Dialog::iterator itId = m_mapId2Dialog.find( strDialogId ) ;
		if( m_mapId2Dialog.end() == itId ) {
			assert(0) ;
			return ;
		}
		m_mapId2Dialog.erase( itId );
	}

    void SipDialogController::addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) {        
        const char* a_tag = nta_incoming_tag( irq, NULL) ;
        nta_leg_tag( leg, a_tag ) ;
        dlg->setLocalTag( a_tag ) ;
        boost::shared_ptr<IIP> p = boost::make_shared<IIP>(leg, irq, transactionId, dlg) ;
        m_mapIrq2IIP.insert( mapIrq2IIP::value_type(irq, p) ) ;
        m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(transactionId, p) ) ;   
        m_mapLeg2IIP.insert( mapLeg2IIP::value_type(leg,p)) ;   

        nta_incoming_bind( irq, uasCancel, (nta_incoming_magic_t *) m_pController ) ;
    }
    void SipDialogController::addOutgoingInviteTransaction( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) {        
        boost::shared_ptr<IIP> p = boost::make_shared<IIP>(leg, orq, transactionId, dlg) ;
        m_mapOrq2IIP.insert( mapOrq2IIP::value_type(orq, p) ) ;
        m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(transactionId, p) ) ;   
        m_mapLeg2IIP.insert( mapLeg2IIP::value_type(leg,p)) ;   
    }
    bool SipDialogController::findIIPByIrq( nta_incoming_t* irq, boost::shared_ptr<IIP>& iip ) {
        mapIrq2IIP::iterator it = m_mapIrq2IIP.find( irq ) ;
        if( m_mapIrq2IIP.end() == it ) return false ;
        iip = it->second ;
        return true ;
    }
    boost::shared_ptr<SipDialog> SipDialogController::clearIIP( nta_leg_t* leg ) {
        mapLeg2IIP::iterator it = m_mapLeg2IIP.find( leg ) ;
        assert( it != m_mapLeg2IIP.end() ) ;
        boost::shared_ptr<IIP> iip = it->second ;
        boost::shared_ptr<SipDialog>  dlg = iip->dlg() ;
        mapIrq2IIP::iterator itIrq = m_mapIrq2IIP.find( iip->irq() ) ;
        mapOrq2IIP::iterator itOrq = m_mapOrq2IIP.find( iip->orq() ) ;
        mapTransactionId2IIP::iterator itTransaction = m_mapTransactionId2IIP.find( iip->transactionId() ) ;
        assert( itTransaction != m_mapTransactionId2IIP.end() ) ;

        assert( !(m_mapIrq2IIP.end() == itIrq && m_mapOrq2IIP.end() == itOrq )) ;

        m_mapLeg2IIP.erase( it ) ;
        if( itIrq != m_mapIrq2IIP.end() ) m_mapIrq2IIP.erase( itIrq ) ;
        if( itOrq != m_mapOrq2IIP.end() ) m_mapOrq2IIP.erase( itOrq ) ;
        m_mapTransactionId2IIP.erase( itTransaction ) ;
        return dlg ;
    }
	tagi_t* SipDialogController::makeTags( json_spirit::mObject&  hdrs ) {
       vector<string> vecUnknownStr ;
        int nHdrs = hdrs.size() ;
        tagi_t *tags = new tagi_t[nHdrs+1] ;
        int i = 0; 
        for( json_spirit::mConfig::Object_type::iterator it = hdrs.begin() ; it != hdrs.end(); it++, i++ ) {

            /* default to skip, as this may not be a header we are allowed to set, or value might not be provided correctly (as a string) */
            tags[i].t_tag = tag_skip ;
            tags[i].t_value = (tag_value_t) 0 ;                     

            string hdr = boost::to_lower_copy( boost::replace_all_copy( it->first, "-", "_" ) );

            /* check to make sure this isn't a header that is not client-editable */
            if( isImmutableHdr( hdr ) ) {
                DR_LOG(log_warning) << "SipDialogController::makeTags - client provided a value for header '" << it->first << "' which is not modfiable by client" << endl;
                continue ;                       
            }

            try {
                string value = it->second.get_str() ;
                DR_LOG(log_debug) << "Adding header '" << hdr << "' with value '" << value << "'" << endl ;

                tag_type_t tt ;
                if( getTagTypeForHdr( hdr, tt ) ) {
                	/* well-known header */
                    tags[i].t_tag = tt;
                    tags[i].t_value = (tag_value_t) value.c_str() ;
                }
                else {
                    /* custom header */
                    if( string::npos != it->first.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890-_") ) {
                       DR_LOG(log_error) << "SipDialogController::makeTags - client supplied invalid custom header name '" << it->first << "'" << endl;
                       continue ;
                    }
                    else if( string::npos != value.find("\r\n") ) {
                      DR_LOG(log_error) << "SipDialogController::makeTags - client supplied invalid custom header value (contains CR or LF) for header '" << it->first << "'" << endl;
                       continue ;
                    }
                    ostringstream o ;
                    o << it->first << ": " <<  value.c_str()  ;
                    vecUnknownStr.push_back( o.str() ) ;
                    tags[i].t_tag = siptag_unknown_str ;
                    tags[i].t_value = (tag_value_t) vecUnknownStr.back().c_str() ;
                }
            } catch( std::runtime_error& err ) {
                DR_LOG(log_error) << "SipDialogController::makeTags - Error attempting to read string value for header " << hdr << ": " << err.what() << endl;
            }                       
        }

        tags[nHdrs].t_tag = tag_null ;
        tags[nHdrs].t_value = (tag_value_t) 0 ;       

        return tags ;	//NB: caller responsible to delete after use to free memory      
	}

}
