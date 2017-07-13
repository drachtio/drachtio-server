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
#include <boost/asio.hpp>
#include <boost/assign/list_of.hpp>

#include "sofia-sip/url.h"

#include "request-handler.hpp"
#include "controller.hpp"

namespace drachtio {

    static boost::unordered_map<unsigned int, std::string> responseReasons = boost::assign::map_list_of
        (100, "Trying") 
        (180, "Ringing")
        (181, "Call is Being Forwarded")
        (182, "Queued")
        (183, "Session in Progress")
        (199, "Early Dialog Terminated")
        (200, "OK")
        (202, "Accepted") 
        (204, "No Notification") 
        (300, "Multiple Choices") 
        (301, "Moved Permanently") 
        (302, "Moved Temporarily") 
        (305, "Use Proxy") 
        (380, "Alternative Service") 
        (400, "Bad Request") 
        (401, "Unauthorized") 
        (402, "Payment Required") 
        (403, "Forbidden") 
        (404, "Not Found") 
        (405, "Method Not Allowed") 
        (406, "Not Acceptable") 
        (407, "Proxy Authentication Required") 
        (408, "Request Timeout") 
        (409, "Conflict") 
        (410, "Gone") 
        (411, "Length Required") 
        (412, "Conditional Request Failed") 
        (413, "Request Entity Too Large") 
        (414, "Request-URI Too Long") 
        (415, "Unsupported Media Type") 
        (416, "Unsupported URI Scheme") 
        (417, "Unknown Resource-Priority") 
        (420, "Bad Extension") 
        (421, "Extension Required") 
        (422, "Session Interval Too Small") 
        (423, "Interval Too Brief") 
        (424, "Bad Location Information") 
        (428, "Use Identity Header") 
        (429, "Provide Referrer Identity") 
        (430, "Flow Failed") 
        (433, "Anonymity Disallowed") 
        (436, "Bad Identity-Info") 
        (437, "Unsupported Certificate") 
        (438, "Invalid Identity Header") 
        (439, "First Hop Lacks Outbound Support") 
        (470, "Consent Needed") 
        (480, "Temporarily Unavailable") 
        (481, "Call Leg/Transaction Does Not Exist") 
        (482, "Loop Detected") 
        (483, "Too Many Hops") 
        (484, "Address Incomplete") 
        (485, "Ambiguous") 
        (486, "Busy Here") 
        (487, "Request Terminated") 
        (488, "Not Acceptable Here") 
        (489, "Bad Event") 
        (491, "Request Pending") 
        (493, "Undecipherable") 
        (494, "Security Agreement Required") 
        (500, "Server Internal Error") 
        (501, "Not Implemented") 
        (502, "Bad Gateway") 
        (503, "Service Unavailable") 
        (504, "Server Timeout") 
        (505, "Version Not Supported") 
        (513, "Message Too Large") 
        (580, "Precondition Failure") 
        (600, "Busy Everywhere") 
        (603, "Decline") 
        (604, "Does Not Exist Anywhere") 
        (606, "Not Acceptable");


    // RequestHandler::Client
    
    RequestHandler::Client::Client(boost::shared_ptr<RequestHandler> pRequestHandler, const string& transactionId,
        const std::string& server, const std::string& path, const std::string& service, const string& httpMethod) : pRequestHandler_(pRequestHandler),
            resolver_(pRequestHandler->getIOService()), socket_(pRequestHandler->getIOService()), status_code_(0), transactionId_(transactionId) {

            DR_LOG(log_debug) << "RequestHandler::Client::Client: Host " << server << " GET " << path ;

            // Form the request. We specify the "Connection: close" header so that the
            // server will close the socket after transmitting the response. This will
            // allow us to treat all data up until the EOF as the content.
            std::ostream request_stream(&request_);
            request_stream << "GET " << path << " HTTP/1.1\r\n";
            request_stream << "Host: " << server << "\r\n";
            request_stream << "Accept: application/json\r\n";
            request_stream << "Connection: close\r\n\r\n";

            // Start an asynchronous resolve to translate the server and service names
            // into a list of endpoints.
            tcp::resolver::query query(server, service);
            resolver_.async_resolve(query,
                boost::bind(&Client::handle_resolve, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator));
    }

    void RequestHandler::Client::handle_resolve(const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator) {
        if (!(err_ = err)) {
          // Attempt a connection to each endpoint in the list until we
          // successfully establish a connection.
          boost::asio::async_connect(socket_, endpoint_iterator,
              boost::bind(&Client::handle_connect, this,
                boost::asio::placeholders::error));
        }
        else
        {
          DR_LOG(log_error) << "RequestHandler::Client::handle_resolve: Error: " << err.message() ;
          wrapUp() ;
        }
    }

