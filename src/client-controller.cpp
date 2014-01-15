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
#include <iostream>

#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio.hpp>

#include "sofia-msg.hpp"
#include "client-controller.hpp"
#include "controller.hpp"

namespace drachtio {
     
    ClientController::ClientController( DrachtioController* pController, string& address, unsigned int port ) :
        m_pController( pController ),
        m_endpoint(  boost::asio::ip::tcp::v4(), port ),
        m_acceptor( m_ioservice, m_endpoint ) {
            
        boost::thread t(&ClientController::threadFunc, this) ;
        m_thread.swap( t ) ;
            
        this->start_accept() ;
    }
    ClientController::~ClientController() {
        this->stop() ;
    }
    void ClientController::threadFunc() {
        
        DR_LOG(log_debug) << "Client controller thread id: " << boost::this_thread::get_id() << endl ;
         
        /* to make sure the event loop doesn't terminate when there is no work to do */
        boost::asio::io_service::work work(m_ioservice);
        
        for(;;) {
            
            try {
                DR_LOG(log_notice) << "ClientController: io_service run loop started" << endl ;
                m_ioservice.run() ;
                DR_LOG(log_notice) << "ClientController: io_service run loop ended normally" << endl ;
                break ;
            }
            catch( std::exception& e) {
                DR_LOG(log_error) << "Error in event thread: " << string( e.what() ) << endl ;
                break ;
            }
        }
    }
    void ClientController::join( client_ptr client ) {
        m_clients.insert( client ) ;
        client_weak_ptr p( client ) ;
        for( vector<string>::const_iterator it = client->getServices().begin(); it != client->getServices().end(); ++it ) {
           m_services.insert(map_of_services::value_type(*it, p)) ;
        }
       DR_LOG(log_debug) << "Added client, count of connected clients is now: " << m_clients.size() << endl ;
    }
    void ClientController::leave( client_ptr client ) {
        m_clients.erase( client ) ;
        DR_LOG(log_debug) << "Removed client, count of connected clients is now: " << m_clients.size() << endl ;
    }

	void ClientController::start_accept() {
		client_ptr new_session( new Client( m_ioservice, *this ) ) ;
		m_acceptor.async_accept( new_session->socket(), boost::bind(&ClientController::accept_handler, this, new_session, boost::asio::placeholders::error));
    }
	void ClientController::accept_handler( client_ptr session, const boost::system::error_code& ec) {
        if(!ec) {
            session->start() ;
        }
        start_accept(); 
    }
    bool ClientController::wants_requests( client_ptr client, const string& verb ) {
        RequestSpecifier spec( client ) ;
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        m_request_types.insert( map_of_request_types::value_type(verb, spec)) ;  
        DR_LOG(log_debug) << "Added client for " << verb << " requests" << endl ;
        //TODO: validate the verb is supported
        return true ;  
    }

    bool ClientController::route_request_outside_dialog( nta_incoming_t* irq, sip_t const *sip, const string& transactionId ) {

        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( irq, sip ) ;

        string method_name = sip->sip_request->rq_method_name ;
        transform(method_name.begin(), method_name.end(), method_name.begin(), ::tolower);

        boost::lock_guard<boost::mutex> l( m_lock ) ;
        client_ptr client ;
        string matchId ;
        pair<map_of_request_types::iterator,map_of_request_types::iterator> pair = m_request_types.equal_range( method_name ) ;
        for( map_of_request_types::iterator it = pair.first; it != pair.second; ) {
            RequestSpecifier& spec = it->second ;

            if( !spec.client() ) {
                /* note: our weak pointer may be to a client that has disconnected, so we need to check and remove them from the map
                    at this point if that is so.
                */
                DR_LOG(log_debug) << "Removing disconnected client while iterating" << endl ;
                m_request_types.erase(it++) ;
                continue ;
            }
            client = spec.client() ;
            break ;
        }

        if( !client ) {
            DR_LOG(log_info) << "No clients found to handle incoming " << method_name << " request" << endl ;
           return false ;
        }

        /* we've selected a client for this message */
        string json = sm->str() ;

        JsonMsg jmsg( json ) ;

        m_mapTransactions.insert( mapTransactions::value_type(transactionId,client)) ;

        m_ioservice.post( boost::bind(&Client::sendRequestOutsideDialog, client, transactionId, json) ) ;
 
        return true ;
    }
    void ClientController::respondToSipRequest( const string& transactionId, boost::shared_ptr<JsonMsg> pMsg ) {
         m_pController->getDialogController()->respondToSipRequest( transactionId, pMsg ) ;
    }

    bool ClientController::route_request_inside_dialog( nta_incoming_t* irq, sip_t const *sip, const string& transactionId, const string& dialogId ) {
        client_ptr client = this->findClientForDialog( dialogId );
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_request_inside_dialog - client managing dialog has disconnected: " << dialogId << endl ;
            return false ;
        }

        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( irq, sip ) ;
        string json = sm->str() ;
        JsonMsg jmsg( json ) ;

        m_ioservice.post( boost::bind(&Client::sendRequestInsideDialog, client, transactionId, dialogId, json) ) ;

