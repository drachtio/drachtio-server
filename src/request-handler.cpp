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

#include <jansson.h>

#include "sofia-sip/url.h"

#include "request-handler.hpp"
#include "controller.hpp"

namespace {
    inline bool is_ssl_short_read_error(boost::system::error_code err) {
        return err.category() == boost::asio::error::ssl_category &&
            (err.value() == 335544539 || //short read error
            err.value() == 336130329); //decryption failed or bad record mac
    }
}
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
    template <class T>
    RequestHandler::Client<T>::Client(boost::shared_ptr<RequestHandler> pRequestHandler, const string& transactionId,
        const std::string& server, const std::string& path, const std::string& service, const string& httpMethod) : 
            pRequestHandler_(pRequestHandler),
            m_ctx(boost::asio::ssl::context::sslv23),
            resolver_(pRequestHandler->getIOService()), 
            socket_(pRequestHandler->getIOService()),
            status_code_(0), transactionId_(transactionId), m_verifyPeer(false), serverName_(server) {

            DR_LOG(log_debug) << "RequestHandler::Client::Client: Host (http): " << server << " GET " << path ;

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

    template <>
    void RequestHandler::Client< boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >::handle_connect(const boost::system::error_code& err) {
        if (!(err_ = err)) {
            // The connection was successful. Send the request.
            
            socket_.async_handshake(boost::asio::ssl::stream_base::client,
                boost::bind(&Client::handle_handshake, this, boost::asio::placeholders::error));
        }
        else
        {
          DR_LOG(log_error) << "RequestHandler::Client::handle_connect: (" << (void*) this << ") Error: " << err.message() ;
          wrapUp() ;
        }
    }

    template <>
    void RequestHandler::Client< boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >::handle_resolve(const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator) {
        if (!(err_ = err)) {
            socket_.set_verify_mode(boost::asio::ssl::verify_peer);

            if( !m_verifyPeer ) {
                // just log
                socket_.set_verify_callback(
                    boost::bind(&Client::verify_certificate, this, _1, _2));
            }

            boost::asio::async_connect(socket_.lowest_layer(), endpoint_iterator,
                boost::bind(&Client::handle_connect, this, boost::asio::placeholders::error));
        }
        else
        {
          DR_LOG(log_error) << "RequestHandler::Client::handle_resolve: Error: " << err.message() ;
          wrapUp() ;
        }
    }


    template <class T>
    RequestHandler::Client<T>::Client(boost::shared_ptr<RequestHandler> pRequestHandler, const string& transactionId,
        const std::string& server, const std::string& path, const std::string& service, const string& httpMethod, 
        boost::asio::ssl::context_base::method m, bool verifyPeer) : 
            pRequestHandler_(pRequestHandler),
            m_ctx(m),
            resolver_(pRequestHandler->getIOService()), 
            socket_(pRequestHandler->getIOService(), m_ctx),
            status_code_(0), transactionId_(transactionId), serverName_(server),
            m_verifyPeer(verifyPeer) {

            SSL_set_tlsext_host_name(socket_.native_handle(), server.c_str());
            socket_.set_verify_mode(boost::asio::ssl::verify_peer);

            if( verifyPeer ) {
                socket_.set_verify_callback(boost::asio::ssl::rfc2818_verification(server.c_str()));
                m_ctx.set_default_verify_paths();
            }

            DR_LOG(log_debug) << "RequestHandler::Client::Client: Host (https): " << server << " GET " << path ;

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

    template <class T>
    void RequestHandler::Client<T>::handle_resolve(const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator) {
        if (!(err_ = err)) {
            boost::asio::async_connect(socket_, endpoint_iterator, 
                boost::bind(&Client::handle_connect, this, boost::asio::placeholders::error));                
        }
        else
        {
          DR_LOG(log_error) << "RequestHandler::Client::handle_resolve: Error: " << err.message() ;
          wrapUp() ;
        }
    }

    template <class T>
    bool RequestHandler::Client<T>::verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx) {
        // The verify callback can be used to check whether the certificate that is
        // being presented is valid for the peer. For example, RFC 2818 describes
        // the steps involved in doing this for HTTPS. Consult the OpenSSL
        // documentation for more details. Note that the callback is called once
        // for each certificate in the certificate chain, starting from the root
        // certificate authority.

        // In this example we will simply print the certificate's subject name.
        
        char subject_name[256];
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
        DR_LOG(log_debug) << "RequestHandler::Client::verify_certificate - Verifying " << subject_name ;

        //return preverified;   
        return true;     
/*
        int8_t subject_name[256];
        X509_STORE_CTX *cts = ctx.native_handle();
        int32_t length = 0;
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        DR_LOG(log_debug) << "CTX ERROR : " << cts->error;

        int32_t depth = X509_STORE_CTX_get_error_depth(cts);
        DR_LOG(log_debug) << "CTX DEPTH : " << depth ;

        switch (cts->error)
        {
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
            DR_LOG(log_debug) <<  "X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT";
            break;
        case X509_V_ERR_CERT_NOT_YET_VALID:
        case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
            DR_LOG(log_debug) <<  "Certificate not yet valid!!";
            break;
        case X509_V_ERR_CERT_HAS_EXPIRED:
        case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
            DR_LOG(log_debug) <<  "Certificate expired..";
            break;
        case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
            DR_LOG(log_debug) <<  "Self signed certificate in chain!!!";
            preverified = true;
            break;
        default:
            break;
        }
        const int32_t name_length = 256;
        X509_NAME_oneline(X509_get_subject_name(cert), reinterpret_cast<char*>(subject_name), name_length);
        DR_LOG(log_debug) <<  "Verifying " << subject_name;
        DR_LOG(log_debug) <<  "Verification status " << preverified;
        return preverified ;       
*/
    }

    template <class T>
    void RequestHandler::Client<T>::handle_connect(const boost::system::error_code& err) {
        if (!(err_ = err)) {
            // The connection was successful. Send the request.
            boost::asio::async_write(socket_, request_,
                boost::bind(&Client::handle_write_request, this, boost::asio::placeholders::error));
        }
        else
        {
          DR_LOG(log_error) << "RequestHandler::Client::handle_connect: (" << (void*) this << ") Error: " << err.message() ;
          wrapUp() ;
        }
    }

    template <class T>
    void RequestHandler::Client<T>::handle_handshake(const boost::system::error_code& error)
    {
        if (!error)
        {
            DR_LOG(log_debug) << "RequestHandler::Client::handle_handshake - Handshake OK ";

            // The handshake was successful. Send the request.
            boost::asio::async_write(socket_, request_,
                boost::bind(&Client::handle_write_request, this, boost::asio::placeholders::error));
        }
        else
        {
            DR_LOG(log_error) << "RequestHandler::Client::handle_handshake - Handshake failed to server " << serverName_ << ": " << error.message() ;
            wrapUp() ;
        }
    }

    template <class T>
    void RequestHandler::Client<T>::handle_write_request(const boost::system::error_code& err) {
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

    template <class T>
    void RequestHandler::Client<T>::handle_read_status_line(const boost::system::error_code& err) {
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

            DR_LOG(log_debug) << "RequestHandler::Client::handle_read_status_line: returned status code " << std::dec << status_code_;

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

    template <class T>
    void RequestHandler::Client<T>::handle_read_headers(const boost::system::error_code& err) {
        if (!(err_ = err)) {
            // Process the response headers.
            std::istream response_stream(&response_);
            std::string header;
            while (std::getline(response_stream, header) && header != "\r") {
                DR_LOG(log_debug) << header ;
            }

            // Write whatever content we already have to output.
            if (response_.size() > 0) {
                body_ << &response_ ;
                //DR_LOG(log_debug) << "initial body: " << body_.str() ;
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

    template <class T>
    void RequestHandler::Client<T>::handle_read_content(const boost::system::error_code& err) {
        if (!(err_ = err)) {
          // Write all of the data that has been read so far.
          //DR_LOG(log_debug) << "RequestHandler::Client::handle_read_content - read some content "  ;

          body_ << &response_ ;

          // Continue reading remaining data until EOF.
          boost::asio::async_read(socket_, response_,
              boost::asio::transfer_at_least(1),
              boost::bind(&Client::handle_read_content, this,
                boost::asio::placeholders::error));
        }
        else if (err != boost::asio::error::eof && !is_ssl_short_read_error(err) ) {
            
            DR_LOG(log_error) << "RequestHandler::Client::handle_read_content Error: " << err.message() ;
            wrapUp() ;
        }
        else {
            wrapUp() ;
        }
    }

    template <class T>
    void RequestHandler::Client<T>::wrapUp() {
        pRequestHandler_->requestCompleted( this->shared_from_this(), err_, status_code_, body_.str()) ;
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
        const string& encodedMessage, vector< pair<string, string> >& vecParams, bool verifyPeer) {

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

        path.assign("/");
        path.append( NULL != url.url_path ? url.url_path : "");
        path.append("?") ;
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

        
        if( 0 == strcmp("https", url.url_scheme) ) {
            boost::asio::ssl::context ctx(boost::asio::ssl::context::sslv23);

            boost::shared_ptr< Client<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > > pClient = 
                boost::make_shared< Client<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > >(shared_from_this(), 
                    transactionId, 
                    server, 
                    path, 
                    service, 
                    httpMethod,
                    boost::asio::ssl::context::sslv23, 
                    verifyPeer) ;            
            m_setSslClients.insert( pClient ) ;
        }
        else {
            boost::shared_ptr< Client<tcp::socket> > pClient = 
                boost::make_shared< Client<tcp::socket> >(shared_from_this(), 
                    transactionId, 
                    server, 
                    path, 
                    service, 
                    httpMethod) ;            
            m_setClients.insert( pClient ) ;
            DR_LOG(log_info) << "RequestHandler::processRequest - started (" << hex << (void*) pClient.get() << ") " << httpMethod << " request to " << url.url_scheme << "://" << server << path 
                << ", there are now " << m_setClients.size() << " requests in flight" ;
        }

    }

    void RequestHandler::requestCompleted( boost::shared_ptr< Client< boost::asio::ssl::stream< boost::asio::ip::tcp::socket> > > pClient, 
        const boost::system::error_code& err, unsigned int status_code, const string& body) {
        string transactionId = pClient->getTransactionId() ;

        m_setSslClients.erase( pClient ) ;

        finishRequest(transactionId, err, status_code, body);

    }

    void RequestHandler::requestCompleted(boost::shared_ptr< Client<tcp::socket> > pClient, const boost::system::error_code& err, unsigned int status_code, const string& body) {
        string transactionId = pClient->getTransactionId() ;

        m_setClients.erase( pClient ) ;

        finishRequest(transactionId, err, status_code, body);
    }
    void RequestHandler::finishRequest(const string& transactionId, const boost::system::error_code& err, 
        unsigned int status_code, const string& body) {

        if( 200 == status_code && !body.empty() ) {
            DR_LOG(log_debug) << "RequestHandler::finishRequest received instructions: " << body ;
            processRoutingInstructions(transactionId, body) ;
        }
        else if( 200 != status_code && 0 != status_code ) {
            DR_LOG(log_debug) << "RequestHandler::finishRequest returne non-success status: " << status_code ;
            processRejectInstruction(transactionId, status_code) ;
        }
        else {
            processRejectInstruction(transactionId, 503) ;
        }
    }

    void RequestHandler::processRoutingInstructions(const string& transactionId, const string& body) {
        DR_LOG(log_debug) << "RequestHandler::processRoutingInstructions: " << body ;

        std::ostringstream msg ;
        json_t *root;
        json_error_t error;
        root = json_loads(body.c_str(), 0, &error);

        try {
            if( !root ) {
                msg << "error parsing body as JSON on line " << error.line  << ": " << error.text ;
                return throw std::runtime_error(msg.str()) ;  
            }

            if(!json_is_object(root)) {
                throw std::runtime_error("expected JSON object but got something else") ;  
            }

            json_t* action = json_object_get( root, "action") ;
            json_t* data = json_object_get(root, "data") ;
            if( !json_is_string(action) ) {
                throw std::runtime_error("missing or invalid 'action' attribute") ;  
            }
            if( !json_is_object(data) ) {
                throw std::runtime_error("missing 'data' object") ;  
            }
            const char* actionText = json_string_value(action) ;
            if( 0 == strcmp("reject", actionText)) {
                json_t* status = json_object_get(data, "status") ;
                json_t* reason = json_object_get(data, "reason") ;

                if( !status || !json_is_number(status) ) {
                    throw std::runtime_error("'status' is missing or is not a number") ;  
                }
                processRejectInstruction(transactionId, json_integer_value(status), json_string_value(reason));
            }
            else if( 0 == strcmp("proxy", actionText)) {
                bool recordRoute = false ;
                bool followRedirects = true ;
                bool simultaneous = false ;
                string provisionalTimeout = "5s";
                string finalTimeout = "60s";
                vector<string> vecDestination ;

                json_t* rr = json_object_get(data, "recordRoute") ;
                if( rr && json_is_boolean(rr) ) {
                    recordRoute = json_boolean_value(rr) ;
                }

                json_t* follow = json_object_get(data, "followRedirects") ;
                if( follow && json_is_boolean(follow) ) {
                    followRedirects = json_boolean_value(follow) ;
                }

                json_t* sim = json_object_get(data, "simultaneous") ;
                if( sim && json_is_boolean(sim) ) {
                    simultaneous = json_boolean_value(sim) ;
                }

                json_t* pTimeout = json_object_get(data, "provisionalTimeout") ;
                if( pTimeout && json_is_string(pTimeout) ) {
                    provisionalTimeout = json_string_value(pTimeout) ;
                }

                json_t* fTimeout = json_object_get(data, "finalTimeout") ;
                if( fTimeout && json_is_string(fTimeout) ) {
                    finalTimeout = json_string_value(fTimeout) ;
                }

                json_t* destination = json_object_get(data, "destination") ;
                if( json_is_string(destination) ) {
                    vecDestination.push_back( json_string_value(destination) ) ;
                }
                else if( json_is_array(destination) ) {
                    size_t size = json_array_size(destination);
                    for( unsigned int i = 0; i < size; i++ ) {
                        json_t* aDest = json_array_get(destination, i);
                        if( !json_is_string(aDest) ) {
                            throw std::runtime_error("RequestHandler::processRoutingInstructions - invalid 'contact' array: must contain strings") ;  
                        }
                        vecDestination.push_back( json_string_value(aDest) );
                    }
                }

                processProxyInstruction(transactionId, recordRoute, followRedirects, 
                    simultaneous, provisionalTimeout, finalTimeout, vecDestination) ;
            }
            else if( 0 == strcmp("redirect", actionText)) {
                json_t* contact = json_object_get(data, "contact") ;
                vector<string> vecContact ;

                if( json_is_string(contact) ) {
                    vecContact.push_back( json_string_value(contact) ) ;
                }
                else if( json_is_array(contact) ) {
                    size_t size = json_array_size(contact);
                    for( unsigned int i = 0; i < size; i++ ) {
                        json_t* aContact = json_array_get(contact, i);
                        if( !json_is_string(aContact) ) {
                            throw std::runtime_error("RequestHandler::processRoutingInstructions - invalid 'contact' array: must contain strings") ;  
                        }
                        vecContact.push_back( json_string_value(aContact) );
                    }
                }
                else {
                    throw std::runtime_error("RequestHandler::processRoutingInstructions - invalid 'contact' attribute in redirect action: must be string or array") ;  
                }
                processRedirectInstruction(transactionId, vecContact);

            }
            else if( 0 == strcmp("route", actionText)) {

            }
            else {
                msg << "RequestHandler::processRoutingInstructions - invalid 'action' attribute value '" << actionText << 
                    "': valid values are 'reject', 'proxy', 'redirect', and 'route'" ;
                return throw std::runtime_error(msg.str()) ;  
            }

            json_decref(root) ; 
        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << "RequestHandler::processRoutingInstructions " << err.what();
            DR_LOG(log_error) << body ;
            processRejectInstruction(transactionId, 500) ;
            if( root ) { 
                json_decref(root) ; 
            }
        }

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
    void RequestHandler::processRedirectInstruction(const string& transactionId, vector<string>& vecContact) {
        string headers;
        string body ;

        int i = 0 ;
        BOOST_FOREACH(string& c, vecContact) {
            if( i++ > 0 ) {
                headers.append("\n");
            }
            headers.append("Contact: ") ;
            headers.append(c) ;
        }

        if(( !m_pController->getDialogController()->respondToSipRequest( "", transactionId, "SIP/2.0 302 Moved", headers, body) )) {
            DR_LOG(log_error) << "RequestHandler::processRedirectInstruction - error sending redirect" ;
        }
    }

    void RequestHandler::processProxyInstruction(const string& transactionId, bool recordRoute, bool followRedirects, 
        bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, vector<string>& vecDestination) {
        string headers;
        string body ;

        m_pController->getProxyController()->proxyRequest( "", transactionId, recordRoute, false, 
            followRedirects, 
            simultaneous, provisionalTimeout, finalTimeout, vecDestination, headers ) ;
    }

    void RequestHandler::stop() {
        m_ioservice.stop() ;
        m_thread.join() ;
    }

 }
