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
//
#include <iostream>

#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "client.hpp"
#include "controller.hpp"

namespace drachtio {
    std::size_t hash_value( Client const &c ) {
        std::size_t seed = 0 ;
        boost::hash_combine( seed, c.const_socket().local_endpoint().address().to_string()  ) ;
        boost::hash_combine( seed, c.const_socket().local_endpoint().port() ) ;
        return seed ;
    }

	Client::Client( boost::asio::io_service& io_service, ClientController& controller ) : m_sock(io_service), m_controller( controller ),  
        m_state(initial), m_buffer(12228), m_nMessageLength(0) {
    }
    Client::~Client() {
    }

    void Client::start() {

        DR_LOG(log_info) << "Received connection from client at " << m_sock.remote_endpoint().address().to_string() << ":" << m_sock.remote_endpoint().port() << endl ;

        /* wait for authentication challenge */
        m_controller.join( shared_from_this() ) ;
        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
                        boost::bind( &Client::read_handler, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred ) ) ;
        
    }
    bool Client::readMessageLength(unsigned int& len) {
        bool continueOn = true ;
        boost::array<char, 6> ch ;
        memset( ch.data(), 0, sizeof(ch)) ;
        unsigned int i = 0 ;
        char c ;
        do {
            c = m_buffer.front() ;
            m_buffer.pop_front() ;
            if( '#' == c ) break ;
            if( !isdigit( c ) ) throw std::runtime_error("Client::readMessageLength - invalid message length specifier") ;
            ch[i++] = c ;
        } while( m_buffer.size() && i < 6 ) ;

        if( 6 == i ) throw std::runtime_error("Client::readMessageLength - invalid message length specifier") ;

        if( 0 == m_buffer.size() ) {
            if( '#' != c ) {
                /* the message was split in the middle of the length specifier - put them back for next time to be read in full once remainder come in */
                for( unsigned int n = 0; n < i; n++ ) m_buffer.push_back( ch[n] ) ;
                len = 0 ;
                return false ;
            }
            continueOn = false ;
        }

        len = boost::lexical_cast<unsigned int>(ch.data()); 
        return continueOn ;
    }

    bool Client::processOneMessage( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse ) {
        bool bDisconnect = false ;
        switch( m_state ) {
            case initial:
                if( this->processAuthentication( pMsg, msgResponse ) ) {
                    m_state = authenticated ;
                }     
                else bDisconnect = true ;    
                break ;              
            default:
                this->processMessage( pMsg, msgResponse, bDisconnect ) ;
                break ;
        }      
        return bDisconnect ;
    }

    void Client::read_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) {

        if( ec ) {
            DR_LOG(log_error) << ec << endl ;
            m_controller.leave( shared_from_this() ) ;
            return ;
        }

        DR_LOG(log_debug) << "Client::read_handler read raw message of " << bytes_transferred << " bytes: " << std::string(m_readBuf.begin(), m_readBuf.begin() + bytes_transferred) << endl ;

        /* append the data to our in-process buffer */
        m_buffer.insert( m_buffer.end(), m_readBuf.begin(),  m_readBuf.begin() + bytes_transferred ) ;

        /* if we're starting a new message, parse the message length */
        if( 0 == m_nMessageLength ) {

            try {
                if( !readMessageLength( m_nMessageLength ) ) {
                    DR_LOG(log_debug) << "Client::read_handler - message was split after message length of " << m_nMessageLength << endl ;                     
                    goto read_again ;
                }
            }
            catch( std::runtime_error& err ) {
                DR_LOG(log_error) << "Client::read_handler client sent invalid message -- JSON message length not specified properly" << endl ;                     
                m_controller.leave( shared_from_this() ) ;               
                return ;
            }
        }

        /* while we have at least one full message, process it */
        while( m_buffer.size() >= m_nMessageLength && m_nMessageLength > 0 ) {
            JsonMsg msgResponse ;
            bool bDisconnect = false ;
            try {
                DR_LOG(log_debug) << "Client::read_handler read JSON: " << std::string( m_buffer.begin(), m_buffer.begin() + m_nMessageLength ) << endl ;
                boost::shared_ptr<JsonMsg> pMsg = boost::make_shared<JsonMsg>( m_buffer.begin(), m_buffer.begin() + m_nMessageLength ) ;
                bDisconnect = processOneMessage( pMsg, msgResponse ) ;
            } catch( std::runtime_error& err ) {
                DR_LOG(log_error) << "Error parsing JSON message: " << std::string( m_buffer.begin(), m_buffer.begin() + m_nMessageLength ) << " : " << err.what() << endl ;
                m_controller.leave( shared_from_this() ) ;
                return ;
            }

            /* send response if indicated */
            if( msgResponse.value() ) {
                string strJson ;
                msgResponse.stringify(strJson) ;
                DR_LOG(log_info) << "Sending " << strJson << endl ;
                boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
            }
            if( bDisconnect ) {
                m_controller.leave( shared_from_this() ) ;
                return ;
            }

            /* reload for next message */
            m_buffer.erase_begin( m_nMessageLength ) ;
            if( m_buffer.size() ) {
                DR_LOG(log_debug) << "Client::read_handler processing follow-on message in read buffer, remaining bytes to process: " << m_buffer.size() << endl ;
                try {
                    if( !readMessageLength( m_nMessageLength ) ) {
                        DR_LOG(log_debug) << "Client::read_handler - message was split after message length of " << m_nMessageLength << endl ;                     
                        break ;
                    }
                }
                catch( std::runtime_error& err ) {
                    DR_LOG(log_error) << "Client::read_handler client sent invalid message -- JSON message length not specified properly" << endl ;                     
                    m_controller.leave( shared_from_this() ) ;               
                    return ;
                }
                DR_LOG(log_debug) << "Client::read_handler follow-on message length of " << m_nMessageLength << " bytes " << endl ; 
            }
            else {
                m_nMessageLength = 0 ;
            }
        }

read_again:
        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
                        boost::bind( &Client::read_handler, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred ) ) ;

       
    }
    void Client::write_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) {
    	DR_LOG(log_debug) << "Wrote " << bytes_transferred << " bytes: " << endl ;
    }

    bool Client::processAuthentication( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse  ) {
        bool bReturn = false ;
        const char *id=NULL, *type=NULL, *command=NULL, *secret=NULL, *appName=NULL ;
        json_error_t err ;
        int rc = json_unpack_ex( pMsg->value(), &err, 0, "{s:s,s:s,s:s,s:{s:s,s?s}}", "rid", &id, "type", &type, "command", &command, 
            "data","secret",&secret,"appName",&appName) ;
        json_t* json = NULL ;
        if( rc < 0 ) {
            DR_LOG(log_error) << "Client::processAuthentication - failed to parse message: " << err.text << endl ;
            bReturn = false ;
            json = json_pack("{s:s,s:s,s:{s:b,s:s}}", "type","response","rid",id,"data","authenticated",false,"reason","invalid auth message") ;
        }
        else if( !(bReturn = theOneAndOnlyController->isSecret( secret ) ) ) {
            json = json_pack("{s:s,s:s,s:{s:b,s:s}}", "type","response","rid",id,"data","authenticated",false,"reason","invalid credentials") ;
        } 
        else {
            string hostport ;
            theOneAndOnlyController->getMyHostport( hostport ) ;
            json = json_pack("{s:s,s:s,s:{s:b,s:s}}", "type","response","rid",id,"data","authenticated",true,"hostport",hostport.c_str()) ;
        }
        msgResponse.set(json) ;
        return bReturn ;
    } 
    void Client::processMessage( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
        bDisconnect = false ;
        const char* type=NULL ;
        json_error_t err ;
        if( 0 > json_unpack_ex( pMsg->value(), &err, 0, "{s:s}", "type", &type) ) {
            DR_LOG(log_error) << "Client::processMessage - failed to parse message: " << err.text << endl ;
            bDisconnect = true ;  
            return ;
        }

        if( 0 == strcmp( type, "notify") ) this->processNotify( pMsg, msgResponse, bDisconnect ) ;
        else if( 0 == strcmp( type, "request") ) this->processRequest( pMsg, msgResponse, bDisconnect ) ;
        else {
            DR_LOG(log_error) << "Unknown message type: " << type << endl ;
            bDisconnect = true ;           
        }
    }
    void Client::processNotify( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
        bDisconnect = false ;
        const char *command=NULL, *transactionId=NULL ;
        json_error_t err ;

        if( 0 > json_unpack_ex( pMsg->value(), &err, 0, "{s:s,s:{s:s}}", "command", &command, "data", "transactionId", &transactionId) ) {
            DR_LOG(log_error) << "Client::processNotify - failed to parse message: " << err.text << endl ;
            bDisconnect = true ;  
            return ;           
        }
 
        if( 0 == strcmp( command,"respondToSipRequest") ) {
            m_controller.respondToSipRequest( transactionId, pMsg ) ;
        }
        else {
            DR_LOG(log_error) << "Unknown notify: " << command << endl ;
            bDisconnect = true ;
        }
    }
    void Client::processRequest( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
        bDisconnect = false ;
        const char *command=NULL, *id=NULL, *verb=NULL ;
        json_error_t err ;

        if( 0 > json_unpack_ex( pMsg->value(), &err, 0, "{s:s,s:s,s:{s?s}}", "command", &command, "rid", &id, "data", "verb", &verb) ) {
            DR_LOG(log_error) << "Client::processRequest - failed to parse message: " << err.text << endl ;
            bDisconnect = true ;  
            return ;           
         }

        if( 0 == strcmp(command, "route") ) { 
            if( !m_controller.wants_requests( shared_from_this(), verb ) ) {
               DR_LOG(log_error) << "Route request includes unsupported verb: " << verb << endl ;   
               bDisconnect = true ;       
               return ;        
            }
            json_t* json = json_pack("{s:s,s:s,s:{s:b}}", "type","response","rid",id,"data","success",true) ;
            msgResponse.set( json ) ;
        }
        else if( 0 == strcmp( command,"sendSipRequest") ) {
            m_controller.sendSipRequest( shared_from_this(), pMsg, id ) ;
        }   
        else {
            DR_LOG(log_error) << "Unknown request: " << command << endl ;
            bDisconnect = true ;
        }
    }
    void Client::processResponse( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
        throw std::runtime_error("Client::processResponse not implemented") ;
    }
    void Client::sendRequestOutsideDialog( const string& transactionId, boost::shared_ptr<SofiaMsg> sm ) {
        string strUuid ;
        generateUuid( strUuid ) ;

        json_t* json = json_pack("{s:s,s:s,s:s,s:{s:s,s:o}}", "type","notify","rid",strUuid.c_str(),"command", "sip", 
            "data","transactionId",transactionId.c_str(), "message", sm->detach() ) ;

        send(json) ;
    }
    void Client::sendRequestInsideDialog( const string& transactionId, const string& dialogId, boost::shared_ptr<SofiaMsg> sm  ) {
        string strUuid ;
        generateUuid( strUuid ) ;

        json_t* json = json_pack("{s:s,s:s,s:s,s:{s:s,s:s,s:o}}", "type","notify","rid",strUuid.c_str(),"command", "sip", 
            "data","transactionId",transactionId.c_str(), "dialogId", dialogId.c_str(), "message",sm->detach()) ;

        send(json) ;
    }
    void Client::sendAckRequestInsideDialog( const string& transactionId, const string& inviteTransactionId, const string& dialogId, boost::shared_ptr<SofiaMsg> sm  ) {
        string strUuid ;
        generateUuid( strUuid ) ;

        json_t* json = json_pack("{s:s,s:s,s:s,s:{s:s,s:s,s:s,s:o}}", "type","notify","rid",strUuid.c_str(),"command", "sip", 
            "data","transactionId",transactionId.c_str(), "inviteTransactionId", inviteTransactionId.c_str(), 
            "dialogId", dialogId.c_str(), "message",sm->detach()) ;

        send(json) ;
    }
    void Client::sendResponseInsideTransaction( const string& transactionId, const string& dialogId, boost::shared_ptr<SofiaMsg> sm ) {
        string strUuid ;
        generateUuid( strUuid ) ;

        json_t* json = NULL ;
        if( !dialogId.empty() ) {
            json = json_pack("{s:s,s:s,s:s,s:{s:s,s:s,s:o}}", "type","notify","rid",strUuid.c_str(),"command", "sip", 
                "data","transactionId",transactionId.c_str(), 
                "dialogId", dialogId.c_str(), "message",sm->detach()) ;
        }
        else {
            json = json_pack("{s:s,s:s,s:s,s:{s:s,s:o}}", "type","notify","rid",strUuid.c_str(),"command", "sip", 
                "data","transactionId",transactionId.c_str(), 
                "message",sm->detach()) ;            
        }
        send(json) ;
   }
    void Client::sendResponse( const string& rid, json_t* obj) {
        json_t* json = json_pack("{s:s,s:s,s:o}", "type","response","rid",rid.c_str(),"data",obj) ;            
        send(json) ;
     }
    void Client::sendRequestInsideInvite( const string& transactionId, boost::shared_ptr<SofiaMsg> sm) {
        string strUuid ;
        generateUuid( strUuid ) ;

        json_t* json = json_pack("{s:s,s:s,s:s,s:{s:s,s:o}}", "type","notify","rid",strUuid.c_str(),"command", "sip", 
            "data","transactionId",transactionId.c_str(), "message",sm->detach()) ;

       send(json) ;
     }
    void Client::sendRequestInsideInviteWithDialog( const string& transactionId, const string& dialogId, boost::shared_ptr<SofiaMsg> sm ) {
        string strUuid ;
        generateUuid( strUuid ) ;

        json_t* json = json_pack("{s:s,s:s,s:s,s:{s:s,s:s,s:o}}", "type","notify","rid",strUuid.c_str(),"command", "sip", 
            "data","transactionId",transactionId.c_str(),"dialogId",dialogId.c_str(),"message",sm->detach()) ;

       send(json) ;
     }
   void Client::sendEventInsideDialog( const string& transactionId, const string& dialogId, const string& event ) {
        string strUuid ;
        generateUuid( strUuid ) ;

        json_t* json = json_pack("{s:s,s:s,s:s,s:{s:s,s:s,s:s}}", "type","notify","rid",strUuid.c_str(),"command", "dialog", 
            "data","transactionId",transactionId.c_str(),"dialogId",dialogId.c_str(),"event",event.c_str()) ;

        send(json) ;
    }
    void Client::send( json_t* json ) {
        char * c = json_dumps(json, JSON_SORT_KEYS | JSON_COMPACT | JSON_ENCODE_ANY ) ;
        if( !c ) {
            throw std::runtime_error("JsonMsg::stringify: encode operation failed") ;
        }

        ostringstream o ;
        o << strlen(c) << "#" << c ;
#ifdef DEBUG 
        my_json_free(c) ;
#else
        free(c) ;
#endif        
        boost::asio::write( m_sock, boost::asio::buffer( o.str() ) ) ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;   
        json_decref(json) ;    
    }
}
