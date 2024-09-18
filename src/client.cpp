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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <iostream>
#include <memory>

#include <functional>
#include <algorithm>

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>

#include "drachtio.h"
#include "client.hpp"
#include "controller.hpp"

#if !defined(SOL_TCP) && defined(IPPROTO_TCP)
#define SOL_TCP IPPROTO_TCP
#endif
#if !defined(TCP_KEEPIDLE) && defined(TCP_KEEPALIVE)
#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif

namespace drachtio {
    std::size_t hash_value( BaseClient const &c ) {
        std::size_t seed = 0 ;
        boost::hash_combine(seed, c.endpoint_address()) ;
        boost::hash_combine(seed, c.endpoint_port()) ;
        return seed ;
    }

    void setTcpKeepAlive(int s) {
        int optval = 1 ;
        socklen_t optlen = sizeof(optval);
        int yes = 1;
        int result = setsockopt(s,
                                IPPROTO_TCP,
                                TCP_NODELAY,
                                (char *) &yes,
                                sizeof(int));
        if (result < 0) {
            DR_LOG(log_error) << "Client::start - error setting tcp nodelay";
            return;
        }

        unsigned int seconds = theOneAndOnlyController->getTcpKeepaliveInterval();
        if (seconds == 0) {
            return;
        }

        if (setsockopt(s, SOL_SOCKET,  SO_KEEPALIVE, &optval, optlen) < 0) {
            DR_LOG(log_error) << "Client::start - error enabling tcp keepalive option";
            return;
        }
        optval = 1;
        if (setsockopt(s, SOL_TCP, TCP_KEEPIDLE, &optval, optlen) < 0) {
            DR_LOG(log_error) << "Client::start - error enabling tcp keepalive";
            return;
        }
        optval = seconds;
        if (setsockopt(s, SOL_TCP, TCP_KEEPINTVL, &optval, optlen) < 0) {
            DR_LOG(log_error) << "Client::start - error setting tcp keepalive interval";
            return;
        }
    }

    // BaseClient
    BaseClient::BaseClient(ClientController& controller) :
        m_controller( controller ),  
        m_state(initial), m_buffer(12228), m_nMessageLength(0) {
            time(&m_tConnect);
    }
    BaseClient::BaseClient(ClientController& controller, 
        const string& transactionId, 
        const string& host, const string& port) :
        m_controller( controller ), 
        m_transactionId(transactionId), m_host(host), m_port(port),
        m_state(initial), m_buffer(12228), m_nMessageLength(0) {
            time(&m_tConnect);
    }

    BaseClient::~BaseClient() {
      DR_LOG(log_debug) << "BaseClient::~BaseClient";
    }

    std::shared_ptr<SipDialogController> BaseClient::getDialogController() {
        return m_controller.getDialogController(); 
    }

