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
#include <boost/regex.hpp>
#include <boost/bind.hpp>

#include <sofia-sip/sip_util.h>
#include <sofia-sip/msg_header.h>
#include <sofia-sip/msg_addr.h>

#include "sip-proxy-controller.hpp"
#include "controller.hpp"
#include "pending-request-controller.hpp"

namespace {
    void cloneProxy(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipProxyController::ProxyData* d = reinterpret_cast<drachtio::SipProxyController::ProxyData*>( arg ) ;
        pController->getProxyController()->doProxy(d) ;
    }
} ;


namespace drachtio {
    Proxy_t::Proxy_t(const string& clientMsgId, const string& transactionId, msg_t* msg, sip_t* sip, tport_t* tp,const string& type, 
        bool fullResponse, const vector<string>& vecDestination, const string& headers ) : 
        m_clientMsgId(clientMsgId), m_transactionId(transactionId), m_msg( msg ), m_tp(tp), m_canceled(false), m_headers(headers),
        m_fullResponse(fullResponse), m_vecDestination(vecDestination), m_stateless(0==type.compare("stateless")), 
        m_nCurrentDest(0), m_lastResponse(0),m_provisionalTimeout(0), m_finalTimeout(0),m_handleProvisionalResponse(0),
        m_handleFinalResponse(0) {

        msg_ref( m_msg ) ; 

    }
    Proxy_t::~Proxy_t() {
        msg_unref( m_msg ) ;
    }
    msg_t* Proxy_t::getMsg() { return m_msg ; }
    sip_t* Proxy_t::getSipObject() { return sip_object(m_msg); }
    const string& Proxy_t::getTransactionId() { return m_transactionId; }
    tport_t* Proxy_t::getTport() { return m_tp; }
    void Proxy_t::setProvisionalTimeout(const string& t ) {
        boost::regex e("^(\\d+)(ms|s)$", boost::regex::extended);
        boost::smatch mr;
        if( boost::regex_search( t, mr, e ) ) {
            string s = mr[1] ;
            m_provisionalTimeout = ::atoi( s.c_str() ) ;
            if( 0 == mr[2].compare("s") ) {
                m_provisionalTimeout *= 1000 ;
            }
            DR_LOG(log_debug) << "provisional timeout is " << m_provisionalTimeout << "ms" ;
        }
        else if( t.length() > 0 ) {
            DR_LOG(log_error) << "Invalid timeout syntax: " << t ;
        }
    }
    void Proxy_t::setFinalTimeout(const string& t ) {
        boost::regex e("^(\\d+)(ms|s)$", boost::regex::extended);
        boost::smatch mr;
        if( boost::regex_search( t, mr, e ) ) {
            string s = mr[1] ;
            m_finalTimeout = ::atoi( s.c_str() ) ;
            if( 0 == mr[2].compare("s") ) {
                m_finalTimeout *= 1000 ;
            }
            DR_LOG(log_debug) << "final timeout is " << m_finalTimeout << "ms" ;
        }
        else if( t.length() > 0 ) {
            DR_LOG(log_error) << "Invalid timeout syntax: " << t ;
        }
    }
    bool Proxy_t::isCanceledBranch(const char *branch) { 
        DR_LOG(log_debug) << "isCanceledBranch: checking for " << branch << " size of set is " << m_setCanceledBranches.size() ;
        boost::unordered_set<string>::iterator it = m_setCanceledBranches.find(branch) ;
        bool found = it != m_setCanceledBranches.end(); 
        return found ;
    }
 


    SipProxyController::SipProxyController( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone), 
        m_agent(pController->getAgent()), m_queue(pController->getRoot()) {

            assert(m_agent) ;

    }
    SipProxyController::~SipProxyController() {
    }

