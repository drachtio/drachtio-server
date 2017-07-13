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
#ifndef __REQUEST_HANDLER_H__
#define __REQUEST_HANDLER_H__


#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/unordered_set.hpp>

#include "drachtio.h"

using boost::asio::ip::tcp;

using namespace std ;

namespace drachtio {
    
    class RequestHandler : public boost::enable_shared_from_this<RequestHandler>  {
    public:

        class Client : public boost::enable_shared_from_this<Client>{
        public:
            Client(boost::shared_ptr<RequestHandler> pRequestHandler, const string& transactionId, const std::string& server, const std::string& path, 
                const std::string& service, const string& httpMethod) ;
            ~Client() {}

            const string& getServer(void) { return server_; }
            const string& getPath(void) { return path_; }
            const string& getTransactionId(void) { return transactionId_;}

        private:
            void handle_resolve(const boost::system::error_code& err, tcp::resolver::iterator endpoint_iterator) ;
            void handle_connect(const boost::system::error_code& err) ;
            void handle_write_request(const boost::system::error_code& err) ;
            void handle_read_status_line(const boost::system::error_code& err) ;
            void handle_read_headers(const boost::system::error_code& err) ;
            void handle_read_content(const boost::system::error_code& err) ;
            void wrapUp(void) ;

            boost::shared_ptr<RequestHandler> pRequestHandler_ ;
            tcp::resolver resolver_;
            tcp::socket socket_;
            boost::asio::streambuf request_;
            boost::asio::streambuf response_;
            std::ostringstream body_ ;
            string server_ ;
            string path_ ;
            unsigned int status_code_ ;
            boost::system::error_code err_ ;
            string transactionId_ ;
        } ;


        RequestHandler( DrachtioController* pController ) ;
        ~RequestHandler() ;
        
        boost::asio::io_service& getIOService(void) { return m_ioservice ;}

        void processRequest(const string& transactionId, const string& httpMethod, const string& httpUrl, const string& encodedMessage, vector< pair<string, string> >& vecParams) ;

        void requestCompleted( boost::shared_ptr<Client> pClient, const boost::system::error_code& err, unsigned int status_code, const string& body) ;

        void threadFunc(void) ;

    protected:
        void stop() ;
        void processRoutingInstructions(const string& transactionId, const string& body) ;
        void processRejectInstruction(const string& transactionId, unsigned int status, const char* reason = NULL) ;

    private:

        DrachtioController*         m_pController ;
        boost::thread               m_thread ;
        boost::mutex                m_lock ;

        boost::asio::io_service m_ioservice;

        typedef boost::unordered_set< boost::shared_ptr<Client> > set_of_active_requests ;
        set_of_active_requests      m_setClients ;

    } ;

}  

#endif