    bool BaseClient::processClientMessage( const string& msg, string& msgResponse ) {
        string meta, startLine, headers, body ;
       
        splitMsg( msg, meta, startLine, headers, body ) ;
        vector<string>tokens ;
        splitTokens( meta, tokens) ;

        if( tokens.size() < 2 ) {
            DR_LOG(log_error) << "Client::processClientMessage - invalid message: " << msg  ;
            createResponseMsg( tokens[0], msgResponse, false, "Invalid message format" ) ;
            return false ;
        }

        if (0 == tokens[1].compare("ping")) {
            createResponseMsg( tokens[0], msgResponse, true, "pong" ) ;
            return true;
        }
        else if( 0 == tokens[1].compare("route") ) {
            if( !m_controller.wants_requests( shared_from_this(), tokens[2] ) ) {
                DR_LOG(log_error) << "Route request includes unsupported verb: " << tokens[2]  ;   
                createResponseMsg( tokens[0], msgResponse, false, "Route request includes unsupported verb" ) ;
                return false ;        
            }
            createResponseMsg( tokens[0], msgResponse ) ;
        }
        else if( 0 == tokens[1].compare("remove_route") ) {
            if( !m_controller.no_longer_wants_requests( shared_from_this(), tokens[2] ) ) {
                DR_LOG(log_error) << "Remove route request includes unsupported verb: " << tokens[2]  ;   
                createResponseMsg( tokens[0], msgResponse, false, "Remove route request includes unsupported verb" ) ;
                return false ;        
            }
            createResponseMsg( tokens[0], msgResponse ) ;
        }
        else if( 0 == tokens[1].compare("authenticate")) {
            string secret = tokens[2] ;
            if (tokens.size() > 3) {
                string tags = tokens[3];
                vector<string> strs;
                boost::split(strs, tags, boost::is_any_of(","));
                for (vector<string>::iterator it = strs.begin(); it != strs.end(); ++it) {
                    m_tags.insert(*it);
                }
                DR_LOG(log_debug) << "Client::processAuthentication - added tags " << tags ;
            }
            DR_LOG(log_debug) << "Client::processAuthentication - validating secret " << secret  ;
            if( !theOneAndOnlyController->isSecret( secret ) ) {
                DR_LOG(log_info) << "Client::processAuthentication - secret validation failed: " << secret  ;
                createResponseMsg( tokens[0], msgResponse, false, "incorrect secret" ) ;
                return false ;       
            } 
            else {
                vector<string> hps ;
                theOneAndOnlyController->getMyHostports( hps ) ;
                string hostports = boost::algorithm::join(hps, ",") ;
                string response = hostports + "|" + DRACHTIO_VERSION;
                createResponseMsg( tokens[0], msgResponse, true, response.c_str()) ;
                DR_LOG(log_debug) << "Client::processAuthentication - secret validated successfully: " << secret ;
                return true ;
            }            
        }
        else if( 0 == tokens[1].compare("sip") ) {
            bool bOK = false ;
            string transactionId, dialogId, routeUrl ;

            DR_LOG(log_debug) << "Client::processMessage - got request with " << tokens.size() << " tokens"  ;
            assert(tokens.size() >= 4) ;

            transactionId = tokens[2] ;
            dialogId = tokens[3] ;
            if (tokens.size() > 4) routeUrl = tokens[4] ;

            DR_LOG(log_debug) << "Client::processMessage - request id " << tokens[0] << ", request type: " << tokens[1] 
                << " transaction id: " << transactionId << ", dialog id: " << dialogId  ;

            if( 0 == startLine.find("SIP/") ) {
                //response: must have a transaction id for the associated request
                if( 0 == transactionId.length() ) {
                    DR_LOG(log_error) << "Client::processMessage - invalid sip response message; transaction id missing"  ;
                    createResponseMsg( tokens[0], msgResponse, false, "transaction id missing" ) ;
                    return false; 
                }
                m_controller.respondToSipRequest( shared_from_this(), tokens[0], transactionId, startLine, headers, body ) ;
            }
            else if( dialogId.length() > 0 ) { 
                //has dialog id - request within a dialog
                DR_LOG(log_debug) << "Client::processMessage - sending a request inside a dialog (dialogId provided)"  ;
                bOK = m_controller.sendRequestInsideDialog( shared_from_this(), tokens[0], dialogId, startLine, headers, body, transactionId ) ;
            }
            else if( transactionId.length() > 0 ) {
                if( 0 == startLine.find("CANCEL") ) {
                    DR_LOG(log_debug) << "Client::processMessage - sending a CANCEL request inside a transaction" ;
                    bOK = m_controller.sendCancelRequest( shared_from_this(), tokens[0], transactionId, startLine, headers, body) ;
                }
                else {
                    assert(false) ;// are there other requests within a transaction, besides CANCEL??
                }
            }
            else {
                string strCallId ;

                //if provided, check if Call-ID is for an existing dialog 
                if( GetValueForHeader( headers, "Call-ID", strCallId ) ) {
                    std::shared_ptr<SipDialog> dlg ;
                    if( getDialogController()->findDialogByCallId( strCallId, dlg ) ) {
                        DR_LOG(log_debug) << "Client::processMessage - sending a request inside a dialog (call-id provided)"  ;
                        m_controller.sendRequestInsideDialog( shared_from_this(), tokens[0], dlg->getDialogId(), startLine, headers, body, transactionId ) ;
                        return true ;
                    }
                }
                DR_LOG(log_debug) << "Client::processMessage - sending a request outside of a dialog"  ;
                bOK = m_controller.sendRequestOutsideDialog( shared_from_this(), tokens[0], startLine, headers, body, transactionId, dialogId, routeUrl ) ;
             }

             return true ;
        }
        else if( 0 == tokens[1].compare("proxy") ) {
            DR_LOG(log_debug) << "Client::processMessage - received proxy request " << boost::algorithm::join(tokens, ",");
            if( tokens.size() < 4 ) {
                DR_LOG(log_error) << "Invalid proxy request: insufficient tokens: '" <<  boost::algorithm::join(tokens, ",") ;
                createResponseMsg( tokens[0], msgResponse, false, "Invalid proxy request: not enough information provided" ) ;
                return false ;             
            }
            string transactionId = tokens[2] ;
            bool recordRoute = 0 == tokens[3].compare("remainInDialog") ;
            bool fullResponse = 0 == tokens[4].compare("fullResponse") ;
            bool followRedirects = 0 == tokens[5].compare("followRedirects") ;
            bool simultaneous = 0 == tokens[6].compare("simultaneous") ;
            string provisionalTimeout = tokens[7] ;
            string finalTimeout = tokens[8]; 
            vector<string> vecDestinations( tokens.begin() + 9, tokens.end() ) ;
            m_controller.proxyRequest( shared_from_this(), tokens[0], transactionId, recordRoute, fullResponse, followRedirects, 
                simultaneous, provisionalTimeout, finalTimeout, vecDestinations, headers ) ;
            return true ;
        }
        else {
            DR_LOG(log_error) << "Unknown message type: '" << tokens[1] << "'"  ;
            createResponseMsg( tokens[0], msgResponse, false, "Unknown message type" ) ;
            return false ;           
        }
        createResponseMsg( tokens[0], msgResponse ) ;
        return true ;
    }