    void SipProxyController::proxyRequest( const string& clientMsgId, const string& transactionId, const string& proxyType, 
        bool fullResponse, bool followRedirects, const string& provisionalTimeout, const string& finalTimeout, 
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
            addProxy( clientMsgId, transactionId, p->getMsg(), p->getSipObject(), p->getTport(), proxyType, fullResponse, followRedirects, 
                provisionalTimeout, finalTimeout, vecDestinations, headers ) ;
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

        boost::shared_ptr<Proxy_t> p = getProxyByTransactionId( transactionId ) ;
        if( !p ) {
            m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", "transaction id no longer exists") ;
        }
        else {

            msg_t* msg = p->getMsg() ;
            sip_t* sip = p->getSipObject() ;

            //decrement max forwards
            if( sip->sip_max_forwards ) {
                if( sip->sip_max_forwards->mf_count <= 0 ) {
                    DR_LOG(log_error) << "SipProxyController::doProxy rejecting request due to max forwards used up " << sip->sip_call_id->i_id ;
                    msg_ref( msg ) ;    //because the below will unref
                    nta_msg_treply( m_agent, msg, SIP_483_TOO_MANY_HOPS,TAG_END() ) ;  
                    removeProxyByTransactionId( transactionId )  ;
                    m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                        "Rejected with 483 Too Many Hops due to Max-Forwards value of 0" ) ;
                    pData->~ProxyData() ; 
                    return ;
                }
                else {
                    sip->sip_max_forwards->mf_count-- ;
                }
            }
            else {
                sip_add_tl(msg, sip,
                    SIPTAG_MAX_FORWARDS_STR("70"),
                    TAG_END());
            }

            if( p->isStateful() ) {
                msg_ref( msg ) ;    //because the below will unref
                nta_msg_treply( m_agent, msg, 100, NULL, TAG_END() ) ;                
            } 
 
            int rc = proxyToTarget( p, p->getCurrentTarget() ) ;

            if( rc < 0 ) {
                m_pController->getClientController()->route_api_response( pData->getClientMsgId(), "NOK", 
                    string("error proxying request to ") + p->getCurrentTarget() ) ;
                removeProxyByTransactionId( transactionId ) ;
            }

            if( !p->wantsFullResponse() ) {
                m_pController->getClientController()->route_api_response( p->getClientMsgId(), "OK", "done" ) ;
            }
        }

