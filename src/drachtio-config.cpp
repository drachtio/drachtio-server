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
#include <algorithm>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/algorithm/string.hpp>    
#include <boost/algorithm/string/split.hpp>

#include "drachtio-config.hpp"

#include "controller.hpp"

using namespace std ;
using boost::property_tree::ptree;
using boost::property_tree::ptree_error;

namespace drachtio {

     class DrachtioConfig::Impl {
    public:
        Impl( const char* szFilename, bool isDaemonized) : m_bIsValid(false), m_adminTcpPort(0), m_adminTlsPort(0), m_bDaemon(isDaemonized), 
        m_bConsoleLogger(false), m_captureHepVersion(3), m_mtu(0), m_bAggressiveNatDetection(false), 
        m_prometheusPort(0), m_prometheusAddress("0.0.0.0"), m_tcpKeepalive(45), m_minTlsVersion(0) {

            // default timers
            m_nTimerT1 = 500 ;
            m_nTimerT2 = 4000 ;
            m_nTimerT4 = 5000 ;
            m_nTimerT1x64 = 32000 ;

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
                    m_adminTcpPort = pt.get<unsigned int>("drachtio.admin.<xmlattr>.port", 9022) ;
                    m_adminTlsPort = pt.get<unsigned int>("drachtio.admin.<xmlattr>.tls-port", 0) ;
                    m_secret = pt.get<string>("drachtio.admin.<xmlattr>.secret", "admin") ;
                    m_adminAddress = pt.get<string>("drachtio.admin") ;
                    string tlsValue =  pt.get<string>("drachtio.admin.<xmlattr>.tls", "false") ;
                    m_tcpKeepalive = pt.get<unsigned int>("drachtio.admin.<xmlattr>.tcp-keepalive", 45);
                } catch( boost::property_tree::ptree_bad_path& e ) {
                    cerr << "XML tag <admin> not found; this is required to provide admin socket details" << endl ;
                    return ;
                }


                /* sip contacts */

                // old way: a single contact
                try {
                    string strUrl = pt.get<string>("drachtio.sip.contact") ;
                    m_vecTransports.push_back( std::make_shared<SipTransport>(strUrl) );

                } catch( boost::property_tree::ptree_bad_path& e ) {

                    //good, hopefull they moved to the new way: a parent <contacts> tag containing multiple contacts
                    try {
                         BOOST_FOREACH(ptree::value_type &v, pt.get_child("drachtio.sip.contacts")) {
                            // v.first is the name of the child.
                            // v.second is the child tree.
                            if( 0 == v.first.compare("contact") ) {
                                string external = v.second.get<string>("<xmlattr>.external-ip","") ;            
                                string localNet = v.second.get<string>("<xmlattr>.local-net","") ;   
                                string dnsNames = v.second.get<string>("<xmlattr>.dns-names", "");      

                                std::shared_ptr<SipTransport> p = std::make_shared<SipTransport>(v.second.data(), localNet, external) ;
                                vector<string> names;
                                boost::split(names, dnsNames, boost::is_any_of(",; "));
                                for (vector<string>::iterator it = names.begin(); it != names.end(); ++it) {
                                    if( !(*it).empty() ) {
                                        p->addDnsName((*it)) ;
                                    }
                                }

                                m_vecTransports.push_back(p);
                            }
                        }
                    } catch( boost::property_tree::ptree_bad_path& e ) {
                        //neither <contact> nor <contacts> found: presumably will be provided on command line
                    }
                }

                m_sipOutboundProxy = pt.get<string>("drachtio.sip.outbound-proxy", "") ;