    bool BaseClient::readMessageLength(unsigned int& len) {
        bool continueOn = true ;
        std::array<char, 6> ch ;
        memset( ch.data(), 0, sizeof(ch)) ;
        unsigned int i = 0 ;
        char c ;
        do {
            c = m_buffer.front() ;
            m_buffer.pop_front() ;
            if ('#' == c) break ;
            if (!isdigit(c)) throw std::runtime_error("Client::readMessageLength - invalid message length specifier") ;
            ch[i++] = c ;
        } while(m_buffer.size() && i < 6) ;

        if (6 == i) throw std::runtime_error("Client::readMessageLength - invalid message length specifier") ;

        if (0 == m_buffer.size()) {
            if('#' != c) {
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


    void BaseClient::sendSipMessageToClient( const string& transactionId, const string& dialogId, const string& rawSipMsg, const SipMsgData_t& meta ) {
        string strUuid, s ;
        generateUuid( strUuid ) ;
        meta.toMessageFormat(s) ;

        send(strUuid + "|sip|" + s + "|" + transactionId + "|" + dialogId + "|" + DR_CRLF + rawSipMsg) ;
    }

    void BaseClient::sendSipMessageToClient( const string& transactionId, const string& rawSipMsg, const SipMsgData_t& meta ) {
        string strUuid, s ;
        generateUuid( strUuid ) ;
        meta.toMessageFormat(s) ;
        string strMsg = strUuid + "|sip|" + s + "|" + transactionId + "||" ;
        if (meta.getDestAddress().length() > 0) {
            strMsg += meta.getDestAddress();
            strMsg += "|";
            strMsg += meta.getDestPort();
            strMsg += "|";
        }
        strMsg += DR_CRLF;
        strMsg += rawSipMsg;

        //send(strUuid + "|sip|" + s + "|" + transactionId + "||" + DR_CRLF + rawSipMsg) ;
        send(strMsg) ;
    }

    void BaseClient::sendCdrToClient( const string& rawSipMsg, const string& meta ) {
        string strUuid, s ;
        generateUuid( strUuid ) ;

        send(strUuid + "|" + meta + DR_CRLF + rawSipMsg) ;
    }

    void BaseClient::sendApiResponseToClient( const string& clientMsgId, const string& responseText, const string& additionalResponseText ) {
        string strUuid ;
        generateUuid( strUuid ) ;
        string msg = strUuid + "|response|" + clientMsgId + "|" ;
        msg.append( responseText ) ;
        if( !additionalResponseText.empty() ) {
            msg.append("|") ;
            msg.append( additionalResponseText ) ;
        }
        send(msg) ;
    }

    void BaseClient::createResponseMsg(const string& msgId, string& msg, bool ok, const char* szReason ) {
        string strUuid ;
        generateUuid( strUuid ) ;
        msg = strUuid + "|response|" + msgId + "|" ;
        msg.append( ok ? "OK" : "NO") ;
        if( szReason ) {
            msg.append("|") ;
            msg.append( szReason ) ;
        }
    }

    // Client (member functions)

    template<typename T, typename S>
    void Client<T,S>::read_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) {

        if( ec ) {
            DR_LOG(log_error) << "Client::read_handler - bouncing client due to error reading: " << ec ;
            m_controller.leave( shared_from_this() ) ;
            return ;
        }

        //DR_LOG(log_debug) << "Client::read_handler read raw message of " << bytes_transferred << " bytes: " << std::string(m_readBuf.begin(), m_readBuf.begin() + bytes_transferred) << endl ;

        /* append the data to our in-process buffer */
        m_buffer.insert( m_buffer.end(), m_readBuf.begin(),  m_readBuf.begin() + bytes_transferred ) ;

        /* if we're starting a new message, parse the message length */
        if( 0 == m_nMessageLength ) {

            try {
                if( !readMessageLength( m_nMessageLength ) ) {
                    DR_LOG(log_debug) << "Client::read_handler - message was split after message length of " << m_nMessageLength  ;                     
                    goto read_again ;
                }
            }
            catch( std::runtime_error& err ) {
                DR_LOG(log_error) << "Client::read_handler client sent invalid message -- JSON message length not specified properly"  ;                     
                m_controller.leave( shared_from_this() ) ;               
                return ;
            }
        }

        /* while we have at least one full message, process it */
        while( m_buffer.size() >= m_nMessageLength && m_nMessageLength > 0 ) {
            string msgResponse ;
            string in ;
            bool bContinue = true ;
            try {
                in.assign(m_buffer.begin(), m_buffer.begin() + m_nMessageLength);
                DR_LOG(log_debug) << "Client::read_handler read: " << in << endl ;
                bContinue = processClientMessage( in, msgResponse ) ;
            } catch( std::runtime_error& err ) {
                DR_LOG(log_error) << "Client::read_handler - Error processing client message: " << std::string( m_buffer.begin(), m_buffer.begin() + m_nMessageLength ) << " : " << err.what()  ;
                m_controller.leave( shared_from_this() ) ;
                return ;
            }

            /* send response if indicated */
            if( !msgResponse.empty() ) {
                int len = std::size(msgResponse);
                auto forthelifeofsend = std::make_shared<std::string>(
                    std::to_string(len) + std::string("#") + msgResponse
                );

                auto self(shared_from_this());
                DR_LOG(log_debug) << "Sending response: " << *forthelifeofsend << endl ;
                boost::asio::async_write( m_sock, boost::asio::buffer( *forthelifeofsend ), 
                    [self, forthelifeofsend](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                        DR_LOG(log_debug) << "Client::read_handler - wrote " << bytes_transferred << " bytes: " << ec  ;
                    } );
            }
            if( !bContinue ) {
                 DR_LOG(log_error) << "Client::read_handler - disconnecting client due to error processing client message" ;
                m_controller.leave( shared_from_this() ) ;
                return ;
            }

            if( this->isOutbound() && string::npos != in.find("|authenticate|")) {
              m_controller.outboundReady( shared_from_this(), m_transactionId ) ;
            }


            /* reload for next message */
            m_buffer.erase_begin( m_nMessageLength ) ;
            if( m_buffer.size() ) {
                DR_LOG(log_debug) << "Client::read_handler processing follow-on message in read buffer, remaining bytes to process: " << m_buffer.size()  ;
                try {
                    if( !readMessageLength( m_nMessageLength ) ) {
                        DR_LOG(log_debug) << "Client::read_handler - message was split after message length of " << m_nMessageLength  ;                     
                        break ;
                    }
                }
                catch( std::runtime_error& err ) {
                    DR_LOG(log_error) << "Client::read_handler client sent invalid message -- message length not specified properly"  ;                     
                    m_controller.leave( shared_from_this() ) ;               
                    return ;
                }
                DR_LOG(log_debug) << "Client::read_handler follow-on message length of " << m_nMessageLength << " bytes "  ; 
            }
            else {
                m_nMessageLength = 0 ;
            }
        }

read_again:
        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
            std::bind( &BaseClient::read_handler, shared_from_this(), std::placeholders::_1, std::placeholders::_2 ) ) ;
       
    }

    template<typename T, typename S>
    void Client<T,S>::write_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) {
        DR_LOG(log_debug) << "Client::write_handler - wrote " << bytes_transferred << " bytes: " << ec  ;
    }