        //N.B.: we must explicitly call the destructor of an object allocated with placement new
        pData->~ProxyData() ; 

    }
    bool SipProxyController::processResponse( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;
        DR_LOG(log_debug) << "SipProxyController::processResponse " << std::dec << sip->sip_status->st_status << " " << callId ;

        if( sip->sip_cseq->cs_method == sip_method_invite ) {
            boost::shared_ptr<Proxy_t> p = getProxyByCallId( sip->sip_call_id->i_id ) ;
            if( !p ) {
                DR_LOG(log_error) << "SipProxyController::processResponse unknown call-id for response " <<  std::dec << sip->sip_status->st_status << 
                    " " << sip->sip_call_id->i_id ;
                return false ;
            }

            int status = sip->sip_status->st_status ;
           
            bool locallyCanceled = p->isCanceledBranch( sip->sip_via->v_branch ) ;
            if( locallyCanceled ) {
                //
                //this is a response to a CANCEL we generated due to a timeout
                //ack it and do nothing, as we have already generated a new INVITE to the next server
                DR_LOG(log_debug) << "Received final response to an INVITE that we canceled, generate ack" ;
                assert( 487 == status ) ;
                ackResponse( msg ) ;
                msg_unref( msg ) ;
                return true ;
            }

            p->setLastStatus( status ) ;

            //clear timers, as appropriate 
            clearTimerProvisional(p) ;
            if( status >= 200 ) {
                clearTimerFinal(p) ;
            }

            bool crankback = status > 200 && !isTerminatingResponse( status ) && p->hasMoreTargets() && !p->isCanceled() ;

            //send response back to client
            if( p->wantsFullResponse() ) {
                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta(msg) ;
                string s ;
                meta.toMessageFormat(s) ;

                string data = s + "|||continue" + CRLF + encodedMessage ; //no transaction id or dialog id

                m_pController->getClientController()->route_api_response( p->getClientMsgId(), "OK", data ) ;   

                if( status >= 200 && !crankback ) {
                    m_pController->getClientController()->route_api_response( p->getClientMsgId(), "OK", "done" ) ;
                 }             
            }
            if( 200 == status ) removeProxyByCallId( callId ) ;
            
            if( p->isStateful() && 100 == status ) {
                msg_unref( msg ) ;
                return true ;  //in stateful mode we've already sent a 100 Trying  
            }

            //follow a redirect response if we are configured to do so
            if( status >= 300 && status < 399 && p->shouldFollowRedirects() && sip->sip_contact ) {
                sip_contact_t* contact = sip->sip_contact ;
                int i = 0 ;
                vector<string>& vec = p->getDestinations() ;
                for (sip_contact_t* m = sip->sip_contact; m; m = m->m_next, i++) {
                    char buffer[URL_MAXLEN] = "" ;
                    url_e(buffer, URL_MAXLEN, m->m_url) ;

                    DR_LOG(log_debug) << "SipProxyController::processResponse -- adding contact from redirect response " << buffer ;
                    vec.insert( vec.begin() + p->getCurrentOffset() + 1 + i, buffer ) ;
                }
                crankback = true ;
            }

            //don't send back to client if we are going to fork a new INVITE
            if( crankback ) {
                ackResponse( msg ) ;

                DR_LOG(log_info) << "SipProxyController::processRequestWithoutRouteHeader - proxy crankback to attempt next destination " ;
                proxyToTarget( p, p->getNextTarget() ) ;

                msg_unref( msg ) ;
                return true ;
             }   
        }
        else if( sip->sip_cseq->cs_method == sip_method_cancel ) {
            boost::shared_ptr<Proxy_t> p = getProxyByCallId( sip->sip_call_id->i_id ) ;
            if( p && p->isCanceledBranch( sip->sip_via->v_branch ) ) {
                DR_LOG(log_debug) << "Received response to our CANCEL, discarding" ;
                msg_unref(msg) ;
                return true ;
            }
        }
   
        int rc = nta_msg_tsend( m_pController->getAgent(), msg, NULL, TAG_END() ) ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "SipProxyController::processResponse failed proxying response " << std::dec << sip->sip_status->st_status << 
                " " << sip->sip_call_id->i_id << ": error " << rc ; 
            return false ;            
        }

        return true ;
    }
    bool SipProxyController::processRequestWithRouteHeader( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;
        DR_LOG(log_debug) << "SipProxyController::processRequestWithRouteHeader " << callId ;

        sip_route_remove( msg, sip) ;

        int rc = nta_msg_tsend( m_pController->getAgent(), msg, NULL, TAG_END() ) ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "SipProxyController::processRequestWithRouteHeader failed proxying ACK " << callId << ": error " << rc ; 
            return false ;
        }
    
        return true ;
    }
    bool SipProxyController::processRequestWithoutRouteHeader( msg_t* msg, sip_t* sip ) {
        string callId = sip->sip_call_id->i_id ;
        DR_LOG(log_debug) << "SipProxyController::processRequestWithoutRouteHeader " << callId ;

        boost::shared_ptr<Proxy_t> p = getProxyByCallId( sip->sip_call_id->i_id ) ;
        if( !p ) {
            DR_LOG(log_error) << "SipProxyController::processRequestWithoutRouteHeader unknown call-id for ACK/PRACK " <<  
                sip->sip_call_id->i_id ;

            return false ;
        }

        if( p->isForking() ) {
            //don't proxy retransmissions
            if( sip->sip_request->rq_method == sip_method_invite ) {
                DR_LOG(log_info) << "Discarding retransmitted message because we are a forking proxy" ;
                nta_msg_discard( m_pController->getAgent(), msg ) ;
                return false ;    
            }
        }
        string dest = p->getCurrentTarget() ;
        int rc = nta_msg_tsend( m_pController->getAgent(), msg, URL_STRING_MAKE(dest.c_str()), 
            NTATAG_BRANCH_KEY(p->getCurrentBranch().c_str()),
            TAG_END() ) ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "SipProxyController::processRequestWithoutRouteHeader failed proxying request " << callId << ": error " << rc ; 
            return false ;
        }
    
        if( sip->sip_request->rq_method == sip_method_ack ) {
            //we get here in case of an ACK to a failed final response
            DR_LOG(log_info) << "SipProxyController::processRequestWithoutRouteHeader - proxy attempt completed with non-success status " << 
                p->getLastStatus() ;
            removeProxyByCallId( callId ) ;
        }
        else if( sip->sip_request->rq_method == sip_method_cancel ) {
            p->setCanceled() ;
        }
    
        return true ;
    }
    int SipProxyController::proxyToTarget( boost::shared_ptr<Proxy_t>p, const string& dest ) {
        msg_t* msg = msg_dup(p->getMsg()) ;
        sip_t* sip = sip_object(msg) ;

        sip_request_t *rq = sip_request_format(msg_home(msg), "%s %s SIP/2.0", sip->sip_request->rq_method_name, dest.c_str() ) ;
        msg_header_replace(msg, NULL, (msg_header_t *)sip->sip_request, (msg_header_t *) rq) ;

        tagi_t* tags = makeTags( p->getHeaders() ) ;

        string random ;
        generateUuid( random ) ;
        string branch = string("z9hG4bK-") + random ;
        p->setCurrentBranch( branch ) ;

        int rc = nta_msg_tsend( m_pController->getAgent(), msg, URL_STRING_MAKE(dest.c_str()), 
            TAG_IF( p->isStateful(), SIPTAG_RECORD_ROUTE(m_pController->getMyRecordRoute() ) ),
            NTATAG_BRANCH_KEY(branch.c_str()),
            TAG_NEXT(tags) ) ;

        deleteTags( tags ) ;

        //set timers if client requested, and we have other servers to crank back to 
        if( p->hasMoreTargets() ) {
            if( p->getProvisionalTimeout() > 0 ) {
                TimerEventHandle handle = m_queue.add( boost::bind(&SipProxyController::timerProvisional, this, p), 
                    NULL, p->getProvisionalTimeout() ) ;
                assert( handle ) ;
                if( handle ) p->setProvisionalHandle( handle ) ;
                DR_LOG(log_debug) << "set a provisional timeout" ;
            }
            if(  p->getFinalTimeout() > 0 ) {
                TimerEventHandle handle = m_queue.add( boost::bind(&SipProxyController::timerFinal, this, p), 
                    NULL, p->getFinalTimeout() ) ;
                assert( handle ) ;
                if( handle ) p->setFinalHandle( handle ) ;
                DR_LOG(log_debug) << "set a final timeout" ;
            }
        }

        return rc ;
    }
    int SipProxyController::ackResponse( msg_t* msg ) {
        nta_agent_t* agent = m_pController->getAgent() ;
        sip_t *sip = sip_object(msg);
        msg_t *amsg = nta_msg_create(agent, 0);
        sip_t *asip = sip_object(amsg);
        url_string_t const *ruri;
        nta_outgoing_t *ack = NULL, *bye = NULL;
        sip_cseq_t *cseq;
        sip_request_t *rq;
        sip_route_t *route = NULL, *r, r0[1];
        su_home_t *home = msg_home(amsg);

        if (asip == NULL)
        return -1;

        sip_add_tl(amsg, asip,
            SIPTAG_TO(sip->sip_to),
            SIPTAG_FROM(sip->sip_from),
            SIPTAG_CALL_ID(sip->sip_call_id),
            TAG_END());

        if (sip->sip_contact && sip->sip_status->st_status > 399 ) {
            ruri = (url_string_t const *)sip->sip_contact->m_url;
        } else {
            su_sockaddr_t const *su = msg_addr(msg);
            char name[SU_ADDRSIZE] = "";
            char uri[SU_ADDRSIZE] = "" ;
            char szTmp[10] ;

            su_inet_ntop(su->su_family, SU_ADDR(su), name, sizeof(name));
            sprintf( szTmp, "%u", ntohs(su->su_port) ) ;
            sprintf(uri, "sip:%s:%s", name, szTmp) ;
            ruri = URL_STRING_MAKE(uri) ;
        }

        if (!(cseq = sip_cseq_create(home, sip->sip_cseq->cs_seq, SIP_METHOD_ACK)))
            goto err;
        else
            msg_header_insert(amsg, (msg_pub_t *)asip, (msg_header_t *)cseq);

        if (!(rq = sip_request_create(home, SIP_METHOD_ACK, ruri, NULL)))
            goto err;
        else
            msg_header_insert(amsg, (msg_pub_t *)asip, (msg_header_t *)rq);

        if( nta_msg_tsend( agent, amsg, NULL, 
            NTATAG_BRANCH_KEY(sip->sip_via->v_branch),
            TAG_END() ) < 0 )
 
            goto err ;

         return 0;

        err:
            if( amsg ) msg_unref(amsg);
            return -1;
    }
    int SipProxyController::cancelCurrentRequest( boost::shared_ptr<Proxy_t> p ) {
        nta_agent_t* agent = m_pController->getAgent() ;
        
        msg_t* msg = p->getMsg() ;
        sip_t *sip = p->getSipObject() ;

        msg_t *cmsg = nta_msg_create(agent, 0);
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

        if (!(rq = sip_request_format(home, "CANCEL %s SIP/2.0", p->getCurrentTarget().c_str() )))
            goto err;
        else
            msg_header_insert(cmsg, (msg_pub_t *)csip, (msg_header_t *)rq);

        if( nta_msg_tsend( agent, cmsg, NULL, 
            NTATAG_BRANCH_KEY(p->getCurrentBranch().c_str()),
            TAG_END() ) < 0 )
 
            goto err ;

         return 0;

        err:
            if( cmsg ) msg_unref(cmsg);
            return -1;
    }

    void SipProxyController::timerProvisional( boost::shared_ptr<Proxy_t> p ) {
        DR_LOG(log_debug) << "provisional timer fired, queue length " << m_queue.size() ;
        
        //if we later get a response we'll generate a CANCEL then
        p->addUnresponsiveBranch( p->getCurrentBranch().c_str() ) ;

        //clear the provisional timer handle as it is no longer valid
        p->clearProvisionalHandle() ;

        //we shouldn't be here unless we have a next server to try; send the request there
        assert( p->hasNextTarget() ) ;
        proxyToTarget( p, p->getNextTarget() ) ;
    }
    void SipProxyController::timerFinal( boost::shared_ptr<Proxy_t> p ) {
        DR_LOG(log_debug) << "CANCEL request due to lack of final within specified timeout, queue length " << m_queue.size() ;
        
        //generate a CANCEL
        cancelCurrentRequest( p ) ;
        p->addCanceledBranch( p->getCurrentBranch().c_str() ) ;

        //clear the final timer handle as it is no longer valid
        p->clearFinalHandle() ;

        //we shouldn't be here unless we have a next server to try; send the request there
        assert( p->hasNextTarget() ) ;
        proxyToTarget( p, p->getNextTarget() ) ;
    }

    bool SipProxyController::isProxyingRequest( msg_t* msg, sip_t* sip )  {
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapCallId2Proxy::iterator it = m_mapCallId2Proxy.find( sip->sip_call_id->i_id ) ;
      return it != m_mapCallId2Proxy.end() ;
    }

    boost::shared_ptr<Proxy_t> SipProxyController::removeProxyByTransactionId( const string& transactionId ) {
      boost::shared_ptr<Proxy_t> p ;
      boost::lock_guard<boost::mutex> lock(m_mutex) ;
      mapTxnId2Proxy::iterator it = m_mapTxnId2Proxy.find( transactionId ) ;
      if( it != m_mapTxnId2Proxy.end() ) {
        p = it->second ;
        m_mapTxnId2Proxy.erase(it) ;
        mapCallId2Proxy::iterator it2 = m_mapCallId2Proxy.find( p->getCallId() ) ;
        assert( it2 != m_mapCallId2Proxy.end()) ;
        m_mapCallId2Proxy.erase( it2 ) ;
      }
      assert( m_mapTxnId2Proxy.size() == m_mapCallId2Proxy.size() );
      DR_LOG(log_debug) << "SipProxyController::removeProxyByTransactionId - there are now " << m_mapTxnId2Proxy.size() << " proxy instances" ;
      return p ;
    }
    boost::shared_ptr<Proxy_t> SipProxyController::removeProxyByCallId( const string& callId ) {
      boost::shared_ptr<Proxy_t> p ;
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
    void SipProxyController::clearTimerProvisional( boost::shared_ptr<Proxy_t> p ) {
      if( p->getProvisionalHandle() ) {
          m_queue.remove( p->getProvisionalHandle() ) ;
          p->clearProvisionalHandle() ;
          DR_LOG(log_debug) << "cleared provisional timeout, queue length is now " << m_queue.size() ;
      }     
    }
    void SipProxyController::clearTimerFinal( boost::shared_ptr<Proxy_t> p ) {
      if( p->getProvisionalHandle() ) {
          m_queue.remove( p->getProvisionalHandle() ) ;
          p->clearProvisionalHandle() ;
          DR_LOG(log_debug) << "cleared final timeout, queue length is now " << m_queue.size() ;
      }     
    }

    void SipProxyController::logStorageCount(void)  {
        boost::lock_guard<boost::mutex> lock(m_mutex) ;

        DR_LOG(log_debug) << "SipProxyController storage counts"  ;
        DR_LOG(log_debug) << "----------------------------------"  ;
        DR_LOG(log_debug) << "m_mapCallId2Proxy size:                                          " << m_mapCallId2Proxy.size()  ;
        DR_LOG(log_debug) << "m_mapTxnId2Proxy size:                                           " << m_mapTxnId2Proxy.size()  ;
    }


} ;
