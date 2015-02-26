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
#include <boost/property_tree/ptree.hpp>
#include <boost/noncopyable.hpp>

#include "drachtio.h"

using namespace std ;

namespace drachtio {
    
    class DrachtioConfig : private boost::noncopyable {
    public:
        DrachtioConfig( const char* szFilename ) ;
        ~DrachtioConfig() ;
        
        bool isValid() ;

        bool getSipUrl( std::string& sipUrl ) const ;
        bool getSyslogTarget( std::string& address, unsigned int& port ) const ;
        bool getSyslogFacility( sinks::syslog::facility& facility ) const ;

        bool getFileLogTarget( std::string& fileName, std::string& archiveDirectory, unsigned int& rotationSize, bool& autoFlush ) ;

        bool isSecret( const string& secret ) const ;
        severity_levels getLoglevel() ;
        unsigned int getSofiaLogLevel(void) ;
 
         unsigned int getAdminPort( string& address ) ;

         bool getRedisAddress( std::string& address, unsigned int& port ) const ;
       
        void Log() const ;
        
    private:
        DrachtioConfig() {}  //prohibited
        
        class Impl ;
        Impl* m_pimpl ;
    } ;
}





#endif
