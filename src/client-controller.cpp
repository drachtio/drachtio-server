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


#include "sofia-msg.hpp"
#include "client-controller.hpp"
#include "controller.hpp"

namespace drachtio {
    
    bool ClientController::RequestSpecifier::matches( sip_t const *sip ) {
        return false ;
    }
 
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
    void ClientController::wants_invites( client_ptr client, const string& matchId, const string& strMatch ) {
        RequestSpecifier spec( client, matchId, strMatch ) ;
        boost::lock_guard<boost::mutex> l( m_lock ) ;
        m_request_types.insert( map_of_request_types::value_type("invite", spec)) ;    
    }

    bool ClientController::route_request( sip_t const *sip, string& msgId ) {

        //TODO: search invite list, applying regex until we find one that matches.
        //Note: we should also round robin through these, so need to keep previous used distance
        //Note: need to clear out entries with weak pointers that give null
        //return false if no session found, true otherwise
        //stash the leg so we can later use it when we message the controller about how to answer (or not) this leg
        //Q: should we be creating / referencing a SipDialog class here, so we don't pile a lot of code into controller?
        boost::shared_ptr<SofiaMsg> sm = boost::make_shared<SofiaMsg>( sip ) ;

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
            /* does the client want all invites? */
            if( !spec.getMatcher() ) {
                DR_LOG(log_debug) << "Sending invite to app that registered to take all invites" << endl ;
                matchId = spec.getMatchId() ;
                client = spec.client() ;
                break ;
            }

            /* does the client have a regex that matches this invite */
            if( spec.getMatcher()->eval( sm ) ) {
               DR_LOG(log_debug) << "Sending invite to app that registered to take certain invites" << endl ;
                client = spec.client() ;
                matchId = spec.getMatchId() ;
                break;
            } 

            ++it ;

        }
        if( !client ) {
            DR_LOG(log_info) << "No clients found to handle incoming " << sip->sip_request->rq_method_name << " request" << endl ;
           return false ;
        }

        /* we've selected a client for this message */
        string json = sm->str() ;

        DR_LOG(log_debug) << "Sending request to client: " << json << endl; 
        JsonMsg jmsg( json ) ;

        generateUuid( msgId ) ;

        m_ioservice.post( boost::bind(&Client::sendServiceRequest, client, matchId, msgId, json) ) ;
 
        return true ;
    }
    void ClientController::respondToSipRequest( const string& msgId, boost::shared_ptr<JsonMsg> pMsg ) {
         m_pController->getDialogMaker()->respondToSipRequest( msgId, pMsg ) ;
    }

    void ClientController::stop() {
        m_acceptor.cancel() ;
        m_ioservice.stop() ;
        m_thread.join() ;
    }
    
 }