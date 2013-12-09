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
#include <fstream>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include "drachtio-config.hpp"

#include "controller.hpp"

using namespace std ;
using boost::property_tree::ptree;
using boost::property_tree::ptree_error;

namespace drachtio {

     class DrachtioConfig::Impl {
    public:
        Impl( const char* szFilename) : m_bIsValid(false), m_adminPort(0) {
            try {
                std::filebuf fb;
                if( !fb.open (szFilename,std::ios::in) ) {
                    cerr << "Unable to open configuration file: " << szFilename << endl ;
                    return ;
                }
                std::istream is(&fb);
                
                ptree pt;
                read_xml(is, pt);
                
                /* admin configuration */
                try {
                    pt.get_child("drachtio.admin") ; // will throw if doesn't exist
                    m_adminPort = pt.get<unsigned int>("drachtio.admin.<xmlattr>.port", 8022) ;
                    m_secret = pt.get<string>("drachtio.admin.<xmlattr>.secret", "admin") ;
                    m_adminAddress = pt.get<string>("drachtio.admin") ;

                    m_sipUrl = pt.get<string>("drachtio.sip.contact", "sip:*") ;



                } catch( boost::property_tree::ptree_bad_path& e ) {
                }
                
                /* logging configuration  */
                m_syslogAddress = pt.get<string>("drachtio.logging.syslog.address", "localhost") ;
                m_sysLogPort = pt.get<unsigned int>("drachtio.logging.syslog.address", 516) ;
                m_syslogFacility = pt.get<string>("drachtio.logging.syslog.facility","local7") ;
                m_nSofiaLogLevel = pt.get<unsigned int>("drachtio.logging.sofia-loglevel", 1) ;
               
                string loglevel = pt.get<string>("drachtio.logging.loglevel", "info") ;
                
                if( 0 == loglevel.compare("notice") ) m_loglevel = log_notice ;
                else if( 0 == loglevel.compare("error") ) m_loglevel = log_error ;
                else if( 0 == loglevel.compare("warning") ) m_loglevel = log_warning ;
                else if( 0 == loglevel.compare("info") ) m_loglevel = log_info ;
                else if( 0 == loglevel.compare("debug") ) m_loglevel = log_debug ;
                else m_loglevel = log_info ;
                
                fb.close() ;
                                               
                m_bIsValid = true ;
            } catch( exception& e ) {
                cerr << "Error reading configuration file: " << e.what() << endl ;
            }    
        }
        ~Impl() {
        }
                   
        bool isValid() const { return m_bIsValid; }
        const string& getSyslogAddress() const { return m_syslogAddress; }
        unsigned int getSyslogPort() const { return m_sysLogPort ; }        
        bool getSyslogTarget( std::string& address, unsigned int& port ) const {
            address = m_syslogAddress ;
            port = m_sysLogPort  ;
            return true ;
        }
		severity_levels getLoglevel() {
			return m_loglevel ;
		}
        unsigned int getSofiaLogLevel(void) { return m_nSofiaLogLevel; }
      
        bool getSyslogFacility( sinks::syslog::facility& facility ) const {
        
            if( m_syslogFacility.empty() ) return false ;
            
            if( m_syslogFacility == "local0" ) facility = sinks::syslog::local0 ;
            else if( m_syslogFacility == "local1" ) facility = sinks::syslog::local1 ;
            else if( m_syslogFacility == "local2" ) facility = sinks::syslog::local2 ;
            else if( m_syslogFacility == "local3" ) facility = sinks::syslog::local3 ;
            else if( m_syslogFacility == "local4" ) facility = sinks::syslog::local4 ;
            else if( m_syslogFacility == "local5" ) facility = sinks::syslog::local5 ;
            else if( m_syslogFacility == "local6" ) facility = sinks::syslog::local6 ;
            else if( m_syslogFacility == "local7" ) facility = sinks::syslog::local7 ;
            else return false ;
            
            return true ;
        }
        const string& getSipUrl() const { return m_sipUrl; }
        
        unsigned int getAdminPort( string& address ) {
            address = m_adminAddress ;
            return m_adminPort ;
        }
        bool isSecret( const string& secret ) {
            return 0 == secret.compare( m_secret ) ;
        }
 
    private:
        
        bool getXmlAttribute( ptree::value_type const& v, const string& attrName, string& value ) {
            try {
                string key = "<xmlattr>." ;
                key.append( attrName ) ;
                value = v.second.get<string>( key ) ;
            } catch( const ptree_error& err ) {
                return false ;
            }
            if( value.empty() ) return false ;
            return true ;
        }
    
        
        
        bool m_bIsValid ;
        string m_sipUrl ;
        string m_syslogAddress ;
        unsigned int m_sysLogPort ;
        string m_syslogFacility ;
        severity_levels m_loglevel ;
        unsigned int m_nSofiaLogLevel ;
        string m_adminAddress ;
        unsigned int m_adminPort ;
        string m_secret ;
  } ;
    
    /*
     Public interface
    */
    DrachtioConfig::DrachtioConfig( const char* szFilename ) : m_pimpl( new Impl(szFilename) ) {
    }
    
    DrachtioConfig::~DrachtioConfig() {
       delete m_pimpl ;
    }
    
    bool DrachtioConfig::isValid() {
        return m_pimpl->isValid() ;
    }
    unsigned int DrachtioConfig::getSofiaLogLevel() {
        return m_pimpl->getSofiaLogLevel() ;
    }
   
    bool DrachtioConfig::getSyslogTarget( std::string& address, unsigned int& port ) const {
        return m_pimpl->getSyslogTarget( address, port ) ;
    }
    bool DrachtioConfig::getSyslogFacility( sinks::syslog::facility& facility ) const {
        return m_pimpl->getSyslogFacility( facility ) ;
    }
    bool DrachtioConfig::getSipUrl( std::string& sipUrl ) const {
        sipUrl = m_pimpl->getSipUrl() ;
        return true ;
    }

    void DrachtioConfig::Log() const {
        DR_LOG(log_notice) << "Configuration:" << endl ;
    }
    severity_levels DrachtioConfig::getLoglevel() {
        return m_pimpl->getLoglevel() ;
    }
    unsigned int DrachtioConfig::getAdminPort( string& address ) {
        return m_pimpl->getAdminPort( address ) ;
    }
    bool DrachtioConfig::isSecret( const string& secret ) const {
        return m_pimpl->isSecret( secret ) ;
    }
 
}