                // capture server
                try {
                    pt.get_child("drachtio.sip.capture-server") ; // will throw if doesn't exist
                    m_captureServerPort = pt.get<unsigned int>("drachtio.sip.capture-server.<xmlattr>.port", 9060) ;
                    m_captureServerAgentId = pt.get<uint32_t>("drachtio.sip.capture-server.<xmlattr>.id", 0) ;
                    m_captureHepVersion = pt.get<unsigned int>("drachtio.sip.capture-server.<xmlattr>.hep-version", 3) ;
                    m_captureServerAddress = pt.get<string>("drachtio.sip.capture-server") ;

                    if (0 == m_captureServerAddress.length() || 0 == m_captureServerAgentId) {
                        cerr << "invalid capture-server config: address or agent id is missing" << endl;
                        m_captureServerAddress = "";
                    }
                } catch( boost::property_tree::ptree_bad_path& e ) {
                }

                // user agent which if we see in an OPTIONS request, we respond 200 OK
                try {
                    pt.get_child("drachtio.sip.user-agent-options-auto-respond") ; // will throw if doesn't exist
                    m_autoAnswerOptionsUserAgent = pt.get<string>("drachtio.sip.user-agent-options-auto-respond") ;
                } catch( boost::property_tree::ptree_bad_path& e ) {
                }

                // redis blacklist server
                try {
                    pt.get_child("drachtio.sip.blacklist") ; // will throw if doesn't exist
                    m_redisAddress = pt.get<string>("drachtio.sip.blacklist.redis-address", "") ;
                    m_redisPort = pt.get<string>("drachtio.sip.blacklist.redis-port", "6379") ;
                    m_redisKey = pt.get<string>("drachtio.sip.blacklist.redis-key", "") ;
                    m_redisRefreshSecs = pt.get<unsigned int>("drachtio.sip.blacklist.refresh-secs", 0) ;

                    if (0 == m_redisAddress.length() || 0 == m_redisKey.length()) {
                        cerr << "invalid blacklist config: redis-address or redis-key is missing" << endl;
                        m_redisAddress = "";
                    }
                } catch( boost::property_tree::ptree_bad_path& e ) {
                }
                

                try {
                    string nat = pt.get<string>("drachtio.sip.aggressive-nat-detection", "no") ;
                    if (0 == nat.compare("yes") || 0 == nat.compare("YES") || 
                        0 == nat.compare("true") || 0 == nat.compare("TRUE") ||
                         0 == nat.compare("on") || 0 == nat.compare("ON")) {
                        m_bAggressiveNatDetection = true;
                    }
                } catch( boost::property_tree::ptree_bad_path& e) {
                }

                m_minTlsVersion = pt.get<float>("drachtio.sip.tls.min-tls-version", 0);
                m_tlsKeyFile = pt.get<string>("drachtio.sip.tls.key-file", "") ;
                m_tlsCertFile = pt.get<string>("drachtio.sip.tls.cert-file", "") ;
                m_tlsChainFile = pt.get<string>("drachtio.sip.tls.chain-file", "") ;
                m_dhParam = pt.get<string>("drachtio.sip.tls.dh-param", "") ;

                try {
                     BOOST_FOREACH(ptree::value_type &v, pt.get_child("drachtio.request-handlers")) {
                        if( 0 == v.first.compare("request-handler") ) {
                            string sipMethod = v.second.get<string>("<xmlattr>.sip-method","*") ;    
                            boost::algorithm::to_upper(sipMethod);

                            string httpMethod = v.second.get<string>("<xmlattr>.http-method","GET") ;  
                            string verifyPeer = v.second.get<string>("<xmlattr>.verify-peer","false") ;  
                            string httpUrl = v.second.data() ;

                            bool wantsVerifyPeer = (
                                0 == verifyPeer.compare("true") || 
                                0 == verifyPeer.compare("1") || 
                                0 == verifyPeer.compare("yes") ) ;

                            m_router.addRoute(sipMethod, httpMethod, httpUrl, wantsVerifyPeer) ;          
                        }
                    }
                } catch( boost::property_tree::ptree_bad_path& e ) {
                    // optional
                }

                /* monitoring */

                /* prometheus */
                try {
                    pt.get_child("drachtio.monitoring.prometheus") ; // will throw if doesn't exist
                    m_prometheusPort = pt.get<unsigned int>("drachtio.monitoring.prometheus.<xmlattr>.port") ;
                    m_prometheusAddress = pt.get<string>("drachtio.monitoring.prometheus") ;
                } catch( boost::property_tree::ptree_bad_path& e ) {
                }

