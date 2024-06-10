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
#include <getopt.h>
#include <assert.h>
#include <pwd.h>
#include <algorithm>
#include <functional>
#include <regex>
#include <cstdlib>

#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/support/date_time.hpp>

#include <boost/log/core.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/assign/list_of.hpp>

#include <boost/lambda/lambda.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/trivial.hpp>

#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>

#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <jansson.h>

#include <sofia-sip/msg_addr.h>
#include <sofia-sip/sip_util.h>

#define DEFAULT_CONFIG_FILENAME "/etc/drachtio.conf.xml"
#define DEFAULT_HOMER_PORT (9060)
#define MAXLOGLEN (8192)
/* from sofia */
#define MSG_SEPARATOR \
"------------------------------------------------------------------------\n"

namespace drachtio {
    class DrachtioController ;
}

#define SU_ROOT_MAGIC_T drachtio::DrachtioController
#define NTA_AGENT_MAGIC_T drachtio::DrachtioController
#define NTA_LEG_MAGIC_T drachtio::DrachtioController

#include "cdr.hpp"
#include "controller.hpp"

/* clone static functions, used to post a message into the main su event loop from the worker client controller thread */
namespace {
    void my_formatter(logging::record_view const& rec, logging::formatting_ostream& strm) {
        typedef boost::log::formatter formatter;
        formatter f = expr::stream << expr::format_date_time<boost::posix_time::ptime>(
            "TimeStamp", "%Y-%m-%d %H:%M:%S.%f") << " " <<
            expr::smessage ;
        f(rec, strm);
    }
    int clone_init( su_root_t* root, drachtio::DrachtioController* pController ) {
        return 0 ;
    }
    void clone_destroy( su_root_t* root, drachtio::DrachtioController* pController ) {
        return ;
    }
    void watchdogTimerHandler(su_root_magic_t *p, su_timer_t *timer, su_timer_arg_t *arg) {
        theOneAndOnlyController->processWatchdogTimer() ;
    }
            
	/* sofia logging is redirected to this function */
	static void __sofiasip_logger_func(void *logarg, char const *fmt, va_list ap) {
        
        static bool loggingSipMsg = false ;
        static bool sourceIsBlacklisted = false;
        static std::shared_ptr<drachtio::StackMsg> msg ;

        char output[MAXLOGLEN+1] ;
        vsnprintf( output, MAXLOGLEN, fmt, ap ) ;
        va_end(ap) ;

        if( loggingSipMsg ) {
            loggingSipMsg = NULL == ::strstr( fmt, MSG_SEPARATOR) ;
            msg->appendLine( output, !loggingSipMsg ) ;

            if( !loggingSipMsg ) {
                //DR_LOG(drachtio::log_debug) << "Completed logging sip message"  ;
                if (!sourceIsBlacklisted) {
                    DR_LOG( drachtio::log_info ) << msg->getFirstLine()  << msg->getSipMessage() <<  " " ;            

                    msg->isIncoming() 
                    ? theOneAndOnlyController->setLastRecvStackMessage( msg ) 
                    : theOneAndOnlyController->setLastSentStackMessage( msg ) ;
                }
                sourceIsBlacklisted = false;
            }
        }
        else if( ::strstr( output, "recv ") == output || ::strstr( output, "send ") == output ) {
            drachtio::Blacklist* pBlacklist;
            loggingSipMsg = true ;
            //DR_LOG(drachtio::log_debug) << "started logging sip message: " << output  ;

            if ((pBlacklist = theOneAndOnlyController->getBlacklist())) {
                std::string header(output);
                std::regex re("\\[(.*)\\]");
                std::smatch mr;
                if (std::regex_search(header, mr, re) && mr.size() > 1) {
                    std::string host = mr[1] ;
                    if (pBlacklist->isBlackListed(host.c_str())) {
                        sourceIsBlacklisted = true;
                        DR_LOG(drachtio::log_debug) << "discarding message from blacklisted host " << host  ;
                    }
                }
            }

            char* szStartSeparator = strstr( output, "   " MSG_SEPARATOR ) ;
            if( NULL != szStartSeparator ) *szStartSeparator = '\0' ;

            msg = std::make_shared<drachtio::StackMsg>( output ) ;
        }
        else {
            int len = strlen(output) ;
            output[len-1] = '\0' ;
            DR_LOG(drachtio::log_info) << output ;
        }
    } ;

    int legCallback( nta_leg_magic_t* controller,
                        nta_leg_t* leg,
                        nta_incoming_t* irq,
                        sip_t const *sip) {
        
        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", sip->sip_request->rq_method_name}})
        return controller->processRequestInsideDialog( leg, irq, sip ) ;
    }
    int stateless_callback(nta_agent_magic_t *controller,
                    nta_agent_t *agent,
                    msg_t *msg,
                    sip_t *sip) {
        if( sip && sip->sip_request ) STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_REQUESTS_IN, {{"method", sip->sip_request->rq_method_name}})
        return controller->processMessageStatelessly( msg, sip ) ;
    }

    static std::unordered_map<unsigned int, std::string> responseReasons({
        {100, "Trying"},
        {180, "Ringing"},
        {181, "Call is Being Forwarded"},
        {182, "Queued"},
        {183, "Session in Progress"},
        {199, "Early Dialog Terminated"},
        {200, "OK"},
        {202, "Accepted"}, 
        {204, "No Notification"}, 
        {300, "Multiple Choices"}, 
        {301, "Moved Permanently"}, 
        {302, "Moved Temporarily"}, 
        {305, "Use Proxy"}, 
        {380, "Alternative Service"}, 
        {400, "Bad Request"}, 
        {401, "Unauthorized"}, 
        {402, "Payment Required"}, 
        {403, "Forbidden"}, 
        {404, "Not Found"}, 
        {405, "Method Not Allowed"}, 
        {406, "Not Acceptable"}, 
        {407, "Proxy Authentication Required"}, 
        {408, "Request Timeout"}, 
        {409, "Conflict"}, 
        {410, "Gone"}, 
        {411, "Length Required"}, 
        {412, "Conditional Request Failed"}, 
        {413, "Request Entity Too Large"}, 
        {414, "Request-URI Too Long"}, 
        {415, "Unsupported Media Type"}, 
        {416, "Unsupported URI Scheme"}, 
        {417, "Unknown Resource-Priority"}, 
        {420, "Bad Extension"}, 
        {421, "Extension Required"}, 
        {422, "Session Interval Too Small"}, 
        {423, "Interval Too Brief"}, 
        {424, "Bad Location Information"}, 
        {428, "Use Identity Header"}, 
        {429, "Provide Referrer Identity"}, 
        {430, "Flow Failed"}, 
        {433, "Anonymity Disallowed"}, 
        {436, "Bad Identity-Info"}, 
        {437, "Unsupported Certificate"}, 
        {438, "Invalid Identity Header"}, 
        {439, "First Hop Lacks Outbound Support"}, 
        {470, "Consent Needed"}, 
        {480, "Temporarily Unavailable"}, 
        {481, "Call Leg/Transaction Does Not Exist"}, 
        {482, "Loop Detected"}, 
        {483, "Too Many Hops"}, 
        {484, "Address Incomplete"}, 
        {485, "Ambiguous"}, 
        {486, "Busy Here"}, 
        {487, "Request Terminated"}, 
        {488, "Not Acceptable Here"}, 
        {489, "Bad Event"}, 
        {491, "Request Pending"}, 
        {493, "Undecipherable"}, 
        {494, "Security Agreement Required"}, 
        {500, "Server Internal Error"}, 
        {501, "Not Implemented"}, 
        {502, "Bad Gateway"}, 
        {503, "Service Unavailable"}, 
        {504, "Server Timeout"}, 
        {505, "Version Not Supported"}, 
        {513, "Message Too Large"}, 
        {580, "Precondition Failure"}, 
        {600, "Busy Everywhere"}, 
        {603, "Decline"}, 
        {604, "Does Not Exist Anywhere"}, 
        {606, "Not Acceptable"}
    });

};

namespace drachtio {

    StackMsg::StackMsg( const char *szLine ) : m_firstLine( szLine ), m_meta( szLine ), m_os(""), m_bIncoming(::strstr( szLine, "recv ") == szLine) {
    }
    void StackMsg::appendLine( char *szLine, bool complete ) {
        if( complete ) {
            m_os.flush() ;
            m_sipMessage = m_os.str() ;
            if (m_sipMessage.length() > 1) m_sipMessage.resize( m_sipMessage.length() - 1) ;
            boost::replace_all(m_sipMessage, "\n", DR_CRLF);
        }
        else if( 0 == strcmp(szLine, "\n") ) {
            m_os << endl ;
        }
        else {
            int i = 0 ;
            while( ' ' == szLine[i] && '\0' != szLine[i]) i++ ;
            m_os << ( szLine + i ) ;
        }
    }
 
    DrachtioController::DrachtioController( int argc, char* argv[] ) : m_bDaemonize(false), m_bLoggingInitialized(false),
        m_configFilename(DEFAULT_CONFIG_FILENAME), m_adminTcpPort(0), m_adminTlsPort(0), m_bNoConfig(false), 
        m_current_severity_threshold(log_none), m_nSofiaLoglevel(-1), m_bIsOutbound(false), m_bConsoleLogging(false),
        m_nHomerPort(0), m_nHomerId(0), m_mtu(0), m_bAggressiveNatDetection(false), m_bMemoryDebug(false),
        m_nPrometheusPort(0), m_strPrometheusAddress("0.0.0.0"), m_tcpKeepaliveSecs(UINT16_MAX), m_bDumpMemory(false),
        m_minTlsVersion(0), m_bDisableNatDetection(false), m_pBlacklist(nullptr), m_bAlwaysSend180(false), 
        m_bGloballyReadableLogs(false), m_bTlsVerifyClientCert(false), m_bRejectRegisterWithNoRealm(false) {

        getEnv();

        // command line arguments, if provided, override env vars
        if( !parseCmdArgs( argc, argv ) ) {
            usage() ;
            exit(-1) ;
        }
        
        logging::add_common_attributes();
        m_Config = std::make_shared<DrachtioConfig>( m_configFilename.c_str(), m_bDaemonize ) ;

        if( !m_Config->isValid() ) {
            exit(-1) ;
        }
        this->installConfig() ;
    }

    DrachtioController::~DrachtioController() {
    }

    bool DrachtioController::installConfig() {
        if( m_ConfigNew ) {
            m_Config = m_ConfigNew ;
            m_ConfigNew.reset();
        }
        
        if( log_none == m_current_severity_threshold ) {
            m_current_severity_threshold = m_Config->getLoglevel() ;
        }

        if( 0 == m_requestRouter.getCountOfRoutes() ) {
          m_Config->getRequestRouter( m_requestRouter ) ;
        }
        
        return true ;
        
    }
    void DrachtioController::logConfig() {
        DR_LOG(log_notice) << "Starting drachtio version " << DRACHTIO_VERSION;
        DR_LOG(log_notice) << "Logging threshold:                     " << (int) m_current_severity_threshold  ;

        vector<string> routes ;
        m_requestRouter.getAllRoutes( routes ) ;

        BOOST_FOREACH(string &r, routes) {
            DR_LOG(log_notice) << "Route for outbound connection:         " << r;
        }
    }

    void DrachtioController::handleSigPipe( int signal ) {
        DR_LOG(log_notice) << "Received SIGPIPE; ignoring.."  ;
    }
    void DrachtioController::handleSigTerm( int signal ) {
        DR_LOG(log_notice) << "Received SIGTERM; exiting after dumping stats.."  ;
        this->printStats(m_bMemoryDebug || m_bDumpMemory) ;
        m_pDialogController->logStorageCount() ;
        m_pClientController->logStorageCount() ;
        m_pPendingRequestController->logStorageCount() ;
        m_pProxyController->logStorageCount() ;
        nta_agent_destroy(m_nta);
        exit(0);
    }

