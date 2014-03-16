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

#include "client-controller.hpp"
#include "controller.hpp"
#include "sofia-msg.hpp"

namespace drachtio {
     
    ClientController::ClientController( DrachtioController* pController, string& address, unsigned int port ) :
        m_pController( pController ),
        m_endpoint(  boost::asio::ip::tcp::v4(), port ),
        m_acceptor( m_ioservice, m_endpoint ) {
            
        srand (time(NULL));    
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
        DR_LOG(log_debug) << "Added client, count of connected clients is now: " << m_clients.size() << endl ;       
    }
    void ClientController::leave( client_ptr client ) {
        m_clients.erase( client ) ;
        DR_LOG(log_debug) << "Removed client, count of connected clients is now: " << m_clients.size() << endl ;
    }
    void ClientController::addNamedService( client_ptr client, string& strAppName ) {
        //TODO: should we be locking here?  need to review entire locking strategy for this class
        client_weak_ptr p( client ) ;
        m_services.insert( map_of_services::value_type(strAppName,p)) ;       
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

        //initialize the offset if this is the first client registering for that verb
        map_of_request_type_offsets::iterator it = m_map_of_request_type_offsets.find( verb ) ;
        if( m_map_of_request_type_offsets.end() == it ) m_map_of_request_type_offsets.insert(map_of_request_type_offsets::value_type(verb,0)) ;

        //TODO: validate the verb is supported
        return true ;  
    }

    ///NB: route_XXX handles incoming messages from the network
    bool ClientController::route_request_outside_dialog( nta_incoming_t* irq, sip_t const *sip, const string& transactionId ) {

        //TOD: this constructor jsonifies the message, which we would like to move out of this (sip stack) thread
        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( irq, sip ) ;

        string method_name = sip->sip_request->rq_method_name ;
        transform(method_name.begin(), method_name.end(), method_name.begin(), ::tolower);

        /* round robin select a client that has registered for this request type */
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        client_ptr client ;
        string matchId ;
        pair<map_of_request_types::iterator,map_of_request_types::iterator> pair = m_request_types.equal_range( method_name ) ;
        unsigned int nPossibles = std::distance( pair.first, pair.second ) ;
        if( 0 == nPossibles ) {
            DR_LOG(log_info) << "No connected clients found to handle incoming " << method_name << " request" << endl ;
           return false ;           
        }

        unsigned int nOffset = 0 ;
        map_of_request_type_offsets::const_iterator itOffset = m_map_of_request_type_offsets.find( method_name ) ;
        if( m_map_of_request_type_offsets.end() != itOffset ) {
            unsigned int i = itOffset->second;
            if( i < nPossibles ) nOffset = i ;
            else nOffset = 0;
        }
        DR_LOG(log_debug) << "ClientController::route_request_outside_dialog - there are " << nPossibles << 
            " possible clients, we are starting with offset " << nOffset << endl ;

        m_map_of_request_type_offsets.erase( itOffset ) ;
        m_map_of_request_type_offsets.insert(map_of_request_type_offsets::value_type(method_name, nOffset + 1)) ;

        unsigned int nTries = 0 ;
        do {
            map_of_request_types::iterator it = pair.first ;
            std::advance( it, nOffset) ;
            RequestSpecifier& spec = it->second ;
            client = spec.client() ;
            if( !client ) {
                DR_LOG(log_debug) << "Removing disconnected client while iterating" << endl ;
                m_request_types.erase( it ) ;
                pair = m_request_types.equal_range( method_name ) ;
                if( nOffset >= m_request_types.size() ) {
                    nOffset = m_request_types.size() - 1 ;
                }
                DR_LOG(log_debug) << "Offset has been set to " << nOffset << " size of range is " << m_request_types.size() << endl ;
            }
            else {
                DR_LOG(log_debug) << "Selected client at offset " << nOffset << endl ;                
            }
        } while( !client && ++nTries < nPossibles ) ;

        if( !client ) {
            DR_LOG(log_info) << "No clients found to handle incoming " << method_name << " request" << endl ;
           return false ;
        }

        m_mapTransactions.insert( mapTransactions::value_type(transactionId,client)) ;
        DR_LOG(log_info) << "ClientController::route_request_outside_dialog - added invite transaction, map size is now: " << m_mapTransactions.size() << " request" << endl ;
 
        m_ioservice.post( boost::bind(&Client::sendRequestOutsideDialog, client, transactionId, sm) ) ;
 
        return true ;
    }

    //client has sent us a response to an incoming request from the network
    void ClientController::respondToSipRequest( const string& transactionId, boost::shared_ptr<JsonMsg> pMsg ) {
         m_pController->getDialogController()->respondToSipRequest( transactionId, pMsg ) ;
    }