                /* logging configuration  */
 
                m_nSofiaLogLevel = pt.get<unsigned int>("drachtio.logging.sofia-loglevel", 1) ;

                // syslog
                try {
                    m_syslogAddress = pt.get<string>("drachtio.logging.syslog.address") ;
                    m_sysLogPort = pt.get<unsigned int>("drachtio.logging.syslog.port", 514) ;
                    m_syslogFacility = pt.get<string>("drachtio.logging.syslog.facility") ;
                } catch( boost::property_tree::ptree_bad_path& e ) {
                }

                // file log
                try {
                    m_logFileName = pt.get<string>("drachtio.logging.file.name") ;
                    m_logArchiveDirectory = pt.get<string>("drachtio.logging.file.archive", "archive") ;
                    m_rotationSize = pt.get<unsigned int>("drachtio.logging.file.size", 50) ; // default: 50M
                    m_maxSize = pt.get<unsigned int>("drachtio.logging.file.maxSize", 1000) ;    // default: 1G
                    m_minSize = pt.get<unsigned int>("drachtio.logging.file.minSize", 0) ;      // default: no minimum
                    m_bAutoFlush = pt.get<bool>("drachtio.logging.file.auto-flush", true) ;
                    m_maxFiles = pt.get<unsigned int>("drachtio.logging.file.maxFiles", 100);   // number of rotated files to keep
                } catch( boost::property_tree::ptree_bad_path& e ) {
                }

                if( (0 == m_logFileName.length() && 0 == m_syslogAddress.length()) || pt.get_child_optional("drachtio.logging.console") ) {
                    m_bConsoleLogger = true ;
                }
                string loglevel = pt.get<string>("drachtio.logging.loglevel", "info") ;
                
                if( 0 == loglevel.compare("notice") ) m_loglevel = log_notice ;
                else if( 0 == loglevel.compare("error") ) m_loglevel = log_error ;
                else if( 0 == loglevel.compare("warning") ) m_loglevel = log_warning ;
                else if( 0 == loglevel.compare("info") ) m_loglevel = log_info ;
                else if( 0 == loglevel.compare("debug") ) m_loglevel = log_debug ;
                else m_loglevel = log_info ;                    

                // timers
                try {
                        m_nTimerT1 = pt.get<unsigned int>("drachtio.sip.timers.t1", 500) ; 
                        m_nTimerT2 = pt.get<unsigned int>("drachtio.sip.timers.t2", 4000) ; 
                        m_nTimerT4 = pt.get<unsigned int>("drachtio.sip.timers.t4", 5000) ; 
                        m_nTimerT1x64 = pt.get<unsigned int>("drachtio.sip.timers.t1x64", 32000) ; 

                } catch( boost::property_tree::ptree_bad_path& e ) {
                    if( !m_bDaemon ) {
                        cout << "invalid timer configuration" << endl ;
                    }
                }

                // spammers
                try {
                    BOOST_FOREACH(ptree::value_type &v, pt.get_child("drachtio.sip.spammers")) {
                        // v.first is the name of the child.
                        // v.second is the child tree.
                        if( 0 == v.first.compare("header") ) {
                            ptree pt = v.second ;

                            string header = pt.get<string>("<xmlattr>.name", ""); 
                            if( header.length() > 0 ) {
                                std::vector<string> vec ;
                                BOOST_FOREACH(ptree::value_type &v, pt ) {
                                    if( v.second.data().length() > 0 ) {
                                        vec.push_back( v.second.data() ) ;
                                    }
                                }
                                std::transform(header.begin(), header.end(), header.begin(), ::tolower) ;
                                m_mapSpammers.insert( make_pair( header,vec ) ) ;
                            }
                        }
                        m_actionSpammer = pt.get<string>("drachtio.sip.spammers.<xmlattr>.action", "discard") ;
                        m_tcpActionSpammer = pt.get<string>("drachtio.sip.spammers.<xmlattr>.tcp-action", "discard") ;
                    }
                } catch( boost::property_tree::ptree_bad_path& e ) {
                    //no spammer config...its optional
                }