        return true ;
    }
    bool ClientController::route_response_inside_transaction( nta_outgoing_t* orq, sip_t const *sip, const string& transactionId ) {
        client_ptr client = this->findClientForTransaction( transactionId );
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_response_inside_transaction - client managing transaction has disconnected: " << transactionId << endl ;
            return false ;
        }

        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( orq, sip ) ;
        string json = sm->str() ;
        JsonMsg jmsg( json ) ;

        m_ioservice.post( boost::bind(&Client::sendResponseInsideTransaction, client, transactionId, json) ) ;

        return true ;
    }
    void ClientController::addDialogForTransaction( const string& transactionId, const string& dialogId ) {
        mapTransactions::iterator it = m_mapTransactions.find( transactionId ) ;
        if( m_mapTransactions.end() != it ) {
            m_mapDialogs.insert( mapDialogs::value_type(dialogId, it->second ) ) ;
        }
        else {
            DR_LOG(log_error) << "ClientController::addDialogForTransaction - transaction id " << transactionId << " not found" << endl ;
            assert(false) ;
        }
        DR_LOG(log_debug) << "ClientController::addDialogForTransaction - transaction id " << transactionId << 
            " has associated dialog " << dialogId << endl ;

        client_ptr client = this->findClientForDialog( dialogId );
        if( !client ) {
            m_mapDialogs.erase( dialogId ) ;
            m_mapTransactions.erase( transactionId ) ;
            DR_LOG(log_warning) << "ClientController::addDialogForTransaction - client managing dialog has disconnected: " << dialogId << endl ;
            return  ;
        }

        m_ioservice.post( boost::bind(&Client::sendDialogInfo, client, dialogId, transactionId) ) ;

    } 
    bool ClientController::sendSipRequest( client_ptr client, boost::shared_ptr<JsonMsg> pMsg, const string& rid ) {
        ostringstream o ;
        m_mapRequests.insert( mapRequests::value_type( rid, client)) ;   
        string strDialogId ;
        if( pMsg->get<string>("data.dialogId", strDialogId) ) {
            if( m_pController->sendRequestInsideDialog( pMsg, rid ) < 0 ) {
                o << "{\"success\": false, \"reason\": \"unknown sip dialog\"}" ;
                client->sendResponse( rid, o.str() ) ;
                return false ;
            }
            return true ;
        }
        else {
            string method ;
            string transactionId ;
            pMsg->get<string>("data.method", method ) ;
            if( 0 == method.compare("CANCEL")  ) {
                if( !pMsg->get<string>("data.transactionId", transactionId) ) {
                    o << "{\"success\": false, \"reason\": \"cancel request is missing transaction id\"}" ;
                    client->sendResponse( rid, o.str() ) ;
                    return false ;
                }
                return m_pController->getDialogController()->sendCancelRequest( pMsg, rid ) ;
            }
            return m_pController->getDialogController()->sendRequestOutsideDialog( pMsg, rid ) ;        
        }
    }
    void ClientController::sendResponseToClient( const string& rid, const string& strData ) {
        string null;
        sendResponseToClient( rid, strData, null) ;
    }
    void ClientController::sendResponseToClient( const string& rid, const string& strData, const string& transactionId ) {
        client_ptr client = findClientForRequest( rid ) ;
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::sendResponseToClient - client that sent the request has disconnected: " << rid << endl ;
            return ;
        }
        m_ioservice.post( boost::bind(&Client::sendResponse, client, rid, strData) ) ;   
        if( !transactionId.empty() ) {
            m_mapTransactions.insert( mapTransactions::value_type( transactionId, client) ) ; //TODO: need to think about when this gets cleared
        }     
    }
    bool ClientController::route_request_inside_invite( nta_incoming_t* irq, sip_t const *sip, const string& transactionId, const string& dialogId  ) {
        client_ptr client = findClientForTransaction( transactionId ) ;
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_request_inside_invite - client that was sent the transaction has disconnected: " << transactionId << endl ;
            return false;            
        }
        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( irq, sip ) ;
        string json = sm->str() ;
        JsonMsg jmsg( json ) ;

        if( dialogId.length() > 0 )  m_ioservice.post( boost::bind(&Client::sendRequestInsideInviteWithDialog, client, transactionId, dialogId, json) ) ;
        else m_ioservice.post( boost::bind(&Client::sendRequestInsideInvite, client, transactionId, json) ) ;

        return true ;
    }
    client_ptr ClientController::findClientForDialog( const string& dialogId ) {
        client_ptr client ;
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        mapDialogs::iterator it = m_mapDialogs.find( dialogId ) ;
        if( m_mapDialogs.end() != it ) client = it->second.lock() ;
        return client ;
    }
    client_ptr ClientController::findClientForRequest( const string& rid ) {
        client_ptr client ;
        mapRequests::iterator it = m_mapRequests.find( rid ) ;
        if( m_mapRequests.end() != it ) client = it->second.lock() ;
        return client ;
    }
    client_ptr ClientController::findClientForTransaction( const string& transactionId ) {
        client_ptr client ;
        mapTransactions::iterator it = m_mapTransactions.find( transactionId ) ;
        if( m_mapTransactions.end() != it ) client = it->second.lock() ;
        return client ;
    }

    void ClientController::stop() {
        m_acceptor.cancel() ;
        m_ioservice.stop() ;
        m_thread.join() ;
    }

 }