    bool ClientController::route_request_inside_dialog( nta_incoming_t* irq, sip_t const *sip, const string& transactionId, const string& dialogId ) {
        client_ptr client = this->findClientForDialog( dialogId );
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_request_inside_dialog - client managing dialog has disconnected: " << dialogId << endl ;
            
            //TODO: try to find another client providing the same service
            return false ;
        }

        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( irq, sip ) ;
 
        m_ioservice.post( boost::bind(&Client::sendRequestInsideDialog, client, transactionId, dialogId, sm) ) ;

        /* if this is a BYE from the network, it ends the dialog */
        string method_name = sip->sip_request->rq_method_name ;
        if( 0 == method_name.compare("BYE") ) {
            removeDialog( dialogId ) ;
        }

        return true ;
    }
    bool ClientController::route_ack_request_inside_dialog( nta_incoming_t* irq, sip_t const *sip, const string& transactionId, const string& inviteTransactionId, const string& dialogId ) {
        client_ptr client = this->findClientForDialog( dialogId );
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_ack_request_inside_dialog - client managing dialog has disconnected: " << dialogId << endl ;
 
           //TODO: try to find another client providing the same service
             return false ;
        }

        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( irq, sip ) ;

        m_ioservice.post( boost::bind(&Client::sendAckRequestInsideDialog, client, transactionId, inviteTransactionId, dialogId, sm) ) ;

        return true ;
    }

    bool ClientController::route_response_inside_transaction( nta_outgoing_t* orq, sip_t const *sip, const string& transactionId, const string& dialogId ) {
        client_ptr client = this->findClientForTransaction( transactionId );
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_response_inside_transaction - client managing transaction has disconnected: " << transactionId << endl ;
            return false ;
        }

        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( orq, sip, true ) ;
 
        m_ioservice.post( boost::bind(&Client::sendResponseInsideTransaction, client, transactionId, dialogId, sm) ) ;

        string method_name = sip->sip_cseq->cs_method_name ;
        if( 0 == method_name.compare("BYE") ) {
            removeDialog( dialogId ) ;
        }

        return true ;
    }
    void ClientController::addDialogForTransaction( const string& transactionId, const string& dialogId ) {
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        mapTransactions::iterator it = m_mapTransactions.find( transactionId ) ;
        if( m_mapTransactions.end() != it ) {
            m_mapDialogs.insert( mapDialogs::value_type(dialogId, it->second ) ) ;
            DR_LOG(log_warning) << "ClientController::addDialogForTransaction - added dialog, now tracking: " << 
                m_mapDialogs.size() << "dialogs and " << m_mapTransactions.size() << " transactions" << endl ;
         }
        else {
            DR_LOG(log_error) << "ClientController::addDialogForTransaction - transaction id " << transactionId << " not found" << endl ;
            assert(false) ;
        }
        DR_LOG(log_debug) << "ClientController::addDialogForTransaction - transaction id " << transactionId << 
            " has associated dialog " << dialogId << endl ;

        client_ptr client = this->findClientForDialog_nolock( dialogId );
        if( !client ) {
            m_mapDialogs.erase( dialogId ) ;
            m_mapTransactions.erase( transactionId ) ;
            DR_LOG(log_warning) << "ClientController::addDialogForTransaction - client managing dialog has disconnected: " << dialogId << endl ;
            return  ;
        }
        else {
            string strAppName ;
            if( client->getAppName( strAppName ) ) {
                m_mapDialogId2Appname.insert( mapDialogId2Appname::value_type( dialogId, strAppName ) ) ;
                
                DR_LOG(log_debug) << "ClientController::addDialogForTransaction - dialog id " << dialogId << 
                    " has been established for client app " << strAppName << "; count of tracked dialogs is " << m_mapDialogId2Appname.size() << endl ;
            }
        }
    } 
    bool ClientController::sendSipRequest( client_ptr client, boost::shared_ptr<JsonMsg> pMsg, const string& rid ) {
        ostringstream o ;
        m_mapRequests.insert( mapRequests::value_type( rid, client)) ;   
        const char* dialogId = NULL ;
        json_error_t err ;
        if( 0 >  json_unpack_ex( pMsg->value(), &err, 0, "{s:{s?s}}","data","dialogId",&dialogId) ) {
            DR_LOG(log_error) << "ClientController::sendSipRequest failed parsing dialogId from json message: " << err.text << endl ;
            return false ;
        }
        if( dialogId ) {
            if( m_pController->sendRequestInsideDialog( pMsg, rid ) < 0 ) {
                json_t* data = json_pack("{s:b,s:s}","success",false,"reason","unknown sip dialog") ;
                client->sendResponse( rid, data ) ;
                return false ;
            }
            return true ;            
        }
        else {
            const char *method=NULL, *transactionId=NULL ;
            if( 0 > json_unpack_ex( pMsg->value(), &err, 0, "{s:{s?s,s:{s?s}}}","data","transactionId",&transactionId, "message","method",&method) ) {
                DR_LOG(log_error) << "ClientController::sendSipRequest failed parsing transactionId from json message: " << err.text << endl ;
                return false ;       
            }
            if( 0 == strcmp( method,"CANCEL") ) {
                 return m_pController->getDialogController()->sendCancelRequest( pMsg, rid ) ;
            }
            return m_pController->getDialogController()->sendRequestOutsideDialog( pMsg, rid ) ;        
        }
    }
    void ClientController::sendResponseToClient( const string& rid, json_t* data ) {
        string null;
        sendResponseToClient( rid, data, null) ;
    }
    void ClientController::sendResponseToClient( const string& rid, json_t* data, const string& transactionId ) {
        client_ptr client = findClientForRequest( rid ) ;
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::sendResponseToClient - client that sent the request has disconnected: " << rid << endl ;
            return ;
        }
        m_ioservice.post( boost::bind(&Client::sendResponse, client, rid, data) ) ;   
        if( !transactionId.empty() ) {
            boost::lock_guard<boost::mutex> l( m_lock ) ;
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
        string json ;
        if( !sm->str( json ) ) {
            DR_LOG(log_error) << "ClientController::route_request_inside_invite - Error converting incoming sip message to json" << endl ;
            return false ;            
        }
 
        if( dialogId.length() > 0 )  m_ioservice.post( boost::bind(&Client::sendRequestInsideInviteWithDialog, client, transactionId, dialogId, sm) ) ;
        else m_ioservice.post( boost::bind(&Client::sendRequestInsideInvite, client, transactionId, sm) ) ;

        return true ;
    }
    bool ClientController::route_event_inside_dialog( const string& event, const string& transactionId, const string& dialogId ) {
        client_ptr client = this->findClientForDialog( dialogId );
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_event_inside_dialog - client managing dialog has disconnected: " << dialogId << endl ;
            return false ;
        }

        m_ioservice.post( boost::bind(&Client::sendEventInsideDialog, client, transactionId, dialogId, event) ) ;

        return true ;
    }

    void ClientController::removeDialog( const string& dialogId ) {
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        mapDialogs::iterator it = m_mapDialogs.find( dialogId ) ;
        if( m_mapDialogs.end() == it ) {
            DR_LOG(log_warning) << "ClientController::removeDialog - dialog not found: " << dialogId << endl ;
            return ;
        }
        m_mapDialogs.erase( it ) ;
        DR_LOG(log_info) << "ClientController::removeDialog - after removing dialogs count is now: " << m_mapDialogs.size() << endl ;
    }
    client_ptr ClientController::findClientForDialog( const string& dialogId ) {
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        return findClientForDialog_nolock( dialogId ) ;
    }

    client_ptr ClientController::findClientForDialog_nolock( const string& dialogId ) {
        client_ptr client ;

        mapDialogs::iterator it = m_mapDialogs.find( dialogId ) ;
        if( m_mapDialogs.end() != it ) client = it->second.lock() ;

        // if that client is no longer connected, randomly select another client that is running that app 
        if( !client ) {
            mapDialogId2Appname::iterator it = m_mapDialogId2Appname.find( dialogId ) ;
            if( m_mapDialogId2Appname.end() != it ) {
                string appName = it->second ;
                DR_LOG(log_info) << "Attempting to find another client for app " << appName << endl ;

                pair<map_of_services::iterator,map_of_services::iterator> pair = m_services.equal_range( appName ) ;
                unsigned int nPossibles = std::distance( pair.first, pair.second ) ;
                if( 0 == nPossibles ) {
                   DR_LOG(log_warning) << "No other clients found for app " << appName << endl ;
                   return client ;
                }
                unsigned int nOffset = rand() % nPossibles ;
                unsigned int nTries = 0 ;
                do {
                    map_of_services::iterator itTemp = pair.first ;
                    std::advance( itTemp, nOffset) ;
                    client = itTemp->second.lock() ;
                    if( !client ) {
                        if( ++nOffset == nPossibles ) nOffset = 0 ;
                    }
                } while( !client && ++nTries < nPossibles ) ;

                if( !client ) DR_LOG(log_warning) << "No other connected clients found for app " << appName << endl ;
                else DR_LOG(log_info) << "Found alternative client for app " << appName << " " << nOffset << ":" << nPossibles << endl ;
            }
        }
        return client ;
    }

    client_ptr ClientController::findClientForRequest( const string& rid ) {
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        client_ptr client ;
        mapRequests::iterator it = m_mapRequests.find( rid ) ;
        if( m_mapRequests.end() != it ) client = it->second.lock() ;
        return client ;
    }
    client_ptr ClientController::findClientForTransaction( const string& transactionId ) {
        boost::lock_guard<boost::mutex> l( m_lock ) ;
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