    void RequestHandler::Client::handle_connect(const boost::system::error_code& err) {
        if (!(err_ = err)) {
          // The connection was successful. Send the request.
          boost::asio::async_write(socket_, request_,
              boost::bind(&Client::handle_write_request, this,
                boost::asio::placeholders::error));
        }
        else
        {
          DR_LOG(log_error) << "RequestHandler::Client::handle_connect: (" << (void*) this << ") Error: " << err.message() ;
          wrapUp() ;
        }
    }

    void RequestHandler::Client::handle_write_request(const boost::system::error_code& err) {
        if (!(err_ = err)) {
          // Read the response status line. The response_ streambuf will
          // automatically grow to accommodate the entire line. The growth may be
          // limited by passing a maximum size to the streambuf constructor.
          boost::asio::async_read_until(socket_, response_, "\r\n",
              boost::bind(&Client::handle_read_status_line, this,
                boost::asio::placeholders::error));
        }
        else
        {
          DR_LOG(log_error) << "RequestHandler::Client::handle_write_request: Error: " << err.message() ;
          wrapUp() ;
        }
    }

    void RequestHandler::Client::handle_read_status_line(const boost::system::error_code& err) {
        if (!(err_ = err)) {
            // Check that response is OK.
            std::istream response_stream(&response_);
            std::string http_version;
            response_stream >> http_version;
            response_stream >> status_code_;
            std::string status_message;
            std::getline(response_stream, status_message);
            if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
                DR_LOG(log_error) << "RequestHandler::Client::handle_read_status_line: invalid/unexpected status line response " << http_version ;
                wrapUp() ;
                return;
            }
            if (status_code_ != 200) {
                DR_LOG(log_error) << "RequestHandler::Client::handle_read_status_line: returned status code " << std::dec << status_code_;
                wrapUp() ;
                return;
            }

            // Read the response headers, which are terminated by a blank line.
            boost::asio::async_read_until(socket_, response_, "\r\n\r\n",
              boost::bind(&Client::handle_read_headers, this,
                boost::asio::placeholders::error));
        }
        else {
          DR_LOG(log_error) << "RequestHandler::Client::handle_read_status_line Error: " << err.message() ;
          wrapUp() ;
        }
    }

    void RequestHandler::Client::handle_read_headers(const boost::system::error_code& err) {
        if (!(err_ = err)) {
            // Process the response headers.
            std::istream response_stream(&response_);
            std::string header;
            while (std::getline(response_stream, header) && header != "\r") {
                DR_LOG(log_debug) << header ;
            }
            DR_LOG(log_debug) << "";

            // Write whatever content we already have to output.
            if (response_.size() > 0) {
                DR_LOG(log_debug) << &response_;
            }

            // Start reading remaining data until EOF.
            boost::asio::async_read(socket_, response_,
              boost::asio::transfer_at_least(1),
              boost::bind(&Client::handle_read_content, this,
                boost::asio::placeholders::error));
        }
        else {
          DR_LOG(log_error) << "RequestHandler::Client::handle_read_headers Error: " << err.message() ;
          wrapUp() ;
        }
    }

    void RequestHandler::Client::handle_read_content(const boost::system::error_code& err) {
        if (!(err_ = err)) {
          // Write all of the data that has been read so far.
          body_ << &response_ ;
          DR_LOG(log_debug) << &response_;

          // Continue reading remaining data until EOF.
          boost::asio::async_read(socket_, response_,
              boost::asio::transfer_at_least(1),
              boost::bind(&Client::handle_read_content, this,
                boost::asio::placeholders::error));
        }
        else if (err != boost::asio::error::eof) {
          DR_LOG(log_error) << "RequestHandler::Client::handle_read_content Error: " << err.message() ;
        }
        else {
            wrapUp() ;
        }
    }

    void RequestHandler::Client::wrapUp() {
        pRequestHandler_->requestCompleted( shared_from_this(), err_, status_code_, body_.str()) ;
    }

    // RequestHandler

    RequestHandler::RequestHandler( DrachtioController* pController ) :
        m_pController( pController ) {
            
        boost::thread t(&RequestHandler::threadFunc, this) ;
        m_thread.swap( t ) ;
            
        //this->start_accept() ;
    }
    RequestHandler::~RequestHandler() {
        this->stop() ;
    }
    void RequestHandler::threadFunc() {
                 
        /* to make sure the event loop doesn"), t terminate when there is no work to do */
        boost::asio::io_service::work work(m_ioservice);
        
        for(;;) {
            
            try {
                m_ioservice.run() ;
                break ;
            }
            catch( std::exception& e) {
                DR_LOG(log_error) << "RequestHandler::threadFunc - Error in event thread: " << string( e.what() )  ;
            }
        }
    }

    void RequestHandler::processRequest(const string& transactionId, const string& httpMethod, const string& httpUrl, 
        const string& encodedMessage, vector< pair<string, string> >& vecParams) {

        url_t url ;
        char szUrl[URL_MAXLEN] ;
        string server, path, service ;

        strncpy( szUrl, httpUrl.c_str(), URL_MAXLEN ) ;
        url_d(&url, szUrl);
        server.assign(url.url_host) ;
        service.assign(url.url_scheme) ;

        if( url.url_port && 0 != strcmp("80", url.url_port) && 0 == strcmp("http", url.url_scheme) ) {
            service.assign(url.url_port);
        }
        else if( url.url_port && 0 != strcmp("443", url.url_port) && 0 == strcmp("https", url.url_scheme) ) {
            service.assign(url.url_port);
        }

        path = "/?" ;
        bool hasParams = false ;
        if( url.url_headers ) {
            path.append(url.url_headers) ;
            path.append("&");
        }
        
        // append our query args
        int i = 0 ;
        pair<string,string> p;
        BOOST_FOREACH(p, vecParams) {
            if( i++ > 0 ) {
                path.append("&") ;
            }
            path.append(p.first) ;
            path.append("=") ;
            path.append(p.second);
        }

        boost::shared_ptr<Client> pClient = boost::make_shared<Client>( shared_from_this(), transactionId, server, path, service, httpMethod ) ;

        m_setClients.insert( pClient ) ;

        DR_LOG(log_info) << "RequestHandler::processRequest - started (" << hex << (void*) pClient.get() << ") " << httpMethod << " request to " << url.url_scheme << "://" << server << path 
            << ", there are now " << m_setClients.size() << " requests in flight" ;
    }

    void RequestHandler::requestCompleted(boost::shared_ptr<Client> pClient, const boost::system::error_code& err, unsigned int status_code, const string& body) {
        string transactionId = pClient->getTransactionId() ;

        m_setClients.erase( pClient ) ;

        if( 200 == status_code && (!err || err == boost::asio::error::eof)) {
            // normal case
            DR_LOG(log_info) << "RequestHandler::requestCompleted - successfully completed request (" << hex << (void*) pClient.get() << 
                "), there are now " << dec << m_setClients.size() << " requests in flight"  ;

            processRoutingInstructions(transactionId, body) ;
        }
        else if( 200 != status_code && 0 != status_code ) {
            DR_LOG(log_error) << "RequestHandler::requestCompleted - error completing request (" << hex << (void*) pClient.get() << 
                ") with status code " << dec << status_code << ", there are now " << m_setClients.size() << " requests in flight"  ;
            processRejectInstruction(transactionId, status_code) ;

        }
        else {
            DR_LOG(log_error) << "RequestHandler::requestCompleted - error completing request (" << hex << (void*) pClient.get() << 
                ") with err " << err.message() << ", there are now " << dec << m_setClients.size() << " requests in flight"  ;
            processRejectInstruction(transactionId, 503) ;
        }

        // already removed in reject and proxy scenarios, but be sure

    }

    void RequestHandler::processRoutingInstructions(const string& transactionId, const string& body) {
        DR_LOG(log_debug) << "RequestHandler::processRoutingInstructions: " << body ;


        // clean up needed?  not in reject scenarios, nor redirect nor route (proxy?)
        //m_pController->getPendingRequestController()->findAndRemove( transactionId ) ;

    }

    void RequestHandler::processRejectInstruction(const string& transactionId, unsigned int status, const char* reason) {
        string headers;
        string body ;
        std::ostringstream statusLine ;

        statusLine << "SIP/2.0 " << status << " " ;
        if( reason ) {
            statusLine << reason ;
        }
        else {
            boost::unordered_map<unsigned int, std::string>::const_iterator it = responseReasons.find(status) ;
            if( it != responseReasons.end() ) {
                statusLine << it->second ;
            }
        }
        if(( !m_pController->getDialogController()->respondToSipRequest( "", transactionId, statusLine.str(), headers, body) )) {
            DR_LOG(log_error) << "RequestHandler::processRejectInstruction - error sending rejection with status " << status ;
        }
    }
    

    void RequestHandler::stop() {
        m_ioservice.stop() ;
        m_thread.join() ;
    }

 }