    void DrachtioController::handleSigHup( int signal ) {
        m_bDumpMemory = true;
        DR_LOG(log_notice) << "SIGHUP handled - next storage printout will include detailed logging"  ;
        if( !m_ConfigNew ) {
            DR_LOG(log_notice) << "Re-reading configuration file"  ;
            m_ConfigNew.reset( new DrachtioConfig( m_configFilename.c_str() ) ) ;
            if( !m_ConfigNew->isValid() ) {
                DR_LOG(log_error) << "Error reading configuration file; no changes will be made.  Please correct the configuration file and try to reload again"  ;
                m_ConfigNew.reset() ;
            }
            else {
                m_current_severity_threshold = m_ConfigNew->getLoglevel() ;
                logging::core::get()->set_filter(
                   expr::attr<severity_levels>("Severity") <= m_current_severity_threshold
                );
                switch (m_current_severity_threshold) {
                    case log_notice:
                        DR_LOG(log_notice) << "drachtio loglevel set to NOTICE"  ;
                        break;
                    case log_error:
                        DR_LOG(log_notice) << "drachtio loglevel set to ERROR"  ;
                        break;
                    case log_info:
                        DR_LOG(log_notice) << "drachtio loglevel set to INFO"  ;
                        break;
                    case log_debug:
                        DR_LOG(log_notice) << "drachtio loglevel set to DEBUG"  ;
                        break;
                    default:
                        break;
                }
                unsigned int sofiaLoglevel = m_ConfigNew->getSofiaLogLevel();
                su_log_set_level(NULL, sofiaLoglevel);
                DR_LOG(log_notice) << "sofia loglevel set to " <<  sofiaLoglevel;

                this->installConfig() ;
            }
        }
        else {
            DR_LOG(log_error) << "Ignoring signal; already have a new configuration file to install"  ;
        }
    }

    bool DrachtioController::parseCmdArgs( int argc, char* argv[] ) {        
        int c ;
        string port ;
        string publicAddress ;
        string localNet ;
        string contact ;
        vector<string> vecDnsNames;
        string httpMethod = "GET";
        string httpUrl ;
        string method;

        while (1)
        {
            static struct option long_options[] =
            {
                /* These options set a flag. */
                {"daemon", no_argument,       &m_bDaemonize, true},
                {"noconfig", no_argument,       &m_bNoConfig, true},
                {"reject-register-with-no-realm", no_argument, &m_bRejectRegisterWithNoRealm, true},
                
                /* These options don't set a flag.
                 We distinguish them by their indices. */
                {"file",    required_argument, 0, 'f'},
                {"help",    no_argument, 0, 'h'},
                {"user",    required_argument, 0, 'u'},
                {"port",    required_argument, 0, 'p'},
                {"contact",    required_argument, 0, 'c'},
                {"external-ip",    required_argument, 0, 'x'},
                {"local-net",    required_argument, 0, 'n'},
                {"dns-name",    required_argument, 0, 'd'},
                {"http-handler",    required_argument, 0, 'a'},
                {"http-method",    required_argument, 0, 'm'},
                {"loglevel",    required_argument, 0, 'l'},
                {"sofia-loglevel",    required_argument, 0, 's'},
                {"stdout",    no_argument, 0, 'b'},
                {"homer",    required_argument, 0, 'y'},
                {"homer-id",    required_argument, 0, 'z'},
                {"key-file", required_argument, 0, 'A'},
                {"cert-file", required_argument, 0, 'B'},
                {"chain-file", required_argument, 0, 'C'},
                {"mtu", required_argument, 0, 'D'},
                {"address", required_argument, 0, 'E'},
                {"secret", required_argument, 0, 'F'},
                {"dh-param", required_argument, 0, 'G'},
                {"tls-port",    required_argument, 0, 'H'},
                {"aggressive-nat-detection", no_argument, 0, 'I'},
                {"prometheus-scrape-port", required_argument, 0, 'J'},
                {"memory-debug", no_argument, 0, 'K'},
                {"tcp-keepalive-interval", required_argument, 0, 'L'},
                {"min-tls-version", required_argument, 0, 'M'},
                {"disable-nat-detection", no_argument, 0, 'N'},
                {"blacklist-redis-address", required_argument, 0, 'O'},
                {"blacklist-redis-port", required_argument, 0, 'P'},
                {"blacklist-redis-key", required_argument, 0, 'Q'},
                {"blacklist-refresh-secs", required_argument, 0, 'R'},
                {"always-send-180", no_argument, 0, 'S'},
                {"user-agent-options-auto-respond", no_argument, 0, 'T'},
                {"globally-readable-logs", no_argument, 0, 'U'},
                {"blacklist-redis-sentinels", required_argument, 0, 'V'},
                {"blacklist-redis-master", required_argument, 0, 'W'},
                {"blacklist-redis-password", required_argument, 0, 'X'},
                {"version",    no_argument, 0, 'v'},
                {0, 0, 0, 0}
            };
            /* getopt_long stores the option index here. */
            int option_index = 0;
            
            c = getopt_long (argc, argv, "a:c:f:hi:l:m:p:n:u:vx:y:z:A:B:C:D:E:F:G:I",
                             long_options, &option_index);
            
            /* Detect the end of the options. */
            if (c == -1)
                break;
            
            switch (c)
            {
                case 0:
                    /* If this option set a flag, do nothing else now. */
                    if (long_options[option_index].flag != 0)
                        break;
                    cout << "option " << long_options[option_index].name ;
                    if (optarg)
                        cout << " with arg " << optarg;
                    cout << endl ;
                    break;
                case 'A':
                    m_tlsKeyFile = optarg;
                    break;
                case 'B':
                    m_tlsCertFile = optarg;
                    break;
                case 'C':
                    m_tlsChainFile = optarg;
                    break;
                case 'D':
                    m_mtu = ::atoi(optarg);
                    break;
                case 'E':
                    m_adminAddress = optarg;
                    break;
                case 'F':
                    m_secret = optarg;
                    break;
                case 'G':
                    m_dhParam = optarg;
                    break;
                case 'a':
                    httpUrl = optarg ;
                    break;

                case 'b':
                    m_bConsoleLogging = true ;
                    break;

                case 'd':
                    vecDnsNames.push_back(optarg);
                    break;

                case 'f':
                    m_configFilename = optarg ;
                    break;

                case 'h':
                    usage() ;
                    exit(0);

                case 'l':
                    if( 0 == strcmp(optarg, "notice") ) m_current_severity_threshold = log_notice ;
                    else if( 0 == strcmp(optarg,"error") ) m_current_severity_threshold = log_error ;
                    else if( 0 == strcmp(optarg,"warning") ) m_current_severity_threshold = log_warning ;
                    else if( 0 == strcmp(optarg,"info") ) m_current_severity_threshold = log_info ;
                    else if( 0 == strcmp(optarg,"debug") ) m_current_severity_threshold = log_debug ;
                    else {
                        cerr << "Invalid loglevel '" << optarg << "': valid choices are notice, error, warning, info, debug" << endl ; 
                        return false ;
                    }

                    break;

                case 'm':
                    method = optarg ;
                    if( boost::iequals(method, "POST")) {
                      httpMethod = "POST";
                    }
                    break;

                case 's':
                    m_nSofiaLoglevel = atoi( optarg ) ;
                    if( m_nSofiaLoglevel < 0 || m_nSofiaLoglevel > 9 ) {
                        cerr << "Invalid sofia-loglevel '" << optarg << "': valid choices 0-9 inclusive" << endl ; 
                        return false ;                        
                    }

                    break;

                case 'u':
                    m_user = optarg ;
                    break;

                case 'c':
                    if( !contact.empty() ) {
                        m_vecTransports.push_back( std::make_shared<SipTransport>(contact, localNet, publicAddress )) ;
                        contact.clear() ;
                        publicAddress.clear() ;
                        localNet.clear() ;
                    }
                    contact = optarg ;
                    break;
                                    
                case 'x': 
                    if( contact.empty() ) {
                        cerr << "'public-ip' argument must follow a 'contact'" << endl ;
                        return false ;
                    }
                    if (!publicAddress.empty() ) {
                        cerr << "multiple 'public-ip' arguments provided for a single contact" << endl ;
                        return false ;
                    }
                    publicAddress = optarg ;
                    break ;

                case 'n':
                    if( contact.empty() ) {
                        cerr << "'local-net' argument must follow a 'contact'" << endl ;
                        return false ;
                    }
                    if (!localNet.empty() ) {
                        cerr << "multiple 'local-net' arguments provided for a single contact" << endl ;
                        return false ;
                    }
                    localNet = optarg ;
                    break ;

                case 'p':
                    port = optarg ;
                    m_adminTcpPort = ::atoi( port.c_str() ) ;
                    break;

                case 'H':
                    port = optarg ;
                    m_adminTlsPort = ::atoi( port.c_str() ) ;
                    break;

                case 'y':
                    {
                        m_nHomerPort = DEFAULT_HOMER_PORT;
                        vector<string>strs;
                        boost::split(strs, optarg, boost::is_any_of(":"));
                        if(strs.size() > 2) {
                            cerr << "invalid homer address: " << optarg << endl ;
                            return false ;
                        }
                        m_strHomerAddress = strs[0];
                        if( 2 == strs.size()) m_nHomerPort = boost::lexical_cast<uint32_t>(strs[1]);
                    }
                    break;

                case 'z':
                    try {
                        m_nHomerId = boost::lexical_cast<uint32_t>(optarg);
                    } catch(boost::bad_lexical_cast& err) {
                        cerr << "--homer-id must be a positive 32-bit integer" << endl;
                        return false;
                    }
                    if(0 == m_nHomerId) {
                        cerr << "--homer-id must be a positive 32-bit integer" << endl;
                        return false;                        
                    }
                    break;
                case 'I':
                    m_bAggressiveNatDetection = true;
                    break;

                case 'J':
                    {
                        vector<string>strs;
                        boost::split(strs, optarg, boost::is_any_of(":"));
                        if(strs.size() == 2) {
                            m_strPrometheusAddress = strs[0];
                            m_nPrometheusPort = boost::lexical_cast<uint32_t>(strs[1]);
                        }
                        else {
                            m_nPrometheusPort = boost::lexical_cast<uint32_t>(optarg); 
                        }
                    }
                    break;

                case 'K':
                    m_bMemoryDebug = true;
                    break;

                case 'L':
                    m_tcpKeepaliveSecs = ::atoi(optarg);
                    break;

                case 'M':
                    m_minTlsVersion = ::atof(optarg);
                    break;

                case 'N':
                    m_bDisableNatDetection = true;
                    break;
                case 'O':
                    m_redisAddress = optarg;
                    break;
                case 'P':
                    m_redisPort = ::atoi(optarg);
                    break;
                case 'Q':
                    m_redisKey = optarg;
                    break;
                case 'R':
                    m_redisRefreshSecs = ::atoi(optarg);
                    break;
                case 'S':
                    m_bAlwaysSend180 = true;
                    break;
                case 'T':
                    m_strUserAgentAutoAnswerOptions = optarg;
                    break;
                case 'U':
                    m_bGloballyReadableLogs = true;
                    break;
                case 'V':
                    m_redisSentinels = optarg;
                    break;
                case 'W':
                    m_redisMaster = optarg;
                    break;
                case 'X':
                    m_redisPassword= optarg;
                    break;
                case 'v':
                    cout << DRACHTIO_VERSION << endl ;
                    exit(0) ;
                                                            
                case '?':
                    /* getopt_long already printed an error message. */
                    break;
                    
                default:
                    abort ();
            }
        }

        if(!m_strHomerAddress.empty() && 0 == m_nHomerId) {
            cerr << "--homer-id is required to specify an agent id when using --homer" << endl;
            return false;
        }

        if( !contact.empty() ) {
          std::shared_ptr<SipTransport> p = std::make_shared<SipTransport>(contact, localNet, publicAddress);
          for( std::vector<string>::const_iterator it = vecDnsNames.begin(); it != vecDnsNames.end(); ++it) {
            p->addDnsName(*it);
          }
          m_vecTransports.push_back(p) ;
        }

        if (!m_redisAddress.empty()) {
            if (0 == m_redisPort) m_redisPort = 6379;
            if (0 == m_redisRefreshSecs) m_redisRefreshSecs = 300;
        }

        if( !httpUrl.empty() ) {
          m_requestRouter.addRoute("*", httpMethod, httpUrl, true);
        }

        /* Print any remaining command line arguments (not options). */
        if (optind < argc)
        {
            cout << "non-option ARGV-elements: ";
            while (optind < argc)
                cout << argv[optind++] ;
        }
                
        return true ;
    }

