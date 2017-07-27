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
#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/unordered_map.hpp>


namespace drachtio {

	class ClientController ;

	class SipDialogController ;

	class Client : public boost::enable_shared_from_this<Client> {
	public:
	    Client( boost::asio::io_service& io_service, ClientController& controller ) ;
	    ~Client() ;
	    
	    boost::asio::ip::tcp::socket& socket() {
	        return m_sock;
	    }
	    const boost::asio::ip::tcp::socket& const_socket() const {
	        return m_sock;
	    }
	    
	    void start(); 
	    void read_handler( const boost::system::error_code& ec, std::size_t bytes_transferred ) ;
	    void write_handler( const boost::system::error_code& ec, std::size_t bytes_transferred );

			bool processClientMessage( const string& msg, string& msgResponse ) ;
	    void sendSipMessageToClient( const string& transactionId, const string& dialogId, const string& rawSipMsg, const SipMsgData_t& meta ) ;
	    void sendSipMessageToClient( const string& transactionId, const string& rawSipMsg, const SipMsgData_t& meta ) ;
	    void sendCdrToClient( const string& rawSipMsg, const string& meta ) ;
	    void sendApiResponseToClient( const string& clientMsgId, const string& responseText, const string& additionalResponseText ) ;

		bool getAppName( string& strAppName ) { strAppName = m_strAppName; return !strAppName.empty(); }

	protected:

		enum state {
			initial = 0,
			authenticated,
		} ;
	  

		bool readMessageLength( unsigned int& len ) ;
		void createResponseMsg( const string& msgId, string& msg, bool ok = true, const char* szReason = NULL ) ;
		void send( const string& str ) ;  
		boost::shared_ptr<SipDialogController> getDialogController(void) ;


	private:
		ClientController& m_controller ;
		state m_state ;
    boost::asio::ip::tcp::socket m_sock;
    boost::array<char, 8192> m_readBuf ;
    boost::circular_buffer<char> m_buffer ;
    unsigned int m_nMessageLength ;
    string m_strAppName ;
	} ;

	typedef boost::shared_ptr<Client> client_ptr;
	typedef boost::weak_ptr<Client> client_weak_ptr;
 	  
}



#endif
