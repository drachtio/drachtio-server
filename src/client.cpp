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

using namespace json_spirit ;

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
                    return ;
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
            if( !msgResponse.isNull() ) {
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
                        return ;
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

        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
                        boost::bind( &Client::read_handler, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred ) ) ;

       
    }
    void Client::write_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) {
    	DR_LOG(log_debug) << "Wrote " << bytes_transferred << " bytes: " << endl ;
    }

    bool Client::processAuthentication( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse  ) {
        bool bReturn = false ;
        string secret ;
        string id = pMsg->get_or_default<string>("rid","") ;
        ostringstream o ;
        o << "{\"type\": \"response\", \"rid\": \"" << id << "\",\"data\": {\"authenticated\":" ;        

        if( 0 != pMsg->get_or_default<string>("type","").compare("request") || 
            0 != pMsg->get_or_default<string>("command","").compare("auth") || 
            !pMsg->get<string>("data.secret", secret)) {

            o << "false, \"reason\":\"invalid auth message\"}}" ;
        }
        else if( (bReturn = theOneAndOnlyController->isSecret( secret ) )  ) {
            string hostport ;
            theOneAndOnlyController->getMyHostport( hostport ) ;
            o << "true, \"hostport\": \"" << hostport << "\"}}" ;

            string strAppName ;
            if( pMsg->get<string>("data.appName", strAppName) ) {
                m_controller.addNamedService( shared_from_this(), strAppName ) ; 
                m_strAppName = strAppName ;
            }
        }
        else {
            o << "false, \"reason\":\"invalid credentials\"}}" ;
        }
        msgResponse.set( o.str() ) ;
        return bReturn ;
    } 
    void Client::processMessage( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
        bDisconnect = false ;

        string type = pMsg->get_or_default<string>("type","") ;
        if( 0 == type.compare("notify") ) this->processNotify( pMsg, msgResponse, bDisconnect ) ;
        else if( 0 == type.compare("request") ) this->processRequest( pMsg, msgResponse, bDisconnect ) ;


    }
    void Client::processNotify( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
       bDisconnect = false ;

        string command = pMsg->get_or_default<string>("command","") ;
        string id = pMsg->get_or_default<string>("rid","") ;
        ostringstream o ;

        if( 0 == command.compare("respondToSipRequest") ) {
            int code ;
            string transactionId = pMsg->get_or_default<string>("data.transactionId","") ;
            m_controller.respondToSipRequest( transactionId, pMsg ) ;
        }
        else {
            DR_LOG(log_error) << "Unknown notify: " << command << endl ;
            bDisconnect = true ;
        }
    }
    void Client::processRequest( boost::shared_ptr<JsonMsg> pMsg, JsonMsg& msgResponse, bool& bDisconnect ) {
       bDisconnect = false ;

        string command = pMsg->get_or_default<string>("command","") ;
        string id = pMsg->get_or_default<string>("rid","") ;
        ostringstream o ;

        if( 0 == command.compare("route") ) {
            string verb ;
            if( !pMsg->get<string>("data.verb", verb) ) {
               DR_LOG(log_error) << "Route request must include verb " << endl ;   
               bDisconnect = true ;
               return ;         
            }
 
            if( !m_controller.wants_requests( shared_from_this(), verb ) ) {
               DR_LOG(log_error) << "Route request includes unsupported verb: " << verb << endl ;   
               bDisconnect = true ;       
               return ;        
            }
            o << "{\"type\": \"response\", \"rid\": \"" << id << "\",\"data\": {\"success\":true}}" ;     
            msgResponse.set( o.str() ) ;
    
        }
        else if( 0 == command.compare("sendSipRequest") ) {
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
    void Client::sendRequestOutsideDialog( const string& transactionId, const string& msg ) {

        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendRequestInsideDialog( const string& transactionId, const string& dialogId, const string& msg ) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"dialogId\": \"" << dialogId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendAckRequestInsideDialog( const string& transactionId, const string& inviteTransactionId, const string& dialogId, const string& msg ) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"inviteTransactionId\":\"" << inviteTransactionId << "\",\"dialogId\": \"" << dialogId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendResponseInsideTransaction( const string& transactionId, const string& dialogId, const string& msg ) {
       ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\"" ;
        if( !dialogId.empty() ) o << ",\"dialogId\":\"" << dialogId << "\"" ;
        o << ",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendResponse( const string& rid, const string& strData) {
        ostringstream o ;
        this->pushMsgData( o, "response", NULL, rid.c_str() ) ;
        o << ", \"data\": " << strData.c_str() << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;        
    }
    void Client::sendRequestInsideInvite( const string& transactionId, const string& msg ) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::sendRequestInsideInviteWithDialog( const string& transactionId, const string& dialogId, const string& msg ) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "sip") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"dialogId\":\"" << dialogId << "\",\"message\": " << msg << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
   void Client::sendEventInsideDialog( const string& transactionId, const string& dialogId, const string& event ) {
        ostringstream o ;
        this->pushMsgData( o, "notify", "dialog") ;
        o << ", \"data\": {\"transactionId\": \"" << transactionId << "\",\"dialogId\": \"" << dialogId << "\",\"message\": " << event << "}" << "}" ;
        DR_LOG(log_debug) << "sending " << o.str() << endl ;
        JsonMsg jsonMsg(o.str()) ;
        string strJson ;
        jsonMsg.stringify(strJson) ;
        boost::asio::write( m_sock, boost::asio::buffer( strJson ) ) ;
    }
    void Client::pushMsgData( ostringstream& o, const char* szType, const char* szCommand, const char* szRequestId) {
        string strUuid ;
        if( !szRequestId ) {
            generateUuid( strUuid ) ;
        }
        else {
            strUuid = szRequestId; 
        }
         o << "{\"type\": \"" << szType << "\", \"rid\": \"" << strUuid << "\"" ;
         if( szCommand ) o << ",\"command\": " << "\"" << szCommand << "\"";
    }

}
