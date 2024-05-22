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

#include <functional>
#include <algorithm>

#include <boost/tokenizer.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio.hpp>

#include "client-controller.hpp"
#include "controller.hpp"

static string emptyString;

namespace drachtio {
    
    // simple tcp server
    ClientController::ClientController( DrachtioController* pController, string& address, unsigned int tcpPort ) :
        ClientController(pController, address, tcpPort, 0, emptyString, emptyString, emptyString, emptyString) {

        DR_LOG(log_debug) << "Client controller initializing with tcp only" ;
    }

    // simple tls server
    ClientController::ClientController( DrachtioController* pController, string& address, unsigned int tlsPort,
        const string& chainFile, const string& certFile, const string& keyFile, const string& dhFile ) :
        ClientController(pController, address, 0, tlsPort, chainFile, certFile, keyFile, dhFile) {

        DR_LOG(log_debug) << "Client controller initializing with tcp only" ;
    }

    // both tcp and tls
    ClientController::ClientController( DrachtioController* pController, string& address, unsigned int tcpPort, unsigned int tlsPort, 
        const string& chainFile, const string& certFile, const string& keyFile, const string& dhFile ) :
        m_pController( pController ),
        m_endpoint_tcp(boost::asio::ip::make_address(address.c_str()), tcpPort),
        m_acceptor_tcp(m_ioservice, m_endpoint_tcp), 
        m_endpoint_tls(boost::asio::ip::make_address(address.c_str()), tlsPort),
        m_acceptor_tls(m_ioservice, m_endpoint_tls), 
        m_context(boost::asio::ssl::context::sslv23),
        m_tcpPort(tcpPort), m_tlsPort(tlsPort) {

        if (0 != tlsPort) {
            m_context.set_options(
                boost::asio::ssl::context::default_workarounds | 
                boost::asio::ssl::context::no_sslv2 | 
                boost::asio::ssl::context::single_dh_use
            );
            
            if (!chainFile.empty()) {
                DR_LOG(log_debug) << "ClientController::ClientController setting tls chain file: " << chainFile  ;
                m_context.use_certificate_chain_file(chainFile.c_str());
                if (!certFile.empty()) {
                    DR_LOG(log_debug) << "ClientController::ClientController setting tls cert file: " << certFile  ;
                    m_context.use_certificate_file(certFile.c_str(), boost::asio::ssl::context::pem);
                }
            }
            else {
                DR_LOG(log_debug) << "ClientController::ClientController setting tls chain file: " << certFile  ;
                m_context.use_certificate_chain_file(certFile.c_str());
            }
            DR_LOG(log_debug) << "ClientController::ClientController setting tls dh file: " << dhFile  ;
            m_context.use_tmp_dh_file(dhFile.c_str());
            DR_LOG(log_debug) << "ClientController::ClientController setting tls private key file: " << keyFile  ;
            m_context.use_private_key_file(keyFile.c_str(), boost::asio::ssl::context::pem);

            //m_context.set_verify_mode(boost::asio::ssl::verify_none);
        }
        DR_LOG(log_debug) << "ClientController::ClientController done setting tls options: ";
    }

    void ClientController::start() {
        DR_LOG(log_debug) << "Client controller thread id: " << std::this_thread::get_id()  ;
        srand (time(NULL));    
        std::thread t(&ClientController::threadFunc, this) ;
        m_thread.swap( t ) ;
            
        if (m_tcpPort) start_accept_tcp() ;
        if (m_tlsPort) start_accept_tls() ;
    }