    void DrachtioController::usage() {
        cerr << endl ;
        cerr << "Usage: drachtio [OPTIONS]" << endl ;
        cerr << endl << "Start drachtio sip engine" << endl << endl ;
        cerr << "Options:" << endl << endl ;
        cerr << "    --address                          Bind to the specified address for application connections (default: 0.0.0.0)" << endl ;
        cerr << "    --aggressive-nat-detection         take presence of 'nat=yes' in Record-Route or Contact hdr as an indicator a remote server is behind a NAT" << endl ;
        cerr << "    --blacklist-redis-address          address of redis server that contains a set with blacklisted IPs" << endl;
        cerr << "    --blacklist-redis-port             port for redis server containing blacklisted IPs" << endl;
        cerr << "    --blacklist-redis-key              key for a redis set that contains blacklisted IPs" << endl;
        cerr << "    --blacklist-refresh-secs           how often to check for new blacklisted IPs" << endl;
        cerr << "    --blacklist-redis-sentinels        comma-separated list of redis sentinels in ip:port format" << endl;
        cerr << "    --blacklist-redis-password         password for redis server, if required" << endl;
        cerr << "    --daemon                           Run the process as a daemon background process" << endl ;
        cerr << "    --cert-file                        TLS certificate file" << endl ;
        cerr << "    --chain-file                       TLS certificate chain file" << endl ;
        cerr << "-c, --contact                          Sip contact url to bind to (see /etc/drachtio.conf.xml for examples)" << endl ;
        cerr << "    --dh-param                         file containing Diffie-Helman parameters, required when using encrypted TLS admin connections" << endl ;
        cerr << "    --dns-name                         specifies a DNS name that resolves to the local host, if any" << endl ;
        cerr << "-f, --file                             Path to configuration file (default /etc/drachtio.conf.xml)" << endl ;
        cerr << "    --homer                            ip:port of homer/sipcapture agent" << endl ;
        cerr << "    --homer-id                         homer agent id to use in HEP messages to identify this server" << endl ;
        cerr << "    --http-handler                     http(s) URL to optionally send routing request to for new incoming sip request" << endl ;
        cerr << "    --http-method                      method to use with http-handler: GET (default) or POST" << endl ;
        cerr << "    --key-file                         TLS key file" << endl ;
        cerr << "-l  --loglevel                         Log level (choices: notice, error, warning, info, debug)" << endl ;
        cerr << "    --local-net                        CIDR for local subnet (e.g. \"10.132.0.0/20\")" << endl ;
        cerr << "    --memory-debug                     enable verbose debugging of memory allocations (do not turn on in production)" << endl ;
        cerr << "    --mtu                              max packet size for UDP (default: system-defined mtu)" << endl ;
        cerr << "-p, --port                             TCP port to listen on for application connections (default 9022)" << endl ;
        cerr << "    --prometheus-scrape-port           The port (or host:port) to listen on for Prometheus.io metrics scrapes" << endl ;
        cerr << "    --reject-register-with-no-realm    reject with a 403 any REGISTER that has an IP address in the sip uri host" << endl ;
        cerr << "    --secret                           The shared secret to use for authenticating application connections" << endl ;
        cerr << "    --sofia-loglevel                   Log level of internal sip stack (choices: 0-9)" << endl ;
        cerr << "    --external-ip                      External IP address to use in SIP messaging" << endl ;
        cerr << "    --stdout                           Log to standard output as well as any configured log destinations" << endl ;
        cerr << "    --tcp-keepalive-interval           tcp keepalive in seconds (0=no keepalive)" << endl ;
        cerr << "    --min-tls-version                  minimum allowed TLS version for connecting clients (default: 1.0)" << endl ;
        cerr << "    --user-agent-options-auto-respond  If we see this User-Agent header value in an OPTIONS request, automatically send 200 OK" << endl ;
        cerr << "-v  --version                          Print version and exit" << endl ;
    }
    void DrachtioController::getEnv(void) {
        const char* p = std::getenv("DRACHTIO_ADMIN_ADDRESS");
        if (p) m_adminAddress = p;
        p = std::getenv("DRACHTIO_ADMIN_TCP_PORT");
        if (p && ::atoi(p) > 0) m_adminTcpPort = ::atoi(p);
        p = std::getenv("DRACHTIO_ADMIN_TLS_PORT");
        if (p && ::atoi(p) > 0) m_adminTlsPort = ::atoi(p);
        p = std::getenv("DRACHTIO_AGRESSIVE_NAT_DETECTION");
        if (p && ::atoi(p) == 1) m_bAggressiveNatDetection = true;
        p = std::getenv("DRACHTIO_MEMORY_DEBUG");
        if (p && ::atoi(p) == 1) m_bMemoryDebug = true;
        p = std::getenv("DRACHTIO_TLS_CERT_FILE");
        if (p) m_tlsCertFile = p;
        p = std::getenv("DRACHTIO_TLS_CHAIN_FILE");
        if (p) m_tlsChainFile = p;
        p = std::getenv("DRACHTIO_TLS_KEY_FILE");
        if (p) m_tlsKeyFile = p;
        p = std::getenv("DRACHTIO_TLS_DH_PARAM_FILE");
        if (p) m_dhParam = p;
        p = std::getenv("DRACHTIO_TLS_VERIFY_CLIENT_CERT");
        if (p && ::atoi(p) == 1) m_bTlsVerifyClientCert = true;
        p = std::getenv("DRACHTIO_CONFIG_FILE_PATH");
        if (p) m_configFilename = p;
        p = std::getenv("DRACHTIO_HOMER_ADDRESS");
        if (p) m_strHomerAddress = p;
        p = std::getenv("DRACHTIO_HOMER_PORT");
        if (p && ::atoi(p) > 0) m_nHomerPort = ::atoi(p);
        p = std::getenv("DRACHTIO_HOMER_ID");
        if (p && ::atoi(p) > 0) m_nHomerId = ::atoi(p);
        p = std::getenv("DRACHTIO_HTTP_HANDLER_URI");
        if (p) {
            m_requestRouter.clearRoutes();
            m_requestRouter.addRoute("*", "GET", p, true);
        }
        p = std::getenv("DRACHTIO_LOGLEVEL");
        if (p) {
            if( 0 == strcmp(p, "notice") ) m_current_severity_threshold = log_notice ;
            else if( 0 == strcmp(p,"error") ) m_current_severity_threshold = log_error ;
            else if( 0 == strcmp(p,"warning") ) m_current_severity_threshold = log_warning ;
            else if( 0 == strcmp(p,"info") ) m_current_severity_threshold = log_info ;
            else if( 0 == strcmp(p,"debug") ) m_current_severity_threshold = log_debug ;
        }
        p = std::getenv("DRACHTIO_SOFIA_LOGLEVEL");
        if (p && ::atoi(p) > 0 && ::atoi(p) <= 9) m_nSofiaLoglevel = ::atoi(p);
        p = std::getenv("DRACHTIO_UDP_MTU");
        if (p && ::atoi(p) > 0) m_mtu = ::atoi(p);
        p = std::getenv("DRACHTIO_TCP_KEEPALIVE_INTERVAL");
        if (p && ::atoi(p) >= 0) m_tcpKeepaliveSecs = ::atoi(p);
        p = std::getenv("DRACHTIO_SECRET");
        if (p) m_secret = p;
        p = std::getenv("DRACHTIO_CONSOLE_LOGGING");
        if (p && ::atoi(p) == 1) m_bConsoleLogging = true;
        p = std::getenv("DRACHTIO_PROMETHEUS_SCRAPE_PORT");
        if (p){
            vector<string>strs;
            boost::split(strs, p, boost::is_any_of(":"));
            if(strs.size() == 2) {
                m_strPrometheusAddress = strs[0];
                m_nPrometheusPort = boost::lexical_cast<uint32_t>(strs[1]);
            }
            else {
                m_nPrometheusPort = boost::lexical_cast<uint32_t>(p); 
            }            
        }
        p = std::getenv("DRACHTIO_MIN_TLS_VERSION");
        if (p) {
            float min = ::atof(p);
            if (min >= 1.0 && min <= 1.3) m_minTlsVersion = min;
        }
        p = std::getenv("DRACHTIO_BLACKLIST_REDIS_ADDRESS");
        if (p) {
            m_redisAddress = p;
        }
        p = std::getenv("DRACHTIO_BLACKLIST_REDIS_SENTINELS");
        if (p) {
            m_redisSentinels = p;
        }
        p = std::getenv("DRACHTIO_BLACKLIST_REDIS_MASTER");
        if (p) {
            m_redisMaster = p;
        }
        p = std::getenv("DRACHTIO_BLACKLIST_REDIS_PASSWORD");
        if (p) {
            m_redisPassword = p;
        }
        p = std::getenv("DRACHTIO_BLACKLIST_REDIS_PORT");
        if (p) {
            m_redisPort = boost::lexical_cast<unsigned int>(p); ;
        }
        p = std::getenv("DRACHTIO_BLACKLIST_REDIS_KEY");
        if (p) {
            m_redisKey = p;
        }
        p = std::getenv("DRACHTIO_BLACKLIST_REDIS_REFRESH_SECS");
        if (p) {
            m_redisRefreshSecs = boost::lexical_cast<unsigned int>(p); ;
        }
        p = std::getenv("DRACHTIO_USER_AGENT_OPTIONS_AUTO_RESPOND");
        if (p) {
            m_strUserAgentAutoAnswerOptions = p;
        }
        p = std::getenv("DRACHTIO_REJECT_REGISTER_WITH_NO_REALM");
        if (p && ::atoi(p) == 1) m_bRejectRegisterWithNoRealm = true;
    }

    void DrachtioController::daemonize() {
        /* Our process ID and Session ID */
        pid_t pid, sid;
        
        /* Fork off the parent process */
        pid = fork();
        if (pid < 0) {
            exit(EXIT_FAILURE);
        }
        /* If we got a good PID, then
         we can exit the parent process. */
        if (pid > 0) {
            cout << pid  ;
            exit(EXIT_SUCCESS);
        }
        if( !m_user.empty() ) {
            struct passwd *pw = getpwnam( m_user.c_str() );
            
            if( pw ) {
                int rc = setuid( pw->pw_uid ) ;
                if( 0 != rc ) {
                    cerr << "Error setting userid to user " << m_user << ": " << errno  ;
                }
            }
        }
        /* Change the file mode mask */
        umask(0);
            
        /* Create a new SID for the child process */
        sid = setsid();
        if (sid < 0) {
            /* Log the failure */
            exit(EXIT_FAILURE);
        }
        
        /* Change the current working directory */
        if ((chdir("/tmp")) < 0) {
            /* Log the failure */
            exit(EXIT_FAILURE);
        }
        
        /* Close out the standard file descriptors */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
    }
    src::severity_logger_mt< severity_levels >* DrachtioController::createLogger()  {	
         if( !m_bLoggingInitialized ) this->initializeLogging() ;
        return new src::severity_logger_mt< severity_levels >(keywords::severity = log_info);
   }

