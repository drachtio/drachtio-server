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
#ifndef __DRACHTIO_CONFIG__H__
#define __DRACHTIO_CONFIG__H__


#include <iostream>
#include <unordered_map>

#include <boost/property_tree/ptree.hpp>

#include "drachtio.h"
#include "sip-transports.hpp"
#include "request-router.hpp"

using namespace std ;

namespace drachtio {


    class DrachtioConfig {
    public:

        DrachtioConfig( const char* szFilename, bool isDaemonized = true ) ;
        ~DrachtioConfig() ;

        DrachtioConfig( const DrachtioConfig& ) = delete;
        
       typedef unordered_map<string, vector<string > > mapHeader2Values ;

        bool isValid() ;

        void getTransports(std::vector< std::shared_ptr<SipTransport> >& transports) const ;

        bool getSipOutboundProxy( string& sipOutboundProxy ) const ;
        bool getSyslogTarget( string& address, unsigned short& port ) const ;
        bool getSyslogFacility( sinks::syslog::facility& facility ) const ;

        bool getFileLogTarget( string& fileName, string& archiveDirectory, unsigned int& rotationSize, bool& autoFlush, unsigned int& maxSize, unsigned int& minSize ) ;

        bool getConsoleLogTarget() ;

        bool isSecret( const string& secret ) const ;
        severity_levels getLoglevel() ;
        unsigned int getSofiaLogLevel(void) ;

        unsigned int getMtu(void);

        bool getAdminAddress( string& address ) ;
        unsigned int getAdminTcpPort( void ) ;
        unsigned int getAdminTlsPort( void ) ;

        bool getTlsFiles( string& keyFile, string& certFile, string& chainFile, string& dhParam ) const ;

        bool generateCdrs(void) const ;

        void getTimers( unsigned int& t1, unsigned int& t2, unsigned int& t4, unsigned int& t1x64 ) ;

        mapHeader2Values& getSpammers( string& action, string& tcpAction ) ;

        void getRequestRouter( RequestRouter& router ) ;

        bool getCaptureServer(string& address, unsigned int& port, uint32_t& agentId, unsigned int& version);

        bool isAggressiveNatEnabled(void);   

        bool getPrometheusAddress( string& address, unsigned int& port ) const ;

        unsigned int getTcpKeepalive() const;

        void Log() const ;
        
    private:
        DrachtioConfig() {}  //prohibited
        
        class Impl ;
        Impl* m_pimpl ;
    } ;
}





#endif