    ClientController::~ClientController() {
        stop() ;
    }
    void ClientController::threadFunc() {
        
        DR_LOG(log_debug) << "Client controller thread id: " << std::this_thread::get_id()  ;
         
        /* to make sure the event loop doesn't terminate when there is no work to do */
        boost::asio::io_context::work work(m_ioservice);
        
        for(;;) {
            
            try {
                DR_LOG(log_notice) << "ClientController::threadFunc - ClientController: io_context run loop started (or restarted)"  ;
                m_ioservice.run() ;
                DR_LOG(log_notice) << "ClientController::threadFunc - ClientController: io_context run loop ended normally"  ;
                break ;
            }
            catch( std::exception& e) {
                DR_LOG(log_error) << "ClientController::threadFunc - Error in event thread: " << string( e.what() )  ;
            }
        }
    }
    void ClientController::join( client_ptr client ) {
        m_clients.insert( client ) ;
        client_weak_ptr p( client ) ;
        DR_LOG(log_info) << "ClientController::join - Added client, count of connected clients is now: " << m_clients.size()  ;       
    }
    void ClientController::leave( client_ptr client ) {
        m_clients.erase( client ) ;
        time_t duration = client->getConnectionDuration();
        DR_LOG(log_info) << "ClientController::leave - Removed client, connection duration " << std::dec << 
            duration << " seconds, count of connected clients is now: " << m_clients.size()  ;
    }
    void ClientController::outboundFailed( const string& transactionId ) {
      string headers, body;
      if((!m_pController->getDialogController()->respondToSipRequest( "", transactionId, "SIP/2.0 480 Temporarily Unavailable", 
        headers, body) )) {
        DR_LOG(log_error) << "ClientController::outboundFailed - error sending 480 for transactionId: " << transactionId ;
      }
    }
    void ClientController::outboundReady( client_ptr client, const string& transactionId ) {
      int rc = m_pController->getPendingRequestController()->routeNewRequestToClient(client, transactionId) ;
      if( rc ) {
        DR_LOG(log_error) << "ClientController::outboundReady - error routing over outbound connection transactionId: " << transactionId ;
        return outboundFailed( transactionId);
      }
    }

    void ClientController::addNamedService( client_ptr client, string& strAppName ) {
        //TODO: should we be locking here?  need to review entire locking strategy for this class
        client_weak_ptr p( client ) ;
        m_services.insert( map_of_services::value_type(strAppName,p)) ;       
    }

	void ClientController::start_accept_tcp() {
        DR_LOG(log_debug) << "ClientController::start_accept_tcp"   ;
        Client<socket_t>* p = new Client<socket_t>(m_ioservice, *this);
		client_ptr new_session(p) ;
		m_acceptor_tcp.async_accept( p->socket(), std::bind(&ClientController::accept_handler_tcp, shared_from_this(), new_session, std::placeholders::_1));
    }
	void ClientController::accept_handler_tcp( client_ptr session, const boost::system::error_code& ec) {
        DR_LOG(log_debug) << "ClientController::accept_handler_tcp - got connection" ;       
        if(!ec) session->start() ;
        start_accept_tcp(); 
    }

	void ClientController::start_accept_tls() {
        DR_LOG(log_debug) << "ClientController::start_accept_tls"   ;
        Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>* p = new Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>(m_ioservice, m_context, *this);
		client_ptr new_session(p) ;
		m_acceptor_tls.async_accept( p->socket().lowest_layer(), std::bind(&ClientController::accept_handler_tls, shared_from_this(), new_session, std::placeholders::_1));
    }
	void ClientController::accept_handler_tls( client_ptr session, const boost::system::error_code& ec) {
        DR_LOG(log_debug) << "ClientController::accept_handler_tls - got connection" ;       
        if(!ec) session->start() ;
        start_accept_tls(); 
    }

    void ClientController::makeOutboundConnection( const string& transactionId, const string& host, const string& port, const string& transport ) {
        if (0 == transport.compare("tls")) {
            Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>* p =  new Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>( m_ioservice, m_context, *this, transactionId, host, port ) ;
            client_ptr new_session(p) ;
            p->async_connect() ;
        }
        else {
            Client<socket_t>* p =  new Client<socket_t>( m_ioservice, *this, transactionId, host, port ) ;
            client_ptr new_session(p) ;
            p->async_connect() ;
        }
    }

