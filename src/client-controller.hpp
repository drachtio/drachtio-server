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
#ifndef __CLIENT_CONTROLLER_H__
#define __CLIENT_CONTROLLER_H__


#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/thread.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include <sofia-sip/nta.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>

#include "drachtio.h"
#include "client.hpp"

using namespace std ;

namespace drachtio {
    
  class ClientController : public boost::enable_shared_from_this<ClientController>  {
  public:
    // simple tcp version
    ClientController( DrachtioController* pController, string& address, unsigned int port ) ;
    
    // tls version
    ClientController( DrachtioController* pController, string& address, unsigned int port, 
      const string& chainFile, const string& keyFile, const string& dhFile ) ;
  
    ~ClientController() ;
      
  	void start_accept() ;
  	void threadFunc(void) ;

    void join( client_ptr client ) ;
    void leave( client_ptr client ) ;
    void outboundFailed( const string& transactionId ) ;
    void outboundReady( client_ptr client, const string& transactionId ) ;

    void addNamedService( client_ptr client, string& strAppName ) ;

    bool wants_requests( client_ptr client, const string& verb ) ;

    void addDialogForTransaction( const string& transactionId, const string& dialogId ) ;
    void addAppTransaction( client_ptr client, const string& transactionId );
    void addNetTransaction( client_ptr client, const string& transactionId );
    void addApiRequest( client_ptr client, const string& clientMsgId );
    void removeDialog( const string& dialogId ) ;
    void removeAppTransaction( const string& transactionId ) ;
    void removeNetTransaction( const string& transactionId ) ;
    void removeApiRequest( const string& clientMsgId ) ;

    client_ptr selectClientForRequestOutsideDialog( const char* keyword, const char* tag = NULL ) ;
    client_ptr findClientForDialog( const string& dialogId ) ;
    client_ptr findClientForAppTransaction( const string& transactionId ) ;
    client_ptr findClientForNetTransaction( const string& transactionId ) ;
    client_ptr findClientForApiRequest( const string& clientMsgId ) ;

    void makeOutboundConnection( const string& transactionId, const string& host, const string& port ) ;
    void selectClientForTag(const string& transactionId, const string& tag);

    bool sendRequestInsideDialog( client_ptr client, const string& clientMsgId, const string& dialogId, const string& startLine, const string& headers, const string& body, string& transactionId ) ;
    bool sendRequestOutsideDialog( client_ptr client, const string& clientMsgId, const string& startLine, const string& headers, const string& body, string& transactionId, string& dialogId, string& routeUrl ) ;
    bool respondToSipRequest( client_ptr client, const string& msgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) ;      
    bool sendCancelRequest( client_ptr client, const string& msgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) ;
    bool proxyRequest( client_ptr client, const string& clientMsgId, const string& transactionId, bool recordRoute, bool fullResponse,
      bool followRedirects, bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, 
      const vector<string>& vecDestination, const string& headers ) ;

    //this sends the client a response to the request it made to send a sip message
    bool route_api_response( const string& clientMsgId, const string& responseText, const string& additionalResponseData ) ;

    //route an incoming ACK request to the client that handled the INVITE
    bool route_ack_request_inside_dialog( const string& rawSipMsg, const SipMsgData_t& meta, nta_incoming_t* prack, sip_t const *sip, 
      const string& transactionId, const string& inviteTransactionId, const string& dialogId ) ;

    //route an incoming response to a request generated by a client
    bool route_response_inside_transaction( const string& rawSipMsg, const SipMsgData_t& meta, nta_outgoing_t* orq, sip_t const *sip, 
      const string& transactionId, const string& dialogId = "" ) ; 

    bool route_request_inside_dialog( const string& rawSipMsg, const SipMsgData_t& meta, sip_t const *sip, const string& transactionId, const string& dialogId ) ;

    bool route_request_inside_invite( const string& rawSipMsg, const SipMsgData_t& meta, nta_incoming_t* prack, sip_t const *sip, const string& transactionId, const string& dialogId  = "" ) ;

    void onTimer( const boost::system::error_code& e, boost::asio::deadline_timer* t ) ;

    void logStorageCount(void) ;

    boost::asio::io_service& getIOService(void) { return m_ioservice ;}

    boost::shared_ptr<SipDialogController> getDialogController(void) ;

  protected:

    class RequestSpecifier {
    public:
      RequestSpecifier( client_ptr client ) : m_client(client) {}
      ~RequestSpecifier() {}

      client_ptr client() {
        client_ptr p = m_client.lock() ;
        return p ;
      }

    protected:
      client_weak_ptr m_client ;
    } ;


  private:
    void accept_handler( client_ptr session, const boost::system::error_code& ec) ;
    void stop() ;

    client_ptr findClientForDialog_nolock( const string& dialogId ) ;

    DrachtioController*         m_pController ;
    boost::thread               m_thread ;
    boost::mutex                m_lock ;

    boost::asio::io_service m_ioservice;
    boost::asio::ip::tcp::endpoint  m_endpoint;
    boost::asio::ip::tcp::acceptor  m_acceptor ;
    boost::asio::ssl::context m_context;

    typedef boost::unordered_set<client_ptr> set_of_clients ;
    set_of_clients m_clients ;

    typedef boost::unordered_multimap<string,client_weak_ptr> map_of_services ;
    map_of_services m_services ;

    typedef boost::unordered_multimap<string,RequestSpecifier> map_of_request_types ;
    map_of_request_types m_request_types ;

    typedef boost::unordered_map<string,unsigned int> map_of_request_type_offsets ;
    map_of_request_type_offsets m_map_of_request_type_offsets ;

    typedef boost::unordered_map<string,client_weak_ptr> mapId2Client ;
    mapId2Client m_mapDialogs ;
    mapId2Client m_mapAppTransactions ;
    mapId2Client m_mapNetTransactions ;
    mapId2Client m_mapApiRequests ;

    typedef boost::unordered_map<string,string> mapDialogId2Appname ;
    mapDialogId2Appname m_mapDialogId2Appname ;

    bool m_useTls;
      
  } ;

}  



#endif
