/*
Copyright (c) 2024, FirstFive8, Inc

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
#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <unordered_map>
#include <unordered_set>
#include <array>
#include <thread>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/circular_buffer.hpp>

#include <time.h>

namespace drachtio {

    typedef boost::asio::ip::tcp::socket socket_t;
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket_t;

	class ClientController ;

	class SipDialogController ;

    class BaseClient : public enable_shared_from_this<BaseClient> {
    public:
        BaseClient(ClientController& controller);
        BaseClient(ClientController& controller, 
            const string& transactionId, const string& host, const string& port);
        ~BaseClient();

        const string& endpoint_address() const { return m_strRemoteAddress;}
        const unsigned short endpoint_port() const { return m_nRemotePort;}
        
        virtual void start() = 0;

        virtual void async_connect() = 0;
        virtual void connect_handler(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator endpointIterator) = 0;
        virtual void read_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) = 0;
        virtual void write_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) = 0;

        virtual void handle_handshake(const boost::system::error_code& ec) = 0;


        bool processClientMessage( const string& msg, string& msgResponse ) ;
        void sendSipMessageToClient( const string& transactionId, const string& dialogId, const string& rawSipMsg, const SipMsgData_t& meta ) ;
        void sendSipMessageToClient( const string& transactionId, const string& rawSipMsg, const SipMsgData_t& meta ) ;
        void sendCdrToClient( const string& rawSipMsg, const string& meta ) ;
        void sendApiResponseToClient( const string& clientMsgId, const string& responseText, const string& additionalResponseText ) ;

        bool getAppName( string& strAppName ) { strAppName = m_strAppName; return !strAppName.empty(); }
        bool isOutbound(void) const { return !m_transactionId.empty(); }
        bool hasTag(const char* tag) const { return m_tags.find(tag) != m_tags.end(); }

        int getConnectionDuration(void) const { 
            return time(NULL) - m_tConnect; 
        }
    protected:
        virtual void send( const string& str ) = 0 ;  

        enum state {
            initial = 0,
            authenticated,
        } ;
    
        bool readMessageLength( unsigned int& len ) ;
        void createResponseMsg( const string& msgId, string& msg, bool ok = true, const char* szReason = NULL ) ;
        std::shared_ptr<SipDialogController> getDialogController(void);

        ClientController& m_controller ;
        state m_state ;

        std::array<char, 8192> m_readBuf ;
        boost::circular_buffer<char> m_buffer ;
        unsigned int m_nMessageLength ;
        string m_strAppName ;

        typedef std::unordered_set<string> set_of_tags ;
        set_of_tags m_tags;

        // outbound connections
        string m_transactionId ;
        string m_host ;
        string m_port ;

        string m_strRemoteAddress;
        unsigned int m_nRemotePort;

        time_t m_tConnect ;
    };

	template <typename T, typename S = T> 
    class Client : public BaseClient {
	public:

        Client(boost::asio::io_context& io_context, ClientController& controller);
        Client(boost::asio::io_context& io_context, ClientController& controller, 
            const string& transactionId, const string& host, const string& port);

        Client(boost::asio::io_context& io_context, boost::asio::ssl::context& context, ClientController& controller) ;
        Client(boost::asio::io_context& io_context, boost::asio::ssl::context& context,  ClientController& controller, 
            const string& transactionId, const string& host, const string& port);
       
        ~Client() {}

        void async_connect();
        void connect_handler(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator endpointIterator);
        void read_handler( const boost::system::error_code& ec, std::size_t bytes_transferred );
        void write_handler( const boost::system::error_code& ec, std::size_t bytes_transferred );

        void handle_handshake(const boost::system::error_code& ec);

        void start(); 

        T& socket() { return m_sock; }

    protected:
        void send( const string& str );  

        T m_sock;

    private:
        Client();  // prohibited

    } ;

    typedef std::shared_ptr<BaseClient> client_ptr;
    typedef std::weak_ptr<BaseClient> client_weak_ptr;
}



#endif