    void ClientController::selectClientForTag(const string& transactionId, const string& tag) {
        string method;
        if (!m_pController->getPendingRequestController()->getMethodForRequest(transactionId, method)) {
            DR_LOG(log_error) << "ClientController::selectClientForTag - unable to find transactionId: " << transactionId ;
            return outboundFailed(transactionId);        
        }

        DR_LOG(log_debug) << "ClientController::selectClientForTag - searching for client to handle " << method << " with tag " << tag;
        client_ptr client = selectClientForRequestOutsideDialog(method.c_str(), tag.c_str());
        if (!client) {
            DR_LOG(log_error) << "ClientController::selectClientForTag - no clients registered for tag: " << tag ;
            return outboundFailed(transactionId);                
        }
        int rc = m_pController->getPendingRequestController()->routeNewRequestToClient(client, transactionId) ;
        if (rc) {
            DR_LOG(log_error) << "ClientController::selectClientForTag - error routing request to client: " << transactionId ;
            return outboundFailed(transactionId);
        }
    }


    bool ClientController::wants_requests( client_ptr client, const string& verb ) {
        RequestSpecifier spec( client ) ;
        std::lock_guard<std::mutex> l( m_lock ) ;
        m_request_types.insert( map_of_request_types::value_type(verb, spec)) ;  
        DR_LOG(log_debug) << "Added client for " << verb << " requests"  ;

        //initialize the offset if this is the first client registering for that verb
        map_of_request_type_offsets::iterator it = m_map_of_request_type_offsets.find( verb ) ;
        if( m_map_of_request_type_offsets.end() == it ) m_map_of_request_type_offsets.insert(map_of_request_type_offsets::value_type(verb,0)) ;

        //TODO: validate the verb is supported
        return true ;  
    }


    bool ClientController::no_longer_wants_requests( client_ptr client, const string& verb ) {
        RequestSpecifier spec( client ) ;
        std::lock_guard<std::mutex> l( m_lock ) ;
        // Remove all instances of this client for this verb
        for (map_of_request_types::iterator it = m_request_types.begin(); it != m_request_types.end(); ) {
            if (it->second.client() == client) {
                it = m_request_types.erase(it);
            }
            else {
                ++it;
            }
        }
        DR_LOG(log_info) << "Removed client for " << verb << " requests"  ;
        return true ;  
    }