    template<typename T, typename S>
    void Client<T,S>::send( const string& str ) {
        int len = std::size(str);

        if (0 == len) {
            DR_LOG(log_info) << "Client::send - we are unable to send this message back to client" << str; 
            return;
        }

        auto forthelifeofsend = std::make_shared<std::string>(
            std::to_string(len) + std::string("#") + str
        );

        auto self(shared_from_this());
        DR_LOG(log_debug) << "Sending: " << *forthelifeofsend << endl ;
        boost::asio::async_write( m_sock, boost::asio::buffer( *forthelifeofsend ), 
            [self, forthelifeofsend](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                DR_LOG(log_debug) << "Client::send - wrote " << bytes_transferred << " bytes: " << ec  ;
            } );
    }

    // Client (member function specializations for plain tcp connections)
    
    template<>
    Client<socket_t>::Client(boost::asio::io_context& io_context, ClientController& controller) :
        BaseClient(controller),
        m_sock(io_context) {
    }

    template<>
    Client<socket_t>::Client( boost::asio::io_context& io_context, ClientController& controller,
        const string& transactionId, const string& host, 
        const string& port ) :
        BaseClient(controller, transactionId, host, port),
        m_sock(io_context) {

    }

    template<>
    void Client<socket_t>::start() {

        m_strRemoteAddress = m_sock.remote_endpoint().address().to_string();
        m_nRemotePort = m_sock.remote_endpoint().port();

        DR_LOG(log_debug) << "Client::start - Received connection from client at " << m_strRemoteAddress << ":" << m_nRemotePort ;

        setTcpKeepAlive(m_sock.native_handle());

        m_controller.join( shared_from_this() ) ;
        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
            std::bind( &BaseClient::read_handler, shared_from_this(), std::placeholders::_1, std::placeholders::_2 ) ) ;
    }

    template<>
    void Client<socket_t>::async_connect() {
        boost::asio::ip::tcp::resolver::query query(m_host, m_port) ;
        boost::asio::ip::tcp::resolver resolver(m_controller.getIOService());
        tcp::resolver::iterator endpointIterator = resolver.resolve(query);
        tcp::endpoint endpoint = *endpointIterator;

        m_sock.async_connect(endpoint, std::bind(&BaseClient::connect_handler, shared_from_this(), std::placeholders::_1, ++endpointIterator));
    }

    template<>
    void Client<socket_t>::connect_handler(const boost::system::error_code& ec, tcp::resolver::iterator endpointIterator) {
        if( !ec ) {
            DR_LOG(log_debug) << "Client::connect_handler tcp - successfully connected to " <<
            endpoint_address() << ":" << endpoint_port() ;

        m_controller.join( shared_from_this() ) ;

        setTcpKeepAlive(m_sock.native_handle());

        m_sock.async_read_some(boost::asio::buffer(m_readBuf),
            std::bind( &BaseClient::read_handler, shared_from_this(), std::placeholders::_1, 
            std::placeholders::_2 ) ) ;

        //TODO: set a timeout of 2 secs or so for remote side to authenticate

        }
        else if( endpointIterator != tcp::resolver::iterator() ) {
            DR_LOG(log_debug) << "Client::connect_handler tcp - failed to connecte to " <<
            endpoint_address() << ":" << endpoint_port() ;
            m_sock.close() ;
            tcp::endpoint endpoint = *endpointIterator;
            m_sock.async_connect(endpoint, std::bind(&BaseClient::connect_handler, shared_from_this(), std::placeholders::_1, ++endpointIterator));
        }
        else {
            // final failure
            DR_LOG(log_warning) << "Client::connect_handler tcp - unable to connect to " << m_host << ":" << m_port ;
            m_controller.outboundFailed(m_transactionId);
        }
    }

    // Client (member function specializations for tls connections)
    
    template<>
    Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>::Client(boost::asio::io_context& io_context, boost::asio::ssl::context& context, ClientController& controller) :
        BaseClient(controller),
        m_sock(io_context, context) {
    }

    template<>
    Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>::Client( boost::asio::io_context& io_context, boost::asio::ssl::context& context, ClientController& controller,
        const string& transactionId, const string& host, const string& port ) :
        BaseClient(controller, transactionId, host, port),
        m_sock(io_context, context) {

        m_sock.set_verify_mode(boost::asio::ssl::verify_none);
    }

    template<>
    void Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>::start() {

        m_strRemoteAddress = m_sock.lowest_layer().remote_endpoint().address().to_string();
        m_nRemotePort = m_sock.lowest_layer().remote_endpoint().port();

        setTcpKeepAlive(m_sock.lowest_layer().native_handle());

        m_sock.async_handshake(boost::asio::ssl::stream_base::server,
            std::bind(&BaseClient::handle_handshake, shared_from_this(),
            std::placeholders::_1));
    }

    template<>
    void Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>::async_connect() {
        boost::asio::ip::tcp::resolver::query query(m_host, m_port) ;
        boost::asio::ip::tcp::resolver resolver(m_controller.getIOService());
        tcp::resolver::iterator endpointIterator = resolver.resolve(query);
        tcp::endpoint endpoint = *endpointIterator;

        m_sock.lowest_layer().async_connect(endpoint, std::bind(&BaseClient::connect_handler, shared_from_this(), std::placeholders::_1, ++endpointIterator));
    }

    template<>
    void Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>::connect_handler(const boost::system::error_code& ec, tcp::resolver::iterator endpointIterator) {
        if( !ec ) {
            DR_LOG(log_debug) << "Client::connect_handler tls - successfully connected to " << m_sock.lowest_layer().remote_endpoint().address().to_string() << 
                ":" << m_sock.lowest_layer().remote_endpoint().port() ;

            m_controller.join( shared_from_this() ) ;
            m_sock.async_handshake(boost::asio::ssl::stream_base::client, std::bind(&BaseClient::handle_handshake, shared_from_this(), std::placeholders::_1));
        }
        else if( endpointIterator != tcp::resolver::iterator() ) {
            DR_LOG(log_debug) << "Client::connect_handler tls - failed to connect to "  << m_sock.lowest_layer().remote_endpoint().address().to_string() << 
                ":" << m_sock.lowest_layer().remote_endpoint().port() ;
            m_sock.lowest_layer().close() ;
            tcp::endpoint endpoint = *endpointIterator;
            m_sock.lowest_layer().async_connect(endpoint, std::bind(&BaseClient::connect_handler, shared_from_this(), std::placeholders::_1, ++endpointIterator));
        }
        else {
            // final failure
            DR_LOG(log_warning) << "Client::connect_handler tls - unable to connect to " << m_host << ":" << m_port ;
            m_controller.outboundFailed(m_transactionId);
        }
    }

    template<>
    void Client<ssl_socket_t, ssl_socket_t::lowest_layer_type>::handle_handshake(const boost::system::error_code& ec) {
        if (!ec) {
            DR_LOG(log_debug) << "Client::handle_handshake - TLS handshake succeeded ";
            m_sock.async_read_some(boost::asio::buffer(m_readBuf),
                std::bind( &BaseClient::read_handler, shared_from_this(), std::placeholders::_1, std::placeholders::_2 ) ) ;
        }
        else {
            m_controller.leave( shared_from_this() ) ;
            DR_LOG(log_error) << "Client::handle_handshake - TLS handshake failed: " << ec.message() << " (" << ec << ")" ;
        }
    }
    template<>
    void Client<socket_t>::handle_handshake(const boost::system::error_code& ec) {
        assert(0);
    }
}