    void DrachtioController::deinitializeLogging() {
        if( m_sinkSysLog ) {
           logging::core::get()->remove_sink( m_sinkSysLog ) ;
            m_sinkSysLog.reset() ;
        }
        if( m_sinkTextFile ) {
           logging::core::get()->remove_sink( m_sinkTextFile ) ;
            m_sinkTextFile.reset() ;            
        }
        if( m_sinkConsole ) {
           logging::core::get()->remove_sink( m_sinkConsole ) ;
            m_sinkConsole.reset() ;            
        }
    }
    void DrachtioController::initializeLogging() {
        try {

            if( m_bNoConfig || m_Config->getConsoleLogTarget() || m_bConsoleLogging ) {

                m_sinkConsole.reset(
                    new sinks::synchronous_sink< sinks::text_ostream_backend >()
                );        
                m_sinkConsole->locked_backend()->add_stream( boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));

                // flush
                m_sinkConsole->locked_backend()->auto_flush(true);

                m_sinkConsole->set_formatter( &my_formatter ) ;
                          
                logging::core::get()->add_sink(m_sinkConsole);

                 logging::core::get()->set_filter(
                   expr::attr<severity_levels>("Severity") <= m_current_severity_threshold
                ) ;
            }
            if( !m_bNoConfig ) {


                // Create a syslog sink
                sinks::syslog::facility facility  ;
                string syslogAddress ;
                unsigned short syslogPort;
                
                // initalize syslog sink, if configuredd
                if( m_Config->getSyslogTarget( syslogAddress, syslogPort ) ) {
                    m_Config->getSyslogFacility( facility ) ;

                    m_sinkSysLog.reset(
                        new sinks::synchronous_sink< sinks::syslog_backend >(
                             keywords::use_impl = sinks::syslog::udp_socket_based
                            , keywords::facility = facility
                        )
                    );

                    // We'll have to map our custom levels to the syslog levels
                    sinks::syslog::custom_severity_mapping< severity_levels > mapping("Severity");
                    mapping[log_debug] = sinks::syslog::debug;
                    mapping[log_notice] = sinks::syslog::notice;
                    mapping[log_info] = sinks::syslog::info;
                    mapping[log_warning] = sinks::syslog::warning;
                    mapping[log_error] = sinks::syslog::critical;

                    m_sinkSysLog->locked_backend()->set_severity_mapper(mapping);

                    // Set the remote address to sent syslog messages to
                    m_sinkSysLog->locked_backend()->set_target_address( syslogAddress.c_str(), syslogPort );

                    logging::core::get()->add_global_attribute("RecordID", attrs::counter< unsigned int >());

                    // Add the sink to the core
                    logging::core::get()->add_sink(m_sinkSysLog);

                }

                //initialize text file sink, of configured
                string name, archiveDirectory ;
                unsigned int rotationSize, maxSize, minSize, maxFiles ;
                bool autoFlush ;
                if( m_Config->getFileLogTarget( name, archiveDirectory, rotationSize, autoFlush, maxSize, minSize, maxFiles ) ) {
                   if (!m_bGloballyReadableLogs) {
                   boost::filesystem::path p(name);
                    boost::filesystem::path dir = p.parent_path();
                    boost::filesystem::create_directory(dir);
                    std::ofstream output(name, ofstream::out | ofstream::app);
                    output.close();
                    boost::filesystem::permissions(name,
                        boost::filesystem::perms::owner_read |
                        boost::filesystem::perms::owner_write |
                        boost::filesystem::perms::group_read |
                        boost::filesystem::perms::group_write
                    );
                   }

                    m_sinkTextFile.reset(
                        new sinks::synchronous_sink< sinks::text_file_backend >(
                            keywords::file_name = name,                                          
                            keywords::rotation_size = rotationSize * 1000000,
                            keywords::auto_flush = autoFlush,
                            keywords::time_based_rotation = sinks::file::rotation_at_time_point(0, 0, 0),
                            keywords::open_mode = (std::ios::out | std::ios::app),
                            keywords::format = 
                            (
                                expr::stream
                                    << expr::attr< unsigned int >("RecordID")
                                    << ": "
                                    << expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S")
                                    << "> " << expr::smessage
                            )
                        )
                    );        

                    m_sinkTextFile->set_formatter( &my_formatter ) ;

                    m_sinkTextFile->locked_backend()->set_file_collector(sinks::file::make_collector(
                        keywords::target = archiveDirectory,                      
                        keywords::max_size = maxSize * 1000000,          
                        keywords::min_free_space = minSize * 1000000,
                        keywords::max_files = maxFiles
                    ));
                               
                    logging::core::get()->add_sink(m_sinkTextFile);
                }
                logging::core::get()->set_filter(
                   expr::attr<severity_levels>("Severity") <= m_current_severity_threshold
                ) ;
            }
            
            m_bLoggingInitialized = true ;

        }
        catch (std::exception& e) {
            std::cerr << "FAILURE creating logger: " << e.what() << std::endl;
            throw e;
        }	
    }

    void DrachtioController::run() {
        
      if( m_bDaemonize ) {
          daemonize() ;
      }

      /* now we can initialize logging */
      m_logger.reset( this->createLogger() );
      this->logConfig() ;

        DR_LOG(log_debug) << "DrachtioController::run: Main thread id: " << std::this_thread::get_id() ;
        if (m_bMemoryDebug) DR_LOG(log_notice) << "DrachtioController::run: memory debugging is ON...only use for non-production configurations" ;

       /* open admin connection */
        string adminAddress ;
        m_Config->getAdminAddress(adminAddress);
        unsigned int adminTcpPort = m_Config->getAdminTcpPort() ;
        unsigned int adminTlsPort = m_Config->getAdminTlsPort() ;
        if (!m_adminAddress.empty()) adminAddress = m_adminAddress;
        if( 0 != m_adminTcpPort ) adminTcpPort = m_adminTcpPort ;
        if( 0 != m_adminTlsPort ) adminTlsPort = m_adminTlsPort ;

        if( 0 == m_vecTransports.size() ) {
            m_Config->getTransports( m_vecTransports ) ; 
        }
        if( 0 == m_vecTransports.size() ) {

            DR_LOG(log_notice) << "DrachtioController::run: no sip contacts provided, will listen on 5060 for udp and tcp " ;
            m_vecTransports.push_back( std::make_shared<SipTransport>("sip:*;transport=udp,tcp"));            
        }

        string outboundProxy ;
        if( m_Config->getSipOutboundProxy(outboundProxy) ) {
            DR_LOG(log_notice) << "DrachtioController::run: outbound proxy " << outboundProxy ;
        }

        if (!m_bAggressiveNatDetection) {
            m_bAggressiveNatDetection = m_Config->isAggressiveNatEnabled();
        }

        if (!m_bRejectRegisterWithNoRealm) {
            m_bRejectRegisterWithNoRealm = m_Config->rejectRegisterWithNoRealm();
        }
        if (m_bRejectRegisterWithNoRealm) {
            DR_LOG(log_notice) << "DrachtioController::run: rejecting REGISTER requests with no realm in the SIP URI" ;
        }

        // tls files
        string tlsKeyFile, tlsCertFile, tlsChainFile, dhParam ;
        int tlsVersionTagValue = TPTLS_VERSION_TLSv1 | TPTLS_VERSION_TLSv1_1 | TPTLS_VERSION_TLSv1_2;
        bool hasTlsFiles = m_Config->getTlsFiles( tlsKeyFile, tlsCertFile, tlsChainFile, dhParam ) ;
        if (!m_tlsKeyFile.empty()) tlsKeyFile = m_tlsKeyFile;
        if (!m_tlsCertFile.empty()) tlsCertFile = m_tlsCertFile;
        if (!m_tlsChainFile.empty()) tlsChainFile = m_tlsChainFile;
        if (!m_dhParam.empty()) dhParam = m_dhParam;
        if (!hasTlsFiles && !tlsKeyFile.empty() && !tlsCertFile.empty()) hasTlsFiles = true;
        if (m_minTlsVersion >= 1.0 || (m_Config->getMinTlsVersion(m_minTlsVersion) && m_minTlsVersion > 1.0)) {
            if (m_minTlsVersion >= 1.2) {
                DR_LOG(log_notice) << "DrachtioController::run: minTls version 1.2";
                tlsVersionTagValue = TPTLS_VERSION_TLSv1_2;
            }
            else if (m_minTlsVersion >= 1.1) {
                DR_LOG(log_notice) << "DrachtioController::run: minTls version 1.1" ;
                tlsVersionTagValue = TPTLS_VERSION_TLSv1_1 | TPTLS_VERSION_TLSv1_2;
            }
        }

        if (hasTlsFiles) {
            DR_LOG(log_notice) << "DrachtioController::run tls key file:         " << tlsKeyFile;
            DR_LOG(log_notice) << "DrachtioController::run tls certificate file: " << tlsCertFile;
            if (!tlsChainFile.empty()) DR_LOG(log_notice) << "DrachtioController::run tls chain file:       " << tlsChainFile;
        }

        if (adminTlsPort) {
            if ((tlsChainFile.empty() && tlsCertFile.empty()) || tlsKeyFile.empty() || dhParam.empty()) {
                DR_LOG(log_notice) << "DrachtioController::run tls was requested on admin connection but either chain file/cert file, private key, or dhParams were not provided";
                throw runtime_error("missing tls settings");
            }
        }

        if (m_adminTcpPort && !m_adminTlsPort) {
            DR_LOG(log_notice) << "DrachtioController::run listening for applications on tcp port " << adminTcpPort << " only";
            m_pClientController.reset(new ClientController(this, adminAddress, adminTcpPort));
        }
        else if (!m_adminTcpPort && m_adminTlsPort) {
            DR_LOG(log_notice) << "DrachtioController::run listening for applications on tls port " << adminTlsPort << " only";
            m_pClientController.reset(new ClientController(this, adminAddress, adminTlsPort, tlsChainFile, tlsCertFile, tlsKeyFile, dhParam));
        }
        else {
             DR_LOG(log_notice) << "DrachtioController::run listening for applications on tcp port " << adminTcpPort << " and tls port " << adminTlsPort ;
           m_pClientController.reset(new ClientController(this, adminAddress, adminTcpPort, adminTlsPort, tlsChainFile, tlsCertFile, tlsKeyFile, dhParam));
        }
        m_pClientController->start();
        
        // mtu
        if (!m_mtu) m_mtu = m_Config->getMtu();
        if (m_mtu > 0 && m_mtu < 1000) {
            DR_LOG(log_notice) << "DrachtioController::run invalid mtu size provided, must be > 1000: " << m_mtu;
            throw runtime_error("invalid mtu setting");
        }
        else if (m_mtu > 0) {
            DR_LOG(log_notice) << "DrachtioController::run mtu size for udp packets: " << m_mtu;            
        }
        
        string captureServer;
        string captureString;
        uint32_t captureId ;
        unsigned int hepVersion;
        unsigned int capturePort ;
        if (!m_strHomerAddress.empty()) {
            captureString = "udp:" + m_strHomerAddress + ":" + boost::lexical_cast<std::string>(m_nHomerPort) + 
                ";hep=3;capture_id=" + boost::lexical_cast<std::string>(m_nHomerId);
            DR_LOG(log_notice) << "DrachtioController::run - sipcapture/Homer enabled: " << captureString;            
        }
        else if (m_Config->getCaptureServer(captureServer, capturePort, captureId, hepVersion)) {
            if (hepVersion < 1 || hepVersion > 3) {
                DR_LOG(log_error) << "DrachtioController::run invalid hep-version " << hepVersion <<
                    "; must be between 1 and 3 inclusive";
            }
            else {
                captureString = "udp:" + captureServer + ":" + boost::lexical_cast<std::string>(capturePort) + 
                    ";hep=" + boost::lexical_cast<std::string>(hepVersion) +
                    ";capture_id=" + boost::lexical_cast<std::string>(captureId);
                DR_LOG(log_notice) << "DrachtioController::run - sipcapture/Homer enabled: " << captureString;
            }

        }

        if (m_strUserAgentAutoAnswerOptions.empty()) {
             m_Config->getAutoAnswerOptionsUserAgent(m_strUserAgentAutoAnswerOptions);
        }

        /* mostly useful for kubernetes deployments, where it is verboten to mess with iptables */
        if (m_redisAddress.empty()) {
            string redisAddress, redisSentinels, redisMaster, redisPassword, redisKey;
            unsigned int redisPort, redisRefreshSecs;
            DR_LOG(log_notice) << "DrachtioController::run - blacklist checking config";

            if (m_Config->getBlacklistServer(redisAddress, redisSentinels, redisMaster, redisPassword, redisPort, redisKey, redisRefreshSecs)) {
                m_redisAddress = redisAddress;
                m_redisSentinels = redisSentinels;
                m_redisMaster = redisMaster;
                m_redisPassword = redisPassword;
                m_redisPort = redisPort;
                m_redisKey = redisKey;
                m_redisRefreshSecs = redisRefreshSecs;
            }
        }
        if (m_redisAddress.length() && m_redisKey.length()) {
            DR_LOG(log_notice) << "DrachtioController::run - blacklist is in redis " << m_redisAddress << ":" << m_redisPort 
                << ", key is " << m_redisKey;
            m_pBlacklist = new Blacklist(m_redisAddress, m_redisPort, m_redisPassword, m_redisKey, m_redisRefreshSecs);
            m_pBlacklist->start();
        }
        else if (m_redisSentinels.length() && m_redisMaster.length() &&  m_redisKey.length()) {
            DR_LOG(log_notice) << "DrachtioController::run - blacklist is in redis, using sentinels " << m_redisSentinels 
                << ", key is " << m_redisKey;
            m_pBlacklist = new Blacklist(m_redisSentinels, m_redisMaster, m_redisPassword, m_redisKey, m_redisRefreshSecs);
            m_pBlacklist->start();
        }
        else {
            DR_LOG(log_notice) << "DrachtioController::run - blacklist is disabled";
        }

        // monitoring
        if (m_nPrometheusPort == 0) m_Config->getPrometheusAddress( m_strPrometheusAddress, m_nPrometheusPort ) ;
        if (m_nPrometheusPort != 0) {
            string hostport = m_strPrometheusAddress + ":" + boost::lexical_cast<std::string>(m_nPrometheusPort);
            DR_LOG(log_notice) << "responding to Prometheus on " << hostport;
            m_statsCollector.enablePrometheus(hostport.c_str());
        }
        else {
            DR_LOG(log_notice) << "Prometheus support disabled";
        }
        initStats();

        // tcp keepalive
        if (UINT16_MAX == m_tcpKeepaliveSecs) {
            m_tcpKeepaliveSecs = m_Config->getTcpKeepalive();
        }
        if (0 == m_tcpKeepaliveSecs) {
            DR_LOG(log_notice) << "tcp keep alives have been disabled ";
        }
        else {
            DR_LOG(log_notice) << "tcp keep alives will be sent to clients every " << m_tcpKeepaliveSecs << " seconds";
        }

        int rv = su_init() ;
        if( rv < 0 ) {
            DR_LOG(log_error) << "Error calling su_init: " << rv ;
            return ;
        }
        ::atexit(su_deinit);
        if (sip_update_default_mclass(sip_extend_mclass(NULL)) < 0) {
            DR_LOG(log_error) << "DrachtioController::run: Error calling sip_update_default_mclass"  ;
            return  ;
        }        
        
        m_root = su_root_create( NULL ) ;
        if( NULL == m_root ) {
            DR_LOG(log_error) << "DrachtioController::run: Error calling su_root_create: "  ;
            return  ;
        }
        m_home = su_home_create() ;
        if( NULL == m_home ) {
            DR_LOG(log_error) << "DrachtioController::run: Error calling su_home_create"  ;
        }
        su_log_redirect(NULL, __sofiasip_logger_func, NULL);
        
        /* for now set logging to full debug */
        su_log_set_level(NULL, m_nSofiaLoglevel >= 0 ? (unsigned int) m_nSofiaLoglevel : m_Config->getSofiaLogLevel() ) ;
        setenv("TPORT_LOG", "1", 1) ;
        
        /* this causes su_clone_start to start a new thread */
        su_root_threading( m_root, 0 ) ;
        rv = su_clone_start( m_root, m_clone, this, clone_init, clone_destroy ) ;
        if( rv < 0 ) {
           DR_LOG(log_error) << "DrachtioController::run: Error calling su_clone_start"  ;
           return  ;
        }

        if( m_vecTransports[0]->hasExternalIp() ) {
            DR_LOG(log_notice) << "DrachtioController::run: starting sip stack on local address " << m_vecTransports[0]->getContact() <<    
                " (external address: " << m_vecTransports[0]->getExternalIp() << ")";   
        }
        else {
            DR_LOG(log_notice) << "DrachtioController::run: starting sip stack on " <<  m_vecTransports[0]->getContact() ;   
        }
        string newUrl; 
        m_vecTransports[0]->getBindableContactUri(newUrl) ;
         
         /* create our agent */
        bool tlsTransport = string::npos != m_vecTransports[0]->getContact().find("sips") || string::npos != m_vecTransports[0]->getContact().find("tls") ;
		m_nta = nta_agent_create( m_root,
         URL_STRING_MAKE(newUrl.c_str()),               /* our contact address */
         stateless_callback,                            /* no callback function */
         this,                                      /* therefore no context */
         TAG_IF( !captureString.empty(), TPTAG_CAPT(captureString.c_str())),
         TAG_IF( tlsTransport && hasTlsFiles && m_bTlsVerifyClientCert, TPTAG_TLS_VERIFY_PEER(true)),
         TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_KEY_FILE(tlsKeyFile.c_str())),
         TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_FILE(tlsCertFile.c_str())),
         TAG_IF( tlsTransport && hasTlsFiles && tlsChainFile.length() > 0, TPTAG_TLS_CERTIFICATE_CHAIN_FILE(tlsChainFile.c_str())),
         TAG_IF( tlsTransport &&hasTlsFiles, 
            TPTAG_TLS_VERSION( tlsVersionTagValue )),
         NTATAG_SERVER_RPORT(2),   //force rport even when client does not provide
         NTATAG_CLIENT_RPORT(true), //add rport on Via headers for requests we send
         NTATAG_PASS_408(true), //pass 408s to application
         TAG_NULL(),
         TAG_END() ) ;
        
        if( NULL == m_nta ) {
            DR_LOG(log_error) << "DrachtioController::run: Error calling nta_agent_create"  ;
            return ;
        }

        SipTransport::addTransports(m_vecTransports[0], m_mtu) ;

        for( std::vector< std::shared_ptr<SipTransport> >::iterator it = m_vecTransports.begin() + 1; it != m_vecTransports.end(); it++ ) {
            string contact = (*it)->getContact() ;
            string externalIp = (*it)->getExternalIp() ;
            string newUrl ;

            tlsTransport = string::npos != contact.find("sips") || string::npos != contact.find("tls") ;

            if( externalIp.length() ) {
                DR_LOG(log_info) << "DrachtioController::run: adding additional internal sip address " << contact << " (external address: " << externalIp << ")" ;                
            }
            else {
                DR_LOG(log_info) << "DrachtioController::run: adding additional sip address " << contact  ;                
            }

            (*it)->getBindableContactUri(newUrl) ;

            rv = nta_agent_add_tport(m_nta, URL_STRING_MAKE(newUrl.c_str()),
                 TAG_IF( !captureString.empty(), TPTAG_CAPT(captureString.c_str())),
                 TAG_IF( tlsTransport && hasTlsFiles && m_bTlsVerifyClientCert, TPTAG_TLS_VERIFY_PEER(true)),
                 TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_KEY_FILE(tlsKeyFile.c_str())),
                 TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_FILE(tlsCertFile.c_str())),
                 TAG_IF( tlsTransport && hasTlsFiles && !tlsChainFile.empty(), TPTAG_TLS_CERTIFICATE_CHAIN_FILE(tlsChainFile.c_str())),
                 TAG_IF( tlsTransport &&hasTlsFiles, 
                    TPTAG_TLS_VERSION( tlsVersionTagValue )),
                 TAG_NULL(),
                 TAG_END() ) ;

            if( rv < 0 ) {
                DR_LOG(log_error) << "DrachtioController::run: Error adding additional transport"  ;
                return ;            
            }
            SipTransport::addTransports(*it, m_mtu) ;
        }

        SipTransport::logTransports() ;

        m_pDialogController = std::make_shared<SipDialogController>( this, &m_clone ) ;
        m_pProxyController = std::make_shared<SipProxyController>( this, &m_clone ) ;
        m_pPendingRequestController = std::make_shared<PendingRequestController>( this ) ;

        // set sip timers
        unsigned int t1, t2, t4, t1x64 ;
        m_Config->getTimers( t1, t2, t4, t1x64 ); 
        DR_LOG(log_debug) << "DrachtioController::run - sip timers: T1: " << std::dec << t1 << "ms, T2: " << t2 << "ms, T4: " << t4 << "ms, T1X64: " << t1x64 << "ms";        
        nta_agent_set_params(m_nta,
            NTATAG_SIP_T1(t1),
            NTATAG_SIP_T2(t2),
            NTATAG_SIP_T4(t4),
            NTATAG_SIP_T1X64(t1x64),
            TAG_END()
        ) ;
    
        /* sofia event loop */
        DR_LOG(log_notice) << "Starting sofia event loop in main thread: " <<  std::this_thread::get_id()  ;

        /* start a timer */
        m_timer = su_timer_create( su_root_task(m_root), 30000) ;
        su_timer_set_for_ever(m_timer, watchdogTimerHandler, this) ;
 
        su_root_run( m_root ) ;
        DR_LOG(log_notice) << "Sofia event loop ended"  ;
        
        su_root_destroy( m_root ) ;
        m_root = NULL ;
        su_home_unref( m_home ) ;
        su_deinit() ;

        m_Config.reset();
        this->deinitializeLogging() ;

        
    }
    int DrachtioController::processMessageStatelessly( msg_t* msg, sip_t* sip ) {
        int rc = 0 ;
        if (m_pBlacklist) {
            string host;
            getSourceAddressForMsg(msg, host);
            if (m_pBlacklist->isBlackListed(host.c_str())) {
                return -1;
            }
        }
        DR_LOG(log_debug) << "processMessageStatelessly - incoming message with call-id " << sip->sip_call_id->i_id <<
            " does not match an existing call leg, processed in thread " << std::this_thread::get_id()  ;

        tport_t* tp_incoming = nta_incoming_transport(m_nta, NULL, msg );
        if (NULL == tp_incoming) {
            DR_LOG(log_error) << "DrachtioController::processMessageStatelessly: unable to get transport for incoming message; discarding call-id " << sip->sip_call_id->i_id ;
            return -1 ;
        }

        tport_t* tp = tport_parent( tp_incoming ) ;
        const tp_name_t* tpn = tport_name( tp );
        tport_unref( tp_incoming ) ;

        if( sip->sip_request ) {
            
            // sofia sanity check on message format
            if( sip_sanity_check(sip) < 0 ) {
                DR_LOG(log_error) << "DrachtioController::processMessageStatelessly: invalid incoming request message; discarding call-id " << sip->sip_call_id->i_id ;
                STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", sip->sip_request->rq_method_name},{"code", "400"}})
                nta_msg_treply( m_nta, msg, 400, NULL, TAG_END() ) ;
                return -1 ;
            }
            // additional sanity checks on headers
            if (sip->sip_error) {
                auto error_header = sip->sip_error;
                if (error_header->er_common[0].h_data && error_header->er_common[0].h_len > 0) {
                    std::string error_string((const char *) error_header->er_common[0].h_data, error_header->er_common[0].h_len);
                    DR_LOG(log_error) << "DrachtioController::processMessageStatelessly: discarding message due to error: " << error_string;
                }
                else {
                    DR_LOG(log_error) << "DrachtioController::processMessageStatelessly: discarding invalid message";
                }
                STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", sip->sip_request->rq_method_name},{"code", "400"}})
                nta_msg_treply( m_nta, msg, 400, NULL, TAG_END() ) ;
                return -1 ;
            }
            
            // spammer check
            string action, tcpAction ;
            DrachtioConfig::mapHeader2Values& mapSpammers =  m_Config->getSpammers( action, tcpAction );
            if( mapSpammers.size() > 0 ) {
                if( 0 == strcmp( tpn->tpn_proto, "tcp") || 0 == strcmp( tpn->tpn_proto, "ws") || 0 == strcmp( tpn->tpn_proto, "wss") ) {
                    if( tcpAction.length() > 0 ) {
                        action = tcpAction ;
                    }
                }

                try {
                    for( DrachtioConfig::mapHeader2Values::iterator it = mapSpammers.begin(); mapSpammers.end() != it; ++it ) {
                        string hdrName = it->first ;
                        vector<string> vecValue = it->second ;

                        // currently limited to looking at User-Agent, From, and To
                        if( 0 == hdrName.compare("user-agent") && sip->sip_user_agent && sip->sip_user_agent->g_string ) {
                            for( std::vector<string>::iterator it = vecValue.begin(); it != vecValue.end(); ++it ) {
                                if( NULL != strstr( sip->sip_user_agent->g_string, (*it).c_str() ) ) {
                                    throw runtime_error(sip->sip_user_agent->g_string) ;
                                }
                            }
                        }
                        if( 0 == hdrName.compare("to") && sip->sip_to && sip->sip_to->a_url->url_user ) {
                            for( std::vector<string>::iterator it = vecValue.begin(); it != vecValue.end(); ++it ) {
                                if( NULL != strstr( sip->sip_to->a_url->url_user, (*it).c_str() ) ) {
                                    throw runtime_error(sip->sip_to->a_url->url_user) ;
                                }
                            }
                        }
                        if( 0 == hdrName.compare("from") && sip->sip_from && sip->sip_from->a_url->url_user ) {
                            for( std::vector<string>::iterator it = vecValue.begin(); it != vecValue.end(); ++it ) {
                                if( NULL != strstr( sip->sip_from->a_url->url_user, (*it).c_str() ) ) {
                                    throw runtime_error(sip->sip_from->a_url->url_user) ;
                                }
                            }
                        }
                    }
                } catch( runtime_error& err ) {
                    nta_incoming_t* irq = nta_incoming_create( m_nta, NULL, msg, sip, NTATAG_TPORT(tp), TAG_END() ) ;
                    if (sip->sip_request->rq_method != sip_method_ack) {
                        const char* remote_host = nta_incoming_remote_host(irq);
                        const char *remote_port = nta_incoming_remote_port(irq);
                        const char* what = err.what();
                        if (remote_host && remote_port && what) {
                            DR_LOG(log_notice) << "DrachtioController::processMessageStatelessly: detected potential spammer from " <<
                                nta_incoming_remote_host(irq) << ":" << nta_incoming_remote_port(irq)  << 
                                " due to header value: " << err.what()  ;
                        }
                        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", sip->sip_request->rq_method_name},{"code", "603"}})
                        nta_incoming_treply( irq, 603, "Decline", TAG_END() ) ;
                        nta_incoming_destroy(irq) ;   

                        /*
                        if( 0 == action.compare("reject") ) {
                            nta_msg_treply( m_nta, msg, 603, NULL, TAG_END() ) ;
                        }
                        */
                        return -1 ;
                    }
                }
            }

            if( sip->sip_route && sip->sip_to->a_tag != NULL && url_has_param(sip->sip_route->r_url, "lr") ) {

                //check if we are in the first Route header, and the request-uri is not us; if so proxy accordingly
                bool hostMatch = 
                  (0 == strcmp( tpn->tpn_host, sip->sip_route->r_url->url_host ) || 
                  (tpn->tpn_canon && 0 == strcmp( tpn->tpn_canon, sip->sip_route->r_url->url_host )));
                bool portMatch = 
                  (tpn->tpn_port && sip->sip_route->r_url->url_port && 0 == strcmp(tpn->tpn_port,sip->sip_route->r_url->url_port)) ||
                  (!tpn->tpn_port && !sip->sip_route->r_url->url_port) ||
                  (!tpn->tpn_port && 0 == strcmp(sip->sip_route->r_url->url_port, "5060")) ||
                  (!sip->sip_route->r_url->url_port && 0 == strcmp(tpn->tpn_port, "5060")) ;


                if( /*hostMatch && portMatch && */ SipTransport::isLocalAddress(sip->sip_route->r_url->url_host)) {
                    //request within an established dialog in which we are a stateful proxy
                    if( !m_pProxyController->processRequestWithRouteHeader( msg, sip ) ) {
                       nta_msg_discard( m_nta, msg ) ;                
                    }          
                    return 0 ;          
                }
                DR_LOG(log_warning) << "DrachtioController::processMessageStatelessly: discarding incoming message with Route header as we do not match the first route";
            }

            if( m_pProxyController->isProxyingRequest( msg, sip ) ) {
                //CANCEL or other request within a proxy transaction
                m_pProxyController->processRequestWithoutRouteHeader( msg, sip ) ;
            }
            else {
                if (sip_method_register == sip->sip_request->rq_method) {
                    /* optionally reject REGISTER quickly if no sip realm provided */
                    if (m_bRejectRegisterWithNoRealm && sip_method_register == sip->sip_request->rq_method ) {
                        std::regex ipRegex("^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}$");
                        if (std::regex_match(sip->sip_request->rq_url->url_host, ipRegex)) {
                            DR_LOG(log_info) << "DrachtioController::processMessageStatelessly: rejecting REGISTER with no realm" ;
                            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", "REGISTER"},{"code", "403"}})
                            nta_msg_treply( m_nta, msg, 403, NULL, TAG_END() ) ;
                            return -1 ;
                        }
                    }
                    
                    /* reject register with Contact: * if Expires is not 0 */
                    if (sip->sip_contact && 0 == strcmp(sip->sip_contact->m_url[0].url_scheme, "*") &&
                        sip->sip_expires && sip->sip_expires->ex_delta != 0) {
                        DR_LOG(log_info) << "DrachtioController::processMessageStatelessly: rejecting REGISTER with Contact: * and non-zero Expires" ;
                        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", "REGISTER"},{"code", "400"}})
                        nta_msg_treply( m_nta, msg, 400, NULL, TAG_END() ) ;
                        return -1 ;
                    }
                }
               
                switch (sip->sip_request->rq_method ) {
                    case sip_method_invite:
                    case sip_method_register:
                    case sip_method_message:
                    case sip_method_options:
                    case sip_method_info:
                    case sip_method_notify:
                    case sip_method_subscribe:
                    case sip_method_publish:
                    {
                        if( m_pPendingRequestController->isRetransmission( sip ) ||
                            m_pProxyController->isRetransmission( sip ) ) {
                        
                            DR_LOG(log_info) << "discarding retransmitted request: " << sip->sip_call_id->i_id  ;
                            int ret = sip_method_invite == sip->sip_request->rq_method ? 100 : -1 ;
                            nta_msg_discard(m_nta, msg) ;  
                            return ret ;
                        }

                        if( sip_method_invite == sip->sip_request->rq_method ) {
                          if (-1 == nta_msg_treply( m_nta, msg_ref_create( msg ), 100, NULL, TAG_END() )) {
                            DR_LOG(log_info) << "failed sending 100 Trying: " << sip->sip_call_id->i_id  ;
                            return -1;
                          }
                          if (m_bAlwaysSend180) {
                            if (-1 == nta_msg_treply( m_nta, msg_ref_create( msg ), 180, NULL, TAG_END() )) {
                              DR_LOG(log_info) << "failed sending 180 Ringing: " << sip->sip_call_id->i_id  ;
                              return -1;
                            }
                          }
                        }
                        if (sip_method_options == sip->sip_request->rq_method && sip->sip_user_agent && sip->sip_user_agent->g_string) {
                            if (0 == m_strUserAgentAutoAnswerOptions.compare(sip->sip_user_agent->g_string)) {
                                nta_msg_treply( m_nta, msg, 200, NULL, TAG_END() ) ;
                                return -1 ;
                            }
                        }

                        string transactionId ;
                        int status = m_pPendingRequestController->processNewRequest( msg, sip, tp_incoming, transactionId ) ;

                        //write attempt record	
                        if( status >= 0 && sip->sip_request->rq_method == sip_method_invite ) {	
                            Cdr::postCdr( std::make_shared<CdrAttempt>( msg, "network" ) );	
                        }
                        
                        //reject message if necessary, write stop record
                        if( status > 0  ) {
                            bool isInvite = sip->sip_request->rq_method == sip_method_invite;
                            msg_t* reply = nta_msg_create(m_nta, 0) ;
                            msg_ref_create(reply) ;
                            nta_msg_mreply( m_nta, reply, sip_object(reply), status, NULL, msg, TAG_END() ) ;

                            if( isInvite ) {
                                Cdr::postCdr( std::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );
                            }
                            msg_destroy(reply) ;
                           return 0;
                        }
                    }
                    break ;

                    case sip_method_prack:
                        assert(0) ;//should not get here
                    break ;

                    case sip_method_cancel:
                    {
                        std::shared_ptr<PendingRequest_t> p = m_pPendingRequestController->findInviteByCallId( sip->sip_call_id->i_id ) ;
                        if( p ) {
                            DR_LOG(log_info) << "received quick cancel for invite that is out to client for disposition: " << sip->sip_call_id->i_id  ;

                            string encodedMessage ;
                            EncodeStackMessage( sip, encodedMessage ) ;
                            SipMsgData_t meta( msg ) ;

                            client_ptr client = m_pClientController->findClientForNetTransaction(p->getTransactionId()); 
                            if(client) {
                                void (BaseClient::*fn)(const string&, const string&, const SipMsgData_t&) = &BaseClient::sendSipMessageToClient;
                                m_pClientController->getIOService().post( std::bind(fn, client, p->getTransactionId(), encodedMessage, meta)) ;
                            }

                            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", sip->sip_request->rq_method_name},{"code", "200"}})
                            nta_msg_treply( m_nta, msg, 200, NULL, TAG_END() ) ;  
                            p->cancel() ;
                            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", "INVITE"},{"code", "487"}})
                            nta_msg_treply( m_nta, msg_dup(p->getMsg()), 487, NULL, TAG_END() ) ;
                        }
                        else {
                            STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", sip->sip_request->rq_method_name},{"code", "481"}})
                            nta_msg_treply( m_nta, msg, 481, NULL, TAG_END() ) ;                              
                        }
                    }
                    break ;
                    
                    case sip_method_update:
                    case sip_method_bye:
                        STATS_COUNTER_INCREMENT(STATS_COUNTER_SIP_RESPONSES_OUT, {{"method", sip->sip_request->rq_method_name},{"code", "481"}})
                        nta_msg_treply( m_nta, msg, 481, NULL, TAG_END() ) ;   
                        break;                           


                    default:
                        nta_msg_discard( m_nta, msg ) ;
                    break ;
                }             
            }
        }
        else {
            if( !m_pProxyController->processResponse( msg, sip ) ) {
                if( sip->sip_via->v_next ) {
                    DR_LOG(log_error) << "processMessageStatelessly - forwarding response upstream" ;
                    nta_msg_tsend( m_nta, msg, NULL, TAG_END() ) ; 
                }
                else {
                    DR_LOG(log_error) << "processMessageStatelessly - unknown response (possibly late arriving?) - discarding" ;
                    nta_msg_discard( m_nta, msg ) ;                    
                }
            } 
        }
        return rc ;
    }

    bool DrachtioController::setupLegForIncomingRequest( const string& transactionId, const string& tag ) {
        //DR_LOG(log_debug) << "DrachtioController::setupLegForIncomingRequest - entering"  ;
        std::shared_ptr<PendingRequest_t> p = m_pPendingRequestController->findAndRemove( transactionId ) ;
        if( !p ) {
            return false ;
        }
        if (p->isCanceled()) {
            DR_LOG(log_notice) << "DrachtioController::setupLegForIncomingRequest - app provided a response but INVITE has already been canceled" ;
            return false;
        }
        sip_t* sip = p->getSipObject() ;
        msg_t* msg = p->getMsg() ;
        tport_t* tp = p->getTport() ;

        if (tport_is_shutdown(tp)) {
            DR_LOG(log_notice) << "DrachtioController::setupLegForIncomingRequest - tport has been closed: " << std::hex << (void *) tp ;
            return false;
        }

        if( sip_method_invite == sip->sip_request->rq_method || sip_method_subscribe == sip->sip_request->rq_method ) {

            nta_incoming_t* irq = nta_incoming_create( m_nta, NULL, msg, sip, NTATAG_TPORT(tp), TAG_END() ) ;
            if( NULL == irq ) {
                DR_LOG(log_error) << "DrachtioController::setupLegForIncomingRequest - Error creating a transaction for new incoming invite" ;
                return false ;
            }

            nta_leg_t* leg = nta_leg_tcreate(m_nta, legCallback, this,
                                           SIPTAG_CALL_ID(sip->sip_call_id),
                                           SIPTAG_CSEQ(sip->sip_cseq),
                                           SIPTAG_TO(sip->sip_from),
                                           SIPTAG_FROM(sip->sip_to),
                                           TAG_END());
            if( NULL == leg ) {
                DR_LOG(log_error) << "DrachtioController::setupLegForIncomingRequest - Error creating a leg for new incoming invite"  ;
                return false ;
            }

            DR_LOG(log_debug) << "DrachtioController::setupLegForIncomingRequest - created leg: " << hex << leg << ", irq: " << irq << 
                ", for transactionId: " << transactionId << ", tag: " << tag;


            std::shared_ptr<SipDialog> dlg = std::make_shared<SipDialog>( leg, irq, sip, msg ) ;
            dlg->setTransactionId( transactionId ) ;

            string contactStr ;
            nta_leg_server_route( leg, sip->sip_record_route, sip->sip_contact ) ;

            m_pDialogController->addIncomingInviteTransaction( leg, irq, sip, transactionId, dlg, tag ) ;            
        }
        else {
          /* first try to find the original incoming irq */
          nta_incoming_t* irq = nta_incoming_find(m_nta, sip, sip->sip_via);
				  if (!irq) {
            irq = nta_incoming_create( m_nta, NULL, msg, sip, NTATAG_TPORT(tp), TAG_END() ) ;
            if( NULL == irq ) {
                DR_LOG(log_error) << "DrachtioController::setupLegForIncomingRequest - Error creating a transaction for new incoming invite or subscribe" ;
                return false ;
            }
          }
          else {
            DR_LOG(log_debug) << "DrachtioController::setupLegForIncomingRequest - found existing irq " << std::hex << (void *)irq ;
          }
          m_pDialogController->addIncomingRequestTransaction( irq, transactionId ) ;
        }
        msg_ref_create( msg ) ; // we need to add a reference to the original request message
        return true ;
    }

    int DrachtioController::processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "DrachtioController::processRequestInsideDialog"  ;

        int rc = validateSipMessage( sip ) ;
        if( 0 != rc ) {
            return rc ;
        }
         
        std::shared_ptr<SipDialog> dlg ;
        if( m_pDialogController->findDialogByLeg( leg, dlg ) ) {
            if( sip->sip_request->rq_method == sip_method_invite && !sip->sip_to->a_tag && dlg->getSipStatus() >= 200 ) {
               DR_LOG(log_info) << "DrachtioController::processRequestInsideDialog - received INVITE out of order (still waiting ACK from prev transaction)" ;
               return 491;
               //return this->processMessageStatelessly( nta_incoming_getrequest( irq ), (sip_t *) sip ) ;
            }

            return m_pDialogController->processRequestInsideDialog( leg, irq, sip ) ;
        }
        assert(false) ;

        return 0 ;
    }
     sip_time_t DrachtioController::getTransactionTime( nta_incoming_t* irq ) {
        return nta_incoming_received( irq, NULL ) ;
    }
    void DrachtioController::getTransactionSender( nta_incoming_t* irq, string& host, unsigned int& port ) {
        msg_t* msg = nta_incoming_getrequest( irq ) ;   //adds a reference
        tport_t* tp = nta_incoming_transport( m_nta, irq, msg);
        msg_destroy(msg);   //releases reference
        const tp_name_t* tpn = tport_name( tp );
        host = tpn->tpn_host ;
        port = ::atoi( tpn->tpn_port ) ;

    }

    const tport_t* DrachtioController::getTportForProtocol( const string& remoteHost, const char* proto ) {
      std::shared_ptr<SipTransport> p = SipTransport::findAppropriateTransport(remoteHost.c_str(), proto) ;
      return p->getTport() ;
    }

    int DrachtioController::validateSipMessage( sip_t const *sip ) {
        if( sip_method_invite == sip->sip_request->rq_method  && (!sip->sip_contact || !sip->sip_contact->m_url[0].url_host ) ) {
            DR_LOG(log_error) << "Invalid or missing contact header"  ;
            return 400 ;
        }
        if( !sip->sip_call_id || !sip->sip_call_id->i_id ) {
            DR_LOG(log_error) << "Invalid or missing call-id header"  ;
            return 400 ;
        }
        if( sip_method_invite == sip->sip_request->rq_method  && !sip->sip_to  ) {
            DR_LOG(log_error) << "Invalid or missing to header"  ;
            return 400 ;            
        }
        if( sip_method_invite == sip->sip_request->rq_method  && (!sip->sip_from || !sip->sip_from->a_tag ) )  {
            DR_LOG(log_error) << "Missing tag on From header on invite"  ;
            return 400 ;            
        }
        return 0 ;
    }
    void DrachtioController::getMyHostports( vector<string>& vec ) {
      return SipTransport::getAllHostports( vec ) ;
    }
    bool DrachtioController::getMySipAddress( const char* proto, string& host, string& port, bool ipv6 ) {
        string desc, p ;
        const tport_t* tp = getTportForProtocol(host, proto) ;
        if( !tp ) {            
            DR_LOG(log_error) << "DrachtioController::getMySipAddress - invalid or non-configured protocol: " << proto  ;
            assert( 0 ) ;
            return false;
        }
        getTransportDescription( tp, desc ) ;

        return parseTransportDescription( desc, p, host, port ) ;
    }

    void DrachtioController::cacheTportForSubscription( const char* user, const char* host, int expires, tport_t* tp ) {
        if (host == nullptr) return;
        string uri ;
        std::shared_ptr<UaInvalidData> pUa = std::make_shared<UaInvalidData>(user, host, expires, tp) ;
        pUa->getUri( uri ) ;

        std::pair<mapUri2InvalidData::iterator, bool> ret = m_mapUri2InvalidData.insert( mapUri2InvalidData::value_type( uri, pUa) );  
        if( ret.second == false ) {
            mapUri2InvalidData::iterator it = ret.first ;
            pUa = it->second;
            //*(it->second) = *pUa ;
            pUa->extendExpires(expires);
            pUa->setTport(tp);
            DR_LOG(log_debug) << "DrachtioController::cacheTportForSubscription updated "  << uri << ", expires: " << expires << 
                " tport: " << (void*) tp << ", count is now: " << m_mapUri2InvalidData.size();
        }
        else {
            std::shared_ptr<UaInvalidData> p = (ret.first)->second ;
            DR_LOG(log_info) << "DrachtioController::cacheTportForSubscription added "  << uri << 
                ", tport:" << (void *) tp << ", expires: " << expires << ", count is now: " << m_mapUri2InvalidData.size();
        }
    }
    void DrachtioController::flushTportForSubscription( const char* user, const char* host ) {
        if (host == nullptr) return;
        string uri = "" ;
        if (user) {
            uri.append(user) ;
            uri.append("@") ;
        }
        uri.append(host) ;

        mapUri2InvalidData::iterator it = m_mapUri2InvalidData.find( uri ) ;
        if( m_mapUri2InvalidData.end() != it ) {
            m_mapUri2InvalidData.erase( it ) ;
        }
        DR_LOG(log_info) << "DrachtioController::flushTportForSubscription "  << uri <<  ", count is now: " << m_mapUri2InvalidData.size();
    }
    std::shared_ptr<UaInvalidData> DrachtioController::findTportForSubscription( const char* user, const char* host ) {
        std::shared_ptr<UaInvalidData> p ;
        string uri = "" ;

        if (user) {
            uri.append(user) ;
            uri.append("@") ;
        }
        uri.append(host) ;

        mapUri2InvalidData::iterator it = m_mapUri2InvalidData.find( uri ) ;
        if( m_mapUri2InvalidData.end() != it ) {
            p = it->second ;
            DR_LOG(log_debug) << "DrachtioController::findTportForSubscription: found transport for " << uri  ;
        }
        else {
            DR_LOG(log_info) << "DrachtioController::findTportForSubscription: no transport found for " << uri  ;
        }
        return p ;
    }

  void DrachtioController::makeOutboundConnection(const string& transactionId, const string& uri) {
    string host ;
    string port = "9021";
    string transport = "tcp";

    try {
        std::regex re("^(.*):(\\d+)(;transport=(tcp|tls))?");
        std::smatch mr;
        if (std::regex_search(uri, mr, re) && mr.size() > 1) {
            host = mr[1] ;
            port = mr[2] ;
            if (mr.size() > 4) {
                transport = mr[4];
            }
        }
        else {
          DR_LOG(log_warning) << "DrachtioController::makeOutboundConnection - invalid uri: " << uri;
          //TODO: send 480, remove pending connection
          return ;              
        }
    } catch (std::regex_error& e) {
        DR_LOG(log_warning) << "DrachtioController::makeOutboundConnection - regex error: " << e.what();
        return;        
    }

    DR_LOG(log_warning) << "DrachtioController::makeOutboundConnection - attempting connection to " << 
      host << ":" << port << " with transport " << transport;
    m_pClientController->makeOutboundConnection(transactionId, host, port, transport) ;
  }

  void DrachtioController::selectInboundConnectionForTag(const string& transactionId, const string& tag) {
    m_pClientController->selectClientForTag(transactionId, tag);
  }

  // handling responses from http route lookups
  // N.B.: these execute in the http thread, not the main thread
  void DrachtioController::httpCallRoutingComplete(const string& transactionId, long response_code, 
    const string& body) {

    std::ostringstream msg ;
    json_t *root;
    json_error_t error;
    root = json_loads(body.c_str(), 0, &error);

    DR_LOG(log_debug) << "DrachtioController::httpCallRoutingComplete thread id " << std::this_thread::get_id() << 
      " transaction id " << transactionId << " response: (" << response_code << ") " << body ; 

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
              throw std::runtime_error("DrachtioController::processRoutingInstructions - invalid 'contact' array: must contain strings") ;  
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
              throw std::runtime_error("DrachtioController::processRoutingInstructions - invalid 'contact' array: must contain strings") ;  
            }
            vecContact.push_back( json_string_value(aContact) );
          }
        }
        else {
          throw std::runtime_error("DrachtioController::processRoutingInstructions - invalid 'contact' attribute in redirect action: must be string or array") ;  
        }
        processRedirectInstruction(transactionId, vecContact);

      }
      else if( 0 == strcmp("route", actionText)) {
        json_t* uri = json_object_get(data, "uri") ;
        json_t* tag = json_object_get(data, "tag") ;

        if(uri && json_is_string(uri)) {
            processOutboundConnectionInstruction(transactionId, json_string_value(uri));
        }
        else if(tag && json_is_string(tag)) {
            processTaggedConnectionInstruction(transactionId, json_string_value(tag));
        }
        else {
          throw std::runtime_error("'uri' is missing or is not a string") ;  
        }
      }
      else {
        msg << "DrachtioController::processRoutingInstructions - invalid 'action' attribute value '" << actionText << 
            "': valid values are 'reject', 'proxy', 'redirect', and 'route'" ;
        return throw std::runtime_error(msg.str()) ;  
      }

      json_decref(root) ; 
    } catch( std::runtime_error& err ) {
      DR_LOG(log_error) << "DrachtioController::processRoutingInstructions " << err.what();
      DR_LOG(log_error) << body ;
      processRejectInstruction(transactionId, 500) ;
      if( root ) { 
        json_decref(root) ; 
      }
    }
      // clean up needed?  not in reject scenarios, nor redirect nor route (proxy?)
      //m_pController->getPendingRequestController()->findAndRemove( transactionId ) ;
  }

  void DrachtioController::processRejectInstruction(const string& transactionId, unsigned int status, 
    const char* reason) {
      string headers;
      string body ;
      std::ostringstream statusLine ;

      statusLine << "SIP/2.0 " << status << " " ;
      if( reason ) {
          statusLine << reason ;
      }
      else {
          std::unordered_map<unsigned int, std::string>::const_iterator it = responseReasons.find(status) ;
          if( it != responseReasons.end() ) {
              statusLine << it->second ;
          }
      }
      if(( !this->getDialogController()->respondToSipRequest("", transactionId, statusLine.str(), headers, body) )) {
          DR_LOG(log_error) << "DrachtioController::processRejectInstruction - error sending rejection with status " << status ;
      }
  }
  void DrachtioController::processRedirectInstruction(const string& transactionId, vector<string>& vecContact) {
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

      if(( !this->getDialogController()->respondToSipRequest( "", transactionId, "SIP/2.0 302 Moved", headers, body) )) {
          DR_LOG(log_error) << "DrachtioController::processRedirectInstruction - error sending redirect" ;
      }
  }

  void DrachtioController::processProxyInstruction(const string& transactionId, bool recordRoute, bool followRedirects, 
    bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, vector<string>& vecDestination) {
    string headers;
    string body ;

    this->getProxyController()->proxyRequest( "", transactionId, recordRoute, false, followRedirects, 
      simultaneous, provisionalTimeout, finalTimeout, vecDestination, headers ) ;
  }

  void DrachtioController::processOutboundConnectionInstruction(const string& transactionId, const char* uri) {
    string val = uri ;
    makeOutboundConnection(transactionId, val);
  }

  void DrachtioController::processTaggedConnectionInstruction(const string& transactionId, const char* tag) {
    string val = tag ;
    selectInboundConnectionForTag(transactionId, val);
  }

    void DrachtioController::printStats(bool bDetail) {
       bool bMemoryDebug = m_bMemoryDebug || m_bDumpMemory;

       usize_t irq_hash = -1, orq_hash = -1, leg_hash = -1;
       usize_t irq_used = -1, orq_used = -1, leg_used = -1 ;
       usize_t recv_msg = -1, sent_msg = -1;
       usize_t recv_request = -1, recv_response = -1;
       usize_t bad_message = -1, bad_request = -1, bad_response = -1;
       usize_t drop_request = -1, drop_response = -1;
       usize_t client_tr = -1, server_tr = -1, dialog_tr = -1;
       usize_t acked_tr = -1, canceled_tr = -1;
       usize_t trless_request = -1, trless_to_tr = -1, trless_response = -1;
       usize_t trless_200 = -1, merged_request = -1;
       usize_t sent_request = -1, sent_response = -1;
       usize_t retry_request = -1, retry_response = -1, recv_retry = -1;
       usize_t tout_request = -1, tout_response = -1;
       sip_time_t now = sip_now();
       nta_agent_get_stats(m_nta,
                                NTATAG_S_IRQ_HASH_REF(irq_hash),
                                NTATAG_S_ORQ_HASH_REF(orq_hash),
                                NTATAG_S_LEG_HASH_REF(leg_hash),
                                NTATAG_S_IRQ_HASH_USED_REF(irq_used),
                                NTATAG_S_ORQ_HASH_USED_REF(orq_used),
                                NTATAG_S_LEG_HASH_USED_REF(leg_used),
                                NTATAG_S_RECV_MSG_REF(recv_msg),
                                NTATAG_S_SENT_MSG_REF(sent_msg),
                                NTATAG_S_RECV_REQUEST_REF(recv_request),
                                NTATAG_S_RECV_RESPONSE_REF(recv_response),
                                NTATAG_S_BAD_MESSAGE_REF(bad_message),
                                NTATAG_S_BAD_REQUEST_REF(bad_request),
                                NTATAG_S_BAD_RESPONSE_REF(bad_response),
                                NTATAG_S_DROP_REQUEST_REF(drop_request),
                                NTATAG_S_DROP_RESPONSE_REF(drop_response),
                                NTATAG_S_CLIENT_TR_REF(client_tr),
                                NTATAG_S_SERVER_TR_REF(server_tr),
                                NTATAG_S_DIALOG_TR_REF(dialog_tr),
                                NTATAG_S_ACKED_TR_REF(acked_tr),
                                NTATAG_S_CANCELED_TR_REF(canceled_tr),
                                NTATAG_S_TRLESS_REQUEST_REF(trless_request),
                                NTATAG_S_TRLESS_TO_TR_REF(trless_to_tr),
                                NTATAG_S_TRLESS_RESPONSE_REF(trless_response),
                                NTATAG_S_TRLESS_200_REF(trless_200),
                                NTATAG_S_MERGED_REQUEST_REF(merged_request),
                                NTATAG_S_SENT_REQUEST_REF(sent_request),
                                NTATAG_S_SENT_RESPONSE_REF(sent_response),
                                NTATAG_S_RETRY_REQUEST_REF(retry_request),
                                NTATAG_S_RETRY_RESPONSE_REF(retry_response),
                                NTATAG_S_RECV_RETRY_REF(recv_retry),
                                NTATAG_S_TOUT_REQUEST_REF(tout_request),
                                NTATAG_S_TOUT_RESPONSE_REF(tout_response),
                           TAG_END()) ;
       
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "size of hash table for server-side transactions                  " << dec << irq_hash  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "size of hash table for client-side transactions                  " << orq_hash  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "size of hash table for dialogs                                   " << leg_hash  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of server-side transactions in the hash table             " << irq_used  ;
       if (bDetail && irq_used > 0) {
           nta_incoming_t* irq = NULL;
           std::deque<nta_incoming_t*> aged;
           do {
               irq = nta_get_next_server_txn_from_hash(m_nta, irq);
               if (irq) {
                   const char* method = nta_incoming_method_name(irq);
                   const char* tag = nta_incoming_gettag(irq);
                   uint32_t seq = nta_incoming_cseq(irq);
                   sip_time_t secsSinceReceived = now - nta_incoming_received(irq, NULL);
                   if (secsSinceReceived > 3600) aged.push_back(irq);
                   DR_LOG(bMemoryDebug ? log_info : log_debug) << "    nta_incoming_t*: " << std::hex << (void *) irq  <<
                    " " << method << " " << std::dec << seq << " remote tag: " << tag << 
                    " alive " << secsSinceReceived << " secs";
               }
           } while (irq) ;
           /*
           std::for_each(aged.begin(), aged.end(), [](nta_incoming_t* irq) {
               DR_LOG(bMemoryDebug ? log_info : log_debug) << "        destroying very old nta_incoming_t*: " <<  std::hex << (void *) irq ;
               nta_incoming_destroy(irq);
           });
           */
       }
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of client-side transactions in the hash table             " << orq_used  ;
       if (bDetail && orq_used > 0) {
           nta_outgoing_t* orq = NULL;
           do {
               orq = nta_get_next_client_txn_from_hash(m_nta, orq);
               if (orq) {
                   const char* method = nta_outgoing_method_name(orq);
                   const char* callId = nta_outgoing_call_id(orq);
                   uint32_t seq = nta_outgoing_cseq(orq);
                   DR_LOG(bMemoryDebug ? log_info : log_debug) << "    nta_outgoing_t*: " << std::hex << (void *) orq  <<
                    " " << method << " " << callId << std::dec << " CSeq: " << seq;
               }
           } while (orq) ;
       }
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of dialogs in the hash table                              " << leg_used  ;
       if (bDetail && leg_used > 0) {
           nta_leg_t* leg = NULL;
           do {
               leg = nta_get_next_dialog_from_hash(m_nta, leg);
               if (leg) {
                   const char* localTag = nta_leg_get_tag(leg);
                   DR_LOG(bMemoryDebug ? log_info : log_debug) << "    nta_leg_t*: " << std::hex << (void *) leg  << 
                    " local tag: " << localTag ;
               }
           } while (leg) ;
       }
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of sip messages received                                  " << recv_msg  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of sip messages sent                                      " << sent_msg  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of sip requests received                                  " << recv_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of sip requests sent                                      " << sent_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of bad sip messages received                              " << bad_message  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of bad sip requests received                              " << bad_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of bad sip requests dropped                               " << drop_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of bad sip reponses dropped                               " << drop_response  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of client transactions created                            " << client_tr  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of server transactions created                            " << server_tr  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of in-dialog server transactions created                  " << dialog_tr  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of server transactions that have received ack             " << acked_tr  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of server transactions that have received cancel          " << canceled_tr  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of requests that were processed stateless                 " << trless_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of requests converted to transactions by message callback " << trless_to_tr  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of responses without matching request                     " << trless_response  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of successful responses missing INVITE client transaction " << trless_200  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of requests merged by UAS                                 " << merged_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of SIP responses sent by stack                            " << sent_response  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of SIP requests retransmitted by stack                    " << retry_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of SIP responses retransmitted by stack                   " << retry_response  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of retransmitted SIP requests received by stack           " << recv_retry  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of SIP client transactions that has timeout               " << tout_request  ;
       DR_LOG(bMemoryDebug ? log_info : log_debug) << "number of SIP server transactions that has timeout               " << tout_response  ;

       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_SERVER_HASH_SIZE, irq_hash)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_CLIENT_HASH_SIZE, orq_hash)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_DIALOG_HASH_SIZE, leg_hash)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_NUM_SERVER_TXNS, irq_used)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_NUM_CLIENT_TXNS, orq_used)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_NUM_DIALOGS, leg_used)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_MSG_RECV, recv_msg)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_MSG_SENT, sent_msg)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_REQ_RECV, recv_request)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_REQ_SENT, sent_request)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_BAD_MSGS, bad_message)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_BAD_REQS, bad_request)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_RETRANS_REQ, retry_request)
       STATS_GAUGE_SET(STATS_GAUGE_SOFIA_RETRANS_RES, retry_response)

       STATS_GAUGE_SET(STATS_GAUGE_REGISTERED_ENDPOINTS, m_mapUri2InvalidData.size());

    }
    void DrachtioController::processWatchdogTimer() {
        DR_LOG(log_debug) << "DrachtioController::processWatchdogTimer"  ;
    
        // expire any UaInvalidData
        for(  mapUri2InvalidData::iterator it = m_mapUri2InvalidData.begin(); it != m_mapUri2InvalidData.end(); ) {
            std::shared_ptr<UaInvalidData> p = it->second ;
            if( p->isExpired() ) {
                string uri  ;
                tport_t* tp = p->getTport();
                p->getUri(uri) ;

                m_mapUri2InvalidData.erase(it++) ;
                DR_LOG(log_info) << "DrachtioController::processWatchdogTimer expiring registration for webrtc client: "  << 
                    uri << " " << (void *)tp << ", count is now " << m_mapUri2InvalidData.size()  ;
            }
            else {
                ++it ;
            }
        }

        bool bMemoryDebug = m_bMemoryDebug || m_bDumpMemory;
        this->printStats(bMemoryDebug) ;
        m_pDialogController->logStorageCount(bMemoryDebug) ;
        m_pClientController->logStorageCount(bMemoryDebug) ;
        m_pPendingRequestController->logStorageCount(bMemoryDebug) ;
        m_pProxyController->logStorageCount(bMemoryDebug) ;
        m_bDumpMemory = false;

        DR_LOG(bMemoryDebug ? log_info : log_debug) << "m_mapUri2InvalidData size:                                       " << m_mapUri2InvalidData.size()  ;

#ifdef SOFIA_MSG_DEBUG_TRACE
        DR_LOG(bMemoryDebug ? log_info : log_debug) << "number allocated msg_t                                           " << sofia_msg_count()  ;
#endif
    }

    void DrachtioController::initStats() {
        STATS_COUNTER_CREATE(STATS_COUNTER_SIP_REQUESTS_IN, "count of sip requests received")
        STATS_COUNTER_CREATE(STATS_COUNTER_SIP_REQUESTS_OUT, "count of sip requests sent")
        STATS_COUNTER_CREATE(STATS_COUNTER_SIP_RESPONSES_IN, "count of sip responses received")
        STATS_COUNTER_CREATE(STATS_COUNTER_SIP_RESPONSES_OUT, "count of sip responses sent")
        STATS_COUNTER_CREATE(STATS_COUNTER_BUILD_INFO, "drachtio version running")

        STATS_GAUGE_CREATE(STATS_GAUGE_START_TIME, "drachtio start time")
        STATS_GAUGE_CREATE(STATS_GAUGE_STABLE_DIALOGS, "count of SIP dialogs in progress")
        STATS_GAUGE_CREATE(STATS_GAUGE_PROXY, "count of proxied call setups in progress")
        STATS_GAUGE_CREATE(STATS_GAUGE_REGISTERED_ENDPOINTS, "count of registered endpoints")
        STATS_GAUGE_CREATE(STATS_GAUGE_CLIENT_APP_CONNECTIONS, "count of connections to drachtio applications")

        //sofia stats
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_CLIENT_HASH_SIZE, "current size of sofia hash table for client transactions")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_SERVER_HASH_SIZE, "current size of sofia hash table for server transactions")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_DIALOG_HASH_SIZE, "current size of sofia hash table for dialogs")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_NUM_SERVER_TXNS, "count of sofia server-side transactions")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_NUM_CLIENT_TXNS, "count of sofia client-side transactions")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_NUM_DIALOGS, "count of sofia dialogs")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_MSG_RECV, "count of sip messages received by sofia sip stack")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_MSG_SENT, "count of sip messages sent by sofia sip stack")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_REQ_RECV, "count of sip requests received by sofia sip stack")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_REQ_SENT, "count of sip requests sent by sofia sip stack")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_BAD_MSGS, "count of invalid sip messages received by sofia sip stack")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_BAD_REQS, "count of invalid sip requests received by sofia sip stack")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_RETRANS_REQ, "count of sip requests retransmitted by sofia sip stack")
        STATS_GAUGE_CREATE(STATS_GAUGE_SOFIA_RETRANS_RES, "count of sip responses retransmitted by sofia sip stack")

        STATS_HISTOGRAM_CREATE(STATS_HISTOGRAM_INVITE_RESPONSE_TIME_IN, "call answer time in seconds for calls received", 
            {1.0, 3.0, 6.0, 10.0, 15.0, 20.0, 30.0, 60.0})
        STATS_HISTOGRAM_CREATE(STATS_HISTOGRAM_INVITE_RESPONSE_TIME_OUT, "call answer time in seconds for calls sent", 
            {1.0, 3.0, 6.0, 10.0, 15.0, 20.0, 30.0, 60.0})
        STATS_HISTOGRAM_CREATE(STATS_HISTOGRAM_INVITE_PDD_IN, "call post-dial delay seconds for calls received", 
            {1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0, 20.0})
        STATS_HISTOGRAM_CREATE(STATS_HISTOGRAM_INVITE_PDD_OUT, "call post-dial delay seconds for calls received", 
            {1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0, 20.0})

        STATS_COUNTER_INCREMENT(STATS_COUNTER_BUILD_INFO, {{"version", DRACHTIO_VERSION}})
        STATS_GAUGE_SET_TO_CURRENT_TIME(STATS_GAUGE_START_TIME)
    }


}