    client_ptr ClientController::selectClientForRequestOutsideDialog(const char* keyword, const char* tag) {
        string method_name = keyword ;
        transform(method_name.begin(), method_name.end(), method_name.begin(), ::tolower);

        /* round robin select a client that has registered for this request type (and, optionally, tag)*/
        std::lock_guard<std::mutex> l( m_lock ) ;
        client_ptr client ;
        string matchId ;
        pair<map_of_request_types::iterator,map_of_request_types::iterator> pair = m_request_types.equal_range(method_name) ;
        unsigned int nPossibles = std::distance(pair.first, pair.second) ;
        if( 0 == nPossibles ) {
            if( 0 == method_name.find("cdr") ) {
                DR_LOG(log_debug) << "No connected clients found to handle incoming " << method_name << " request"  ;
            }
            else {
                DR_LOG(log_info) << "No connected clients found to handle incoming " << method_name << " request"  ;
            }
           return client ;           
        }

        unsigned int nOffset = 0 ;
        map_of_request_type_offsets::const_iterator itOffset = m_map_of_request_type_offsets.find(method_name) ;
        if( m_map_of_request_type_offsets.end() != itOffset ) {
            unsigned int i = itOffset->second;
            if( i < nPossibles ) nOffset = i ;
            else nOffset = 0;
        }
        DR_LOG(log_debug) << "ClientController::selectClientForRequestOutsideDialog - there are " << nPossibles << 
            " possible clients, we are starting with offset " << nOffset  ;

        m_map_of_request_type_offsets.erase(itOffset) ;
        m_map_of_request_type_offsets.insert(map_of_request_type_offsets::value_type(method_name, nOffset + 1)) ;

        unsigned int nTries = 0 ;
        map_of_request_types::iterator it = pair.first ;
        std::advance(it, nOffset) ;
        do {
            if (it == pair.second) it = pair.first;
    
            RequestSpecifier& spec = it->second ;
            client = spec.client() ;
            if (!client) {
                DR_LOG(log_debug) << "ClientController::route_request_outside_dialog - Removing disconnected client while iterating"  ;
                it = m_request_types.erase( it ) ;
                //pair = m_request_types.equal_range(method_name) ;
                //nOffset = 0 ;
                //nPossibles = std::distance( pair.first, pair.second ) ;
            }
            else if (tag && !client->hasTag(tag)) {
                DR_LOG(log_debug) << "ClientController::route_request_outside_dialog - client at offset " << nOffset << " does not support tag " << tag;
                client = NULL;
                it++;
            }
            else {
                DR_LOG(log_debug) << "ClientController::route_request_outside_dialog - Selected client at offset " << nOffset  ;                
            }
            nPossibles--;
        } while( !client && nPossibles > 0 ) ;

        if( !client ) {
            DR_LOG(log_info) << "ClientController::route_request_outside_dialog - No clients found to handle incoming " << method_name << " request"  ;
            return client ;
        }
 
        return client ;
    }
    bool ClientController::route_ack_request_inside_dialog( const string& rawSipMsg, const SipMsgData_t& meta, nta_incoming_t* prack, 
        sip_t const *sip, const string& transactionId, const string& inviteTransactionId, const string& dialogId ) {

        client_ptr client = this->findClientForDialog( dialogId );
        if( !client ) {
            client = this->findClientForNetTransaction( inviteTransactionId );
            if( !client ) {
               DR_LOG(log_debug) << "ClientController::route_ack_request_inside_dialog - client managing dialog has disconnected, or the call was rejected as part of outbound request handler: " << dialogId  ;            
                return false ;
            }
        }

        void (BaseClient::*fn)(const string&, const string&, const string&, const SipMsgData_t&) = &BaseClient::sendSipMessageToClient;
        m_ioservice.post( std::bind(fn, client, transactionId, dialogId, rawSipMsg, meta) ) ;

        this->removeNetTransaction( inviteTransactionId ) ;
        DR_LOG(log_debug) << "ClientController::route_ack_request_inside_dialog - removed incoming invite transaction, map size is now: " << m_mapNetTransactions.size() << " request"  ;
 
        return true ;

    }
    bool ClientController::route_request_inside_invite( const string& rawSipMsg, const SipMsgData_t& meta, nta_incoming_t* prack, sip_t const *sip, 
        const string& transactionId, const string& dialogId  ) {
        client_ptr client = this->findClientForDialog( dialogId );
        if( !client ) {
            client = this->findClientForNetTransaction( transactionId );
            if( !client ) {
                DR_LOG(log_warning) << "ClientController::route_response_inside_invite - client managing transaction has disconnected: " << transactionId  ;
                return false ;
            }
        }
 
        DR_LOG(log_debug) << "ClientController::route_response_inside_invite - sending response to client"  ;
        void (BaseClient::*fn)(const string&, const string&, const string&, const SipMsgData_t&) = &BaseClient::sendSipMessageToClient;
        m_ioservice.post( std::bind(fn, client, transactionId, dialogId, rawSipMsg, meta) ) ;

        return true ;
    }

    bool ClientController::route_request_inside_dialog( const string& rawSipMsg, const SipMsgData_t& meta, sip_t const *sip, 
        const string& transactionId, const string& dialogId ) {
        client_ptr client = this->findClientForDialog( dialogId );
        string method_name = sip->sip_request->rq_method_name ;
        bool isBye = 0 == method_name.compare("BYE");
        bool isFinalNotifyForSubscribe = sip_method_notify == sip->sip_request->rq_method && 
            NULL != sip->sip_subscription_state && 
            NULL != sip->sip_subscription_state->ss_substate &&
            NULL != strstr(sip->sip_subscription_state->ss_substate, "terminated") &&
            (NULL == sip->sip_event || 
                (sip->sip_event->o_type && !std::strstr(sip->sip_event->o_type, "refer") && !std::strstr(sip->sip_event->o_type, "REFER"))
            );
    
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_request_inside_dialog - client managing dialog has disconnected: " << dialogId  ;
            
            // if this is a BYE from the network, it ends the dialog 
            if( isBye || isFinalNotifyForSubscribe) {
                removeDialog( dialogId ) ;
            }
            return false ;
        }
        if (string::npos == transactionId.find("unsolicited")) this->addNetTransaction( client, transactionId ) ;
 