                string cdrs = pt.get<string>("drachtio.cdrs", "") ;
                transform(cdrs.begin(), cdrs.end(), cdrs.begin(), ::tolower);
                m_bGenerateCdrs = ( 0 == cdrs.compare("true") || 0 == cdrs.compare("yes") ) ;

                m_mtu = pt.get<unsigned int>("drachtio.sip.udp-mtu", 0);
                
                fb.close() ;
                                               
                m_bIsValid = true ;
            } catch( exception& e ) {
                cerr << "Error reading configuration file: " << e.what() << endl ;
            }    
        }
        ~Impl() {
        }
                   
        bool isValid() const { return m_bIsValid; }
        bool getSyslogTarget( std::string& address, unsigned short& port ) const {
            if( m_syslogAddress.length() > 0 ) {
                address = m_syslogAddress ;
                port = m_sysLogPort  ;
                return true ;
            }
            return false ;
        }
        bool getFileLogTarget( std::string& fileName, std::string& archiveDirectory, unsigned int& rotationSize, bool& autoFlush, 
            unsigned int& maxSize, unsigned int& minSize, unsigned int& maxFiles ) {
            if( m_logFileName.length() > 0 ) {
                fileName = m_logFileName ;
                archiveDirectory = m_logArchiveDirectory ;
                rotationSize = m_rotationSize ;
                autoFlush = m_bAutoFlush;
                maxSize = m_maxSize ;
                maxFiles = m_maxFiles;
                return true ;
            }
            return false ;
        }
        bool getConsoleLogTarget() {
            return m_bConsoleLogger ;
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

        bool getSipOutboundProxy( string& sipOutboundProxy ) const {
            sipOutboundProxy = m_sipOutboundProxy ;
            return sipOutboundProxy.length() > 0 ;
        }

        bool getTlsFiles( string& tlsKeyFile, string& tlsCertFile, string& tlsChainFile, string& dhParam ) {
            tlsKeyFile = m_tlsKeyFile ;
            tlsCertFile = m_tlsCertFile ;
            tlsChainFile = m_tlsChainFile ;
            dhParam = m_dhParam;

            // both key and cert are minimally required
            return tlsKeyFile.length() > 0 && tlsCertFile.length() > 0 ;
        }
        
        bool getAdminAddress( string& address ) {
            address = m_adminAddress ;
            return !address.empty() ;
        }
        unsigned int getAdminTcpPort() {
            return m_adminTcpPort ;
        }
        unsigned int getAdminTlsPort() {
            return m_adminTlsPort ;
        }
        bool isSecret( const string& secret ) {
            return 0 == secret.compare( m_secret ) ;
        }
        bool generateCdrs(void) const {
            return m_bGenerateCdrs ;
        }

        void getTimers( unsigned int& t1, unsigned int& t2, unsigned int& t4, unsigned int& t1x64 ) {
            t1 = m_nTimerT1 ;
            t2 = m_nTimerT2 ;
            t4 = m_nTimerT4 ;
            t1x64 = m_nTimerT1x64 ;
        }

        DrachtioConfig::mapHeader2Values& getSpammers( string& action, string& tcpAction ) {
            if( !m_mapSpammers.empty() ) {
                action = m_actionSpammer ;
                tcpAction = m_tcpActionSpammer ;
            }
            return m_mapSpammers ;
        }

        void getTransports(std::vector< std::shared_ptr<SipTransport> >& transports) const {
            transports = m_vecTransports ;
        }

        void getRequestRouter( RequestRouter& router ) {
            router = m_router ;
        }

        bool getCaptureServer(string& address, unsigned int& port, uint32_t& agentId, unsigned int& version) {
            if (0 == m_captureServerAddress.length()) return false;
            
            address = m_captureServerAddress;
            port = m_captureServerPort;
            agentId = m_captureServerAgentId;
            version = m_captureHepVersion;
            return true;
        }
        
        bool getBlacklistServer(string& redisAddress, string& redisPort, string& redisKey, unsigned int& redisRefreshSecs) {
            if (0 == m_redisAddress.length()) return false;
            redisAddress = m_redisAddress;
            redisPort = m_redisPort;
            redisKey = m_redisKey;
            redisRefreshSecs = 0 == m_redisRefreshSecs ? 3600 : m_redisRefreshSecs;
            return true;
        }

        bool getAutoAnswerOptionsUserAgent(string& userAgent) {
            if (0 == m_autoAnswerOptionsUserAgent.length()) return false;
            userAgent = m_autoAnswerOptionsUserAgent;
            return true;
        }

        unsigned int getMtu() {
            return m_mtu;
        }

        bool isAggressiveNatEnabled() {
            return m_bAggressiveNatDetection;
        }

        bool getPrometheusAddress( string& address, unsigned int& port ) {
            if (0 == m_prometheusPort) return false;
            address = m_prometheusAddress;
            port = m_prometheusPort;
            return true;
        }

        unsigned int getTcpKeepalive() {
            return m_tcpKeepalive;
        }

        bool getMinTlsVersion(float& minTlsVersion) {
            if (m_minTlsVersion > 0) {
                minTlsVersion = m_minTlsVersion;
                return true;
            }
            return false;
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
        vector< pair<string,string> > m_vecSipUrl ;
        string m_sipOutboundProxy ;
        string m_syslogAddress ;
        string m_logFileName ;
        string m_logArchiveDirectory ;
        string m_tlsKeyFile ;
        string m_tlsCertFile ;
        string m_tlsChainFile ;
        string m_dhParam;
        bool m_bAutoFlush ;
        unsigned int m_rotationSize ;
        unsigned int m_maxSize ;
        unsigned int m_minSize ;
        unsigned int m_maxFiles;
        unsigned short m_sysLogPort ;
        string m_syslogFacility ;
        severity_levels m_loglevel ;
        unsigned int m_nSofiaLogLevel ;
        string m_adminAddress ;
        unsigned int m_adminTcpPort ;
        unsigned int m_adminTlsPort ;
        string m_secret ;
        bool m_bGenerateCdrs ;
        bool m_bDaemon;
        bool m_bConsoleLogger ;
        unsigned int m_nTimerT1, m_nTimerT2, m_nTimerT4, m_nTimerT1x64 ;
        string m_actionSpammer ;
        string m_tcpActionSpammer ;
        mapHeader2Values m_mapSpammers ;
        std::vector< std::shared_ptr<SipTransport> >  m_vecTransports;
        RequestRouter m_router ;
        string m_captureServerAddress ;
        unsigned int m_captureServerPort;
        uint32_t m_captureServerAgentId ;
        unsigned int m_captureHepVersion ;
        unsigned int m_mtu;
        bool m_bAggressiveNatDetection;
        string m_prometheusAddress;
        unsigned int m_prometheusPort;
        unsigned int m_tcpKeepalive;
        float m_minTlsVersion;
        string m_redisAddress;
        string m_redisPort;
        string m_redisKey;
        unsigned int m_redisRefreshSecs;
        string m_autoAnswerOptionsUserAgent;
  } ;
    
    /*
     Public interface
    */
    DrachtioConfig::DrachtioConfig( const char* szFilename, bool isDaemonized ) : m_pimpl( new Impl(szFilename, isDaemonized) ) {
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
   
    bool DrachtioConfig::getSyslogTarget( std::string& address, unsigned short& port ) const {
        return m_pimpl->getSyslogTarget( address, port ) ;
    }
    bool DrachtioConfig::getSyslogFacility( sinks::syslog::facility& facility ) const {
        return m_pimpl->getSyslogFacility( facility ) ;
    }
    bool DrachtioConfig::getFileLogTarget( std::string& fileName, std::string& archiveDirectory, unsigned int& rotationSize, 
        bool& autoFlush, unsigned int& maxSize, unsigned int& minSize, unsigned int& maxFiles ) {
        return m_pimpl->getFileLogTarget( fileName, archiveDirectory, rotationSize, autoFlush, maxSize, minSize, maxFiles ) ;
    }
    bool DrachtioConfig::getConsoleLogTarget() {
        return m_pimpl->getConsoleLogTarget() ;
    }

    bool DrachtioConfig::getSipOutboundProxy( std::string& sipOutboundProxy ) const {
        return m_pimpl->getSipOutboundProxy( sipOutboundProxy ) ;
    }

    void DrachtioConfig::Log() const {
        DR_LOG(log_notice) << "Configuration:" << endl ;
    }
    severity_levels DrachtioConfig::getLoglevel() {
        return m_pimpl->getLoglevel() ;
    }
    unsigned int DrachtioConfig::getAdminTcpPort() {
        return m_pimpl->getAdminTcpPort() ;
    }
    unsigned int DrachtioConfig::getAdminTlsPort() {
        return m_pimpl->getAdminTlsPort() ;
    }
    bool DrachtioConfig::getAdminAddress( string& address ) {
        return m_pimpl->getAdminAddress( address ) ;
    }
    bool DrachtioConfig::isSecret( const string& secret ) const {
        return m_pimpl->isSecret( secret ) ;
    }
    bool DrachtioConfig::getTlsFiles( std::string& keyFile, std::string& certFile, std::string& chainFile, std::string& dhParam ) const {
        return m_pimpl->getTlsFiles( keyFile, certFile, chainFile, dhParam ) ;
    }
    bool DrachtioConfig::generateCdrs(void) const {
        return m_pimpl->generateCdrs() ;
    }
    void DrachtioConfig::getTimers( unsigned int& t1, unsigned int& t2, unsigned int& t4, unsigned int& t1x64 ) {
        return m_pimpl->getTimers( t1, t2, t4, t1x64 ) ;
    }
    DrachtioConfig::mapHeader2Values& DrachtioConfig::getSpammers( string& action, string& tcpAction ) {
        return m_pimpl->getSpammers( action, tcpAction ) ;
    }
    void DrachtioConfig::getTransports(std::vector< std::shared_ptr<SipTransport> >& transports) const {
        return m_pimpl->getTransports(transports) ;
    }
    void DrachtioConfig::getRequestRouter( RequestRouter& router ) {
        return m_pimpl->getRequestRouter(router) ;
    }
    bool DrachtioConfig::getCaptureServer(string& address, unsigned int& port, uint32_t& agentId, unsigned int& version) {
        return m_pimpl->getCaptureServer(address, port, agentId, version);
    }
    unsigned int DrachtioConfig::getMtu() {
        return m_pimpl->getMtu();
    }
    bool DrachtioConfig::isAggressiveNatEnabled() {
        return m_pimpl->isAggressiveNatEnabled();
    }
    bool DrachtioConfig::getPrometheusAddress( string& address, unsigned int& port ) const {
        return m_pimpl->getPrometheusAddress(address, port);
    }
  
    unsigned int DrachtioConfig::getTcpKeepalive() const {
        return m_pimpl->getTcpKeepalive();
    }
        
    bool DrachtioConfig::getMinTlsVersion(float& minTlsVersion) const {
        return m_pimpl->getMinTlsVersion(minTlsVersion);
    }

    bool DrachtioConfig::getBlacklistServer(string& redisAddress, string& redisPort, string& redisKey, unsigned int& redisRefreshSecs) const {
        return m_pimpl->getBlacklistServer(redisAddress, redisPort, redisKey, redisRefreshSecs);
    }

    bool DrachtioConfig::getAutoAnswerOptionsUserAgent(string& userAgent) const {
        return m_pimpl->getAutoAnswerOptionsUserAgent(userAgent);
    }


}