        void (BaseClient::*fn)(const string&, const string&, const string&, const SipMsgData_t&) = &BaseClient::sendSipMessageToClient;
        m_ioservice.post( std::bind(fn, client, transactionId, dialogId, rawSipMsg, meta) ) ;

        // if this is a BYE from the network, it ends the dialog 
        if( isBye || isFinalNotifyForSubscribe) {
            removeDialog( dialogId ) ;
        }

        return true ;
    }

    bool ClientController::route_response_inside_transaction( const string& rawSipMsg, const SipMsgData_t& meta, nta_outgoing_t* orq, sip_t const *sip, 
        const string& transactionId, const string& dialogId ) {
        
        client_ptr client = this->findClientForAppTransaction( transactionId );
        if( !client ) {
            DR_LOG(log_warning) << "ClientController::route_response_inside_transaction - client managing transaction has disconnected: " << transactionId  ;
            removeAppTransaction( transactionId ) ;
            removeDialog( dialogId ) ;
            DR_LOG(log_debug) << "ClientController::route_response_inside_transaction - removed dialog: " << dialogId << ", " <<  m_mapDialogs.size() << " dialogs remain" ;
            return false ;
        }

        void (BaseClient::*fn)(const string&, const string&, const string&, const SipMsgData_t&) = &BaseClient::sendSipMessageToClient;
        m_ioservice.post( std::bind(fn, client, transactionId, dialogId, rawSipMsg, meta) ) ;

        string method_name = sip->sip_cseq->cs_method_name ;
        if( sip->sip_status->st_status >= 200 ) {
            removeAppTransaction( transactionId ) ;
        }

        if( 0 == method_name.compare("BYE") ) {
            removeDialog( dialogId ) ;
        }

        return true ;
    }
    
    void ClientController::addDialogForTransaction( const string& transactionId, const string& dialogId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        mapId2Client::iterator it = m_mapNetTransactions.find( transactionId ) ;
        if( m_mapNetTransactions.end() != it ) {
            m_mapDialogs.insert( mapId2Client::value_type(dialogId, it->second ) ) ;
            DR_LOG(log_info) << "ClientController::addDialogForTransaction - added dialog (uas), now tracking: " << 
                m_mapDialogs.size() << " dialogs and " << m_mapNetTransactions.size() << " net transactions"  ;
         }
        else {
            /* dialog will already exist if we received a reliable provisional response */
            mapId2Client::iterator itDialog = m_mapDialogs.find( dialogId ) ;
            if( m_mapDialogs.end() == itDialog ) {
                mapId2Client::iterator itApp = m_mapAppTransactions.find( transactionId ) ;
                if( m_mapAppTransactions.end() != itApp ) {
                    m_mapDialogs.insert( mapId2Client::value_type(dialogId, itApp->second ) ) ;
                    DR_LOG(log_info) << "ClientController::addDialogForTransaction - added dialog (uac), now tracking: " << 
                        m_mapDialogs.size() << " dialogs and " << m_mapAppTransactions.size() << " app transactions"  ;
                }
                else {
                   DR_LOG(log_error) << "ClientController::addDialogForTransaction - transaction id " << transactionId << 
                    " not found; possible race condition where BYE received during INVITE transaction"  ;
                   return;
                }
            }
        }
        DR_LOG(log_debug) << "ClientController::addDialogForTransaction - transaction id " << transactionId << 
            " has associated dialog " << dialogId  ;

        client_ptr client = this->findClientForDialog_nolock( dialogId );
        if( !client ) {
            m_mapDialogs.erase( dialogId ) ;
            DR_LOG(log_warning) << "ClientController::addDialogForTransaction - client managing dialog has disconnected: " << dialogId  ;
            return  ;
        }
        else {
            string strAppName ;
            if( client->getAppName( strAppName ) ) {
                m_mapDialogId2Appname.insert( mapDialogId2Appname::value_type( dialogId, strAppName ) ) ;
                
                DR_LOG(log_debug) << "ClientController::addDialogForTransaction - dialog id " << dialogId << 
                    " has been established for client app " << strAppName << "; count of tracked dialogs is " << m_mapDialogId2Appname.size()  ;
            }
        }
    } 
    bool ClientController::sendRequestInsideDialog( client_ptr client, const string& clientMsgId, const string& dialogId, const string& startLine, 
        const string& headers, const string& body, string& transactionId ) {

        generateUuid( transactionId ) ;
        if( 0 != startLine.find("ACK") ) {
            addAppTransaction( client, transactionId ) ;
        }

        addApiRequest( client, clientMsgId )  ;
        bool rc = m_pController->getDialogController()->sendRequestInsideDialog( clientMsgId, dialogId, startLine, headers, body, transactionId) ;
        return rc ;
    }
    bool ClientController::sendRequestOutsideDialog( client_ptr client, const string& clientMsgId, const string& startLine, const string& headers, 
            const string& body, string& transactionId, string& dialogId, string& routeUrl ) {

        generateUuid( transactionId ) ;
        if( 0 != startLine.find("ACK") ) {
            addAppTransaction( client, transactionId ) ;
        }

        addApiRequest( client, clientMsgId )  ;
        bool rc = m_pController->getDialogController()->sendRequestOutsideDialog( clientMsgId, startLine, headers, body, transactionId, dialogId, routeUrl) ;
        return rc ;        
    }
    bool ClientController::respondToSipRequest( client_ptr client, const string& clientMsgId, const string& transactionId, const string& startLine, const string& headers, 
        const string& body ) {

        addApiRequest( client, clientMsgId )  ;
        bool rc = m_pController->getDialogController()->respondToSipRequest( clientMsgId, transactionId, startLine, headers, body ) ;
        return rc ;               
    }   
    bool ClientController::sendCancelRequest( client_ptr client, const string& clientMsgId, const string& transactionId, const string& startLine, const string& headers, 
        const string& body ) {

        addApiRequest( client, clientMsgId )  ;
        bool rc = m_pController->getDialogController()->sendCancelRequest( clientMsgId, transactionId, startLine, headers, body ) ;
        return rc ;               
    }
    bool ClientController::proxyRequest( client_ptr client, const string& clientMsgId, const string& transactionId, 
        bool recordRoute, bool fullResponse, bool followRedirects, bool simultaneous, const string& provisionalTimeout, 
        const string& finalTimeout, const vector<string>& vecDestination, const string& headers ) {
        addApiRequest( client, clientMsgId )  ;
        m_pController->getProxyController()->proxyRequest( clientMsgId, transactionId, recordRoute, fullResponse, followRedirects, 
            simultaneous, provisionalTimeout, finalTimeout, vecDestination, headers ) ;
        removeNetTransaction( transactionId ) ;
        return true;
    }
    bool ClientController::route_api_response( const string& clientMsgId, const string& responseText, const string& additionalResponseData ) {
        if( clientMsgId.empty() ) {
            return true ;
        }
       client_ptr client = this->findClientForApiRequest( clientMsgId );
        if( !client ) {
            removeApiRequest( clientMsgId ) ;
            DR_LOG(log_warning) << "ClientController::route_api_response - client that has sent the request has disconnected: " << clientMsgId  ;
            return false ;             
        }
        if( string::npos == additionalResponseData.find("|continue") ) {
            removeApiRequest( clientMsgId ) ;
        }
        m_ioservice.post( std::bind(&BaseClient::sendApiResponseToClient, client, clientMsgId, responseText, additionalResponseData) ) ;
        return true ;                
    }
    
    void ClientController::removeDialog( const string& dialogId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        mapId2Client::iterator it = m_mapDialogs.find( dialogId ) ;
        if( m_mapDialogs.end() == it ) {
            DR_LOG(log_warning) << "ClientController::removeDialog - dialog not found: " << dialogId  ;
            return ;
        }
        m_mapDialogs.erase( it ) ;
        DR_LOG(log_info) << "ClientController::removeDialog - after removing dialogs count is now: " << m_mapDialogs.size()  ;
    }
    client_ptr ClientController::findClientForDialog( const string& dialogId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        return findClientForDialog_nolock( dialogId ) ;
    }

    client_ptr ClientController::findClientForDialog_nolock( const string& dialogId ) {
        client_ptr client ;

        mapId2Client::iterator it = m_mapDialogs.find( dialogId ) ;
        if( m_mapDialogs.end() != it ) client = it->second.lock() ;

        // if that client is no longer connected, randomly select another client that is running that app 
        if( !client ) {
            mapDialogId2Appname::iterator it = m_mapDialogId2Appname.find( dialogId ) ;
            if( m_mapDialogId2Appname.end() != it ) {
                string appName = it->second ;
                DR_LOG(log_info) << "Attempting to find another client for app " << appName  ;

                pair<map_of_services::iterator,map_of_services::iterator> pair = m_services.equal_range( appName ) ;
                unsigned int nPossibles = std::distance( pair.first, pair.second ) ;
                if( 0 == nPossibles ) {
                   DR_LOG(log_warning) << "No other clients found for app " << appName  ;
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

                if( !client ) DR_LOG(log_warning) << "No other connected clients found for app " << appName  ;
                else DR_LOG(log_info) << "Found alternative client for app " << appName << " " << nOffset << ":" << nPossibles  ;
            }
        }
        return client ;
    }

    client_ptr ClientController::findClientForAppTransaction( const string& transactionId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        client_ptr client ;
        mapId2Client::iterator it = m_mapAppTransactions.find( transactionId ) ;
        if( m_mapAppTransactions.end() != it ) client = it->second.lock() ;
        return client ;
    }
    client_ptr ClientController::findClientForNetTransaction( const string& transactionId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        client_ptr client ;
        mapId2Client::iterator it = m_mapNetTransactions.find( transactionId ) ;
        if( m_mapNetTransactions.end() != it ) client = it->second.lock() ;
        return client ;
    }
    client_ptr ClientController::findClientForApiRequest( const string& clientMsgId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        client_ptr client ;
        mapId2Client::iterator it = m_mapApiRequests.find( clientMsgId ) ;
        if( m_mapApiRequests.end() != it ) client = it->second.lock() ;
        return client ;
    }
    void ClientController::removeAppTransaction( const string& transactionId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        m_mapAppTransactions.erase( transactionId ) ;        
        DR_LOG(log_debug) << "ClientController::removeAppTransaction: transactionId " << transactionId << "; size: " << m_mapAppTransactions.size()  ;
    }
    void ClientController::removeNetTransaction( const string& transactionId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        m_mapNetTransactions.erase( transactionId ) ;        
        DR_LOG(log_debug) << "ClientController::removeNetTransaction: transactionId " << transactionId << "; size: " << m_mapNetTransactions.size()  ;
    }
    void ClientController::removeApiRequest( const string& clientMsgId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        m_mapApiRequests.erase( clientMsgId ) ;   
        DR_LOG(log_debug) << "ClientController::removeApiRequest: clientMsgId " << clientMsgId << "; size: " << m_mapApiRequests.size()  ;
    }
    void ClientController::addAppTransaction( client_ptr client, const string& transactionId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        m_mapAppTransactions.insert( make_pair( transactionId, client ) ) ;        
        DR_LOG(log_debug) << "ClientController::addAppTransaction: transactionId " << transactionId << "; size: " << m_mapAppTransactions.size()  ;
    }
    void ClientController::addNetTransaction( client_ptr client, const string& transactionId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        m_mapNetTransactions.insert( make_pair( transactionId, client ) ) ;        
        DR_LOG(log_debug) << "ClientController::addNetTransaction: transactionId " << transactionId << "; size: " << m_mapNetTransactions.size()  ;
    }
    void ClientController::addApiRequest( client_ptr client, const string& clientMsgId ) {
        std::lock_guard<std::mutex> l( m_lock ) ;
        m_mapApiRequests.insert( make_pair( clientMsgId, client ) ) ;        
        DR_LOG(log_debug) << "ClientController::addApiRequest: clientMsgId " << clientMsgId << "; size: " << m_mapApiRequests.size()  ;
    }

    void ClientController::logStorageCount(bool bDetail) {
        std::lock_guard<std::mutex> lock(m_lock) ;

        DR_LOG(bDetail ? log_info : log_debug) << "ClientController storage counts"  ;
        DR_LOG(bDetail ? log_info : log_debug) << "----------------------------------"  ;
        DR_LOG(bDetail ? log_info : log_debug) << "m_clients size:                                                  " << m_clients.size()  ;
        DR_LOG(bDetail ? log_info : log_debug) << "m_services size:                                                 " << m_services.size()  ;
        DR_LOG(bDetail ? log_info : log_debug) << "m_request_types size:                                            " << m_request_types.size()  ;
        DR_LOG(bDetail ? log_info : log_debug) << "m_map_of_request_type_offsets size:                              " << m_map_of_request_type_offsets.size()  ;
        DR_LOG(bDetail ? log_info : log_debug) << "m_mapDialogs size:                                               " << m_mapDialogs.size()  ;
        if (bDetail) {
            for (const auto& kv : m_mapDialogs) {
                DR_LOG(bDetail ? log_info : log_debug) << "    dialog id: " << std::hex << (kv.first).c_str();
            }
        }

        DR_LOG(bDetail ? log_info : log_debug) << "m_mapNetTransactions size:                                       " << m_mapNetTransactions.size()  ;
        if (bDetail) {
            for (const auto& kv : m_mapNetTransactions) {
                DR_LOG(bDetail ? log_info : log_debug) << "    transaction id: " << std::hex << (kv.first).c_str();
            }
        }
        DR_LOG(bDetail ? log_info : log_debug) << "m_mapAppTransactions size:                                       " << m_mapAppTransactions.size()  ;
        if (bDetail) {
            for (const auto& kv : m_mapAppTransactions) {
                DR_LOG(bDetail ? log_info : log_debug) << "    transaction id: " << std::hex << (kv.first).c_str();
            }
        }
        DR_LOG(bDetail ? log_info : log_debug) << "m_mapApiRequests size:                                           " << m_mapApiRequests.size()  ;
        if (bDetail) {
            for (const auto& kv : m_mapApiRequests) {
                DR_LOG(bDetail ? log_info : log_debug) << "    client msg id: " << std::hex << (kv.first).c_str();
            }
        }
        DR_LOG(bDetail ? log_info : log_debug) << "m_mapDialogId2Appname size:                                      " << m_mapDialogId2Appname.size()  ;
        if (bDetail) {
            for (const auto& kv : m_mapDialogId2Appname) {
                DR_LOG(bDetail ? log_info : log_debug) << "    dialog id: " << std::hex << (kv.first).c_str();
            }
        }

        STATS_GAUGE_SET(STATS_GAUGE_CLIENT_APP_CONNECTIONS, m_clients.size())

    }
    std::shared_ptr<SipDialogController> ClientController::getDialogController(void) {
        return m_pController->getDialogController();
    }

    void ClientController::stop() {
        m_acceptor_tcp.cancel() ;
        m_acceptor_tls.cancel() ;
        m_ioservice.stop() ;
        m_thread.join() ;
    }

 }
