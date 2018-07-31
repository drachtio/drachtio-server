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

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
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
#include <boost/regex.hpp>

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
}


namespace {
            
	/* sofia logging is redirected to this function */
	static void __sofiasip_logger_func(void *logarg, char const *fmt, va_list ap) {
        
    static bool loggingSipMsg = false ;
    static boost::shared_ptr<drachtio::StackMsg> msg ;

    char output[MAXLOGLEN+1] ;
    vsnprintf( output, MAXLOGLEN, fmt, ap ) ;
    va_end(ap) ;

    if( loggingSipMsg ) {
      loggingSipMsg = NULL == ::strstr( fmt, MSG_SEPARATOR) ;
      msg->appendLine( output, !loggingSipMsg ) ;

      if( !loggingSipMsg ) {
        //DR_LOG(drachtio::log_debug) << "Completed logging sip message"  ;

        DR_LOG( drachtio::log_info ) << msg->getFirstLine()  << msg->getSipMessage() <<  " " ;            

        msg->isIncoming() 
          ? theOneAndOnlyController->setLastRecvStackMessage( msg ) 
          : theOneAndOnlyController->setLastSentStackMessage( msg ) ;
      }
    }
    else if( ::strstr( output, "recv ") == output || ::strstr( output, "send ") == output ) {
      //DR_LOG(drachtio::log_debug) << "started logging sip message: " << output  ;
      loggingSipMsg = true ;

      char* szStartSeparator = strstr( output, "   " MSG_SEPARATOR ) ;
      if( NULL != szStartSeparator ) *szStartSeparator = '\0' ;

      msg = boost::make_shared<drachtio::StackMsg>( output ) ;
    }
    else {
      int len = strlen(output) ;
      output[len-1] = '\0' ;
      DR_LOG(drachtio::log_info) << output ;
    }
	} ;

  int defaultLegCallback( nta_leg_magic_t* controller,
                       nta_leg_t* leg,
                       nta_incoming_t* irq,
                       sip_t const *sip) {
    
    return controller->processRequestOutsideDialog( leg, irq, sip ) ;
  }
  int legCallback( nta_leg_magic_t* controller,
                       nta_leg_t* leg,
                       nta_incoming_t* irq,
                       sip_t const *sip) {
    
    return controller->processRequestInsideDialog( leg, irq, sip ) ;
  }
  int stateless_callback(nta_agent_magic_t *controller,
                   nta_agent_t *agent,
                   msg_t *msg,
                   sip_t *sip) {
    return controller->processMessageStatelessly( msg, sip ) ;
  }

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
 }

namespace drachtio {

    StackMsg::StackMsg( const char *szLine ) : m_firstLine( szLine ), m_meta( szLine ), m_os(""), m_bIncoming(::strstr( szLine, "recv ") == szLine) {
    }
    void StackMsg::appendLine( char *szLine, bool complete ) {
        if( complete ) {
            m_os.flush() ;
            m_sipMessage = m_os.str() ;
            m_sipMessage.resize( m_sipMessage.length() - 1) ;
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
        m_configFilename(DEFAULT_CONFIG_FILENAME), m_adminPort(0), m_bNoConfig(false), 
        m_current_severity_threshold(log_none), m_nSofiaLoglevel(-1), m_bIsOutbound(false), m_bConsoleLogging(false),
        m_nHomerPort(0), m_nHomerId(0), m_mtu(0) {
        
        if( !parseCmdArgs( argc, argv ) ) {
            usage() ;
            exit(-1) ;
        }
        
        logging::add_common_attributes();
        m_Config = boost::make_shared<DrachtioConfig>( m_configFilename.c_str(), m_bDaemonize ) ;

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

    void DrachtioController::handleSigTerm( int signal ) {
        DR_LOG(log_notice) << "Received SIGTERM; exiting after dumping stats.."  ;
        this->printStats() ;
        m_pDialogController->logStorageCount() ;
        m_pClientController->logStorageCount() ;
        m_pPendingRequestController->logStorageCount() ;
        m_pProxyController->logStorageCount() ;

        nta_agent_destroy(m_nta);
        exit(0);
    }

    void DrachtioController::handleSigHup( int signal ) {
        
        if( !m_ConfigNew ) {
            DR_LOG(log_notice) << "Re-reading configuration file"  ;
            m_ConfigNew.reset( new DrachtioConfig( m_configFilename.c_str() ) ) ;
            if( !m_ConfigNew->isValid() ) {
                DR_LOG(log_error) << "Error reading configuration file; no changes will be made.  Please correct the configuration file and try to reload again"  ;
                m_ConfigNew.reset() ;
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
                {"version",    no_argument, 0, 'v'},
                {0, 0, 0, 0}
            };
            /* getopt_long stores the option index here. */
            int option_index = 0;
            
            c = getopt_long (argc, argv, "a:c:f:hi:l:m:p:n:u:vx:y:z:A:B:C:D:",
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
                        m_vecTransports.push_back( boost::make_shared<SipTransport>(contact, localNet, publicAddress )) ;
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
                    m_adminPort = ::atoi( port.c_str() ) ;
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
          boost::shared_ptr<SipTransport> p = boost::make_shared<SipTransport>(contact, localNet, publicAddress);
          for( std::vector<string>::const_iterator it = vecDnsNames.begin(); it != vecDnsNames.end(); ++it) {
            p->addDnsName(*it);
          }
          m_vecTransports.push_back(p) ;
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
        cerr << "    --daemon           Run the process as a daemon background process" << endl ;
        cerr << "    --cert-file        TLS certificate file" << endl ;
        cerr << "    --chain-file       TLS certificate chain file" << endl ;
        cerr << "-c, --contact          Sip contact url to bind to (see /etc/drachtio.conf.xml for examples)" << endl ;
        cerr << "    --dns-name         specifies a DNS name that resolves to the local host, if any" << endl ;
        cerr << "-f, --file             Path to configuration file (default /etc/drachtio.conf.xml)" << endl ;
        cerr << "    --homer            ip:port of homer/sipcapture agent" << endl ;
        cerr << "    --homer-id         homer agent id to use in HEP messages to identify this server" << endl ;
        cerr << "    --http-handler     http(s) URL to optionally send routing request to for new incoming sip request" << endl ;
        cerr << "    --http-method      method to use with http-handler: GET (default) or POST" << endl ;
        cerr << "    --key-file         TLS key file" << endl ;
        cerr << "-l  --loglevel         Log level (choices: notice, error, warning, info, debug)" << endl ;
        cerr << "    --local-net        CIDR for local subnet (e.g. \"10.132.0.0/20\")" << endl ;
        cerr << "    --mtu              max packet size for UDP (default: system-defined mtu)" << endl ;
        cerr << "-p, --port             TCP port to listen on for application connections (default 9022)" << endl ;
        cerr << "    --sofia-loglevel   Log level of internal sip stack (choices: 0-9)" << endl ;
        cerr << "    --external-ip      External IP address to use in SIP messaging" << endl ;
        cerr << "    --stdout           Log to standard output as well as any configured log destinations" << endl ;
        cerr << "-v  --version          Print version and exit" << endl ;
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
                unsigned int syslogPort;
                
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
                    m_sinkSysLog->locked_backend()->set_target_address( syslogAddress.c_str() );

                    logging::core::get()->add_global_attribute("RecordID", attrs::counter< unsigned int >());

                    // Add the sink to the core
                    logging::core::get()->add_sink(m_sinkSysLog);

                }

                //initialize text file sink, of configured
                string name, archiveDirectory ;
                unsigned int rotationSize, maxSize, minSize ;
                bool autoFlush ;
                if( m_Config->getFileLogTarget( name, archiveDirectory, rotationSize, autoFlush, maxSize, minSize ) ) {

                    m_sinkTextFile.reset(
                        new sinks::synchronous_sink< sinks::text_file_backend >(
                            keywords::file_name = name,                                          
                            keywords::rotation_size = rotationSize * 1024 * 1024,
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
                        keywords::max_size = maxSize * 1024 * 1024,          
                        keywords::min_free_space = minSize * 1024 * 1024   
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
            std::cout << "FAILURE creating logger: " << e.what() << std::endl;
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

        DR_LOG(log_debug) << "DrachtioController::run: Main thread id: " << boost::this_thread::get_id() ;

       /* open admin connection */
        string adminAddress ;
        unsigned int adminPort = m_Config->getAdminPort( adminAddress ) ;
        if( 0 != m_adminPort ) adminPort = m_adminPort ;
        if( 0 != adminPort ) {
            DR_LOG(log_notice) << "DrachtioController::run: listening for client connections on " << adminAddress << ":" << adminPort ;
            m_pClientController.reset( new ClientController( this, adminAddress, adminPort )) ;
        }

        if( 0 == m_vecTransports.size() ) {
            m_Config->getTransports( m_vecTransports ) ; 
        }
        if( 0 == m_vecTransports.size() ) {

            DR_LOG(log_notice) << "DrachtioController::run: no sip contacts provided, will listen on 5060 for udp and tcp " ;
            m_vecTransports.push_back( boost::make_shared<SipTransport>("sip:*;transport=udp,tcp"));            
        }

        string outboundProxy ;
        if( m_Config->getSipOutboundProxy(outboundProxy) ) {
            DR_LOG(log_notice) << "DrachtioController::run: outbound proxy " << outboundProxy ;
        }

        // tls files
        string tlsKeyFile, tlsCertFile, tlsChainFile ;
        bool hasTlsFiles = m_Config->getTlsFiles( tlsKeyFile, tlsCertFile, tlsChainFile ) ;
        if (!m_tlsKeyFile.empty()) tlsKeyFile = m_tlsKeyFile;
        if (!m_tlsCertFile.empty()) tlsCertFile = m_tlsCertFile;
        if (!m_tlsChainFile.empty()) tlsChainFile = m_tlsChainFile;
        if (!hasTlsFiles && !tlsKeyFile.empty() && !tlsCertFile.empty()) hasTlsFiles = true;

        if (hasTlsFiles) {
            DR_LOG(log_notice) << "DrachtioController::run tls key file:         " << tlsKeyFile;
            DR_LOG(log_notice) << "DrachtioController::run tls certificate file: " << tlsCertFile;
            if (!tlsChainFile.empty()) DR_LOG(log_notice) << "DrachtioController::run tls chain file:       " << tlsChainFile;
        }

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
         TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_KEY_FILE(tlsKeyFile.c_str())),
         TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_FILE(tlsCertFile.c_str())),
         TAG_IF( tlsTransport && hasTlsFiles && tlsChainFile.length() > 0, TPTAG_TLS_CERTIFICATE_CHAIN_FILE(tlsChainFile.c_str())),
         TAG_IF( tlsTransport &&hasTlsFiles, 
            TPTAG_TLS_VERSION( TPTLS_VERSION_TLSv1 | TPTLS_VERSION_TLSv1_1 | TPTLS_VERSION_TLSv1_2 )),
         NTATAG_SERVER_RPORT(2),   //force rport even when client does not provide
         NTATAG_CLIENT_RPORT(true), //add rport on Via headers for requests we send
         TAG_NULL(),
         TAG_END() ) ;
        
        if( NULL == m_nta ) {
            DR_LOG(log_error) << "DrachtioController::run: Error calling nta_agent_create"  ;
            return ;
        }

        SipTransport::addTransports(m_vecTransports[0], m_mtu) ;

        for( vector< boost::shared_ptr<SipTransport> >::iterator it = m_vecTransports.begin() + 1; it != m_vecTransports.end(); it++ ) {
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
                 TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_KEY_FILE(tlsKeyFile.c_str())),
                 TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_FILE(tlsCertFile.c_str())),
                 TAG_IF( tlsTransport && hasTlsFiles && !tlsChainFile.empty(), TPTAG_TLS_CERTIFICATE_CHAIN_FILE(tlsChainFile.c_str())),
                 TAG_IF( tlsTransport &&hasTlsFiles, 
                    TPTAG_TLS_VERSION( TPTLS_VERSION_TLSv1 | TPTLS_VERSION_TLSv1_1 | TPTLS_VERSION_TLSv1_2 )),
                 TAG_NULL(),
                 TAG_END() ) ;

            if( rv < 0 ) {
                DR_LOG(log_error) << "DrachtioController::run: Error adding additional transport"  ;
                return ;            
            }
            SipTransport::addTransports(*it, m_mtu) ;
        }

        SipTransport::logTransports() ;

        m_pDialogController = boost::make_shared<SipDialogController>( this, &m_clone ) ;
        m_pProxyController = boost::make_shared<SipProxyController>( this, &m_clone ) ;
        m_pPendingRequestController = boost::make_shared<PendingRequestController>( this ) ;

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
        DR_LOG(log_notice) << "Starting sofia event loop in main thread: " <<  boost::this_thread::get_id()  ;

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

        DR_LOG(log_debug) << "processMessageStatelessly - incoming message with call-id " << sip->sip_call_id->i_id <<
            " does not match an existing call leg, processed in thread " << boost::this_thread::get_id()  ;

        if( sip->sip_request ) {

            // sofia sanity check on message format
            if( sip_sanity_check(sip) < 0 ) {
                DR_LOG(log_error) << "DrachtioController::processMessageStatelessly: invalid incoming request message; discarding call-id " << sip->sip_call_id->i_id ;
                nta_msg_treply( m_nta, msg, 400, NULL, TAG_END() ) ;
                return -1 ;
            }

            tport_t* tp_incoming = nta_incoming_transport(m_nta, NULL, msg );
            tport_t* tp = tport_parent( tp_incoming ) ;
            const tp_name_t* tpn = tport_name( tp );

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

                    DR_LOG(log_notice) << "DrachtioController::processMessageStatelessly: detected potential spammer from " <<
                        nta_incoming_remote_host(irq) << ":" << nta_incoming_remote_port(irq)  << 
                        " due to header value: " << err.what()  ;
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

                tport_unref( tp_incoming ) ;

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
                            nta_msg_discard(m_nta, msg) ;  
                            return sip_method_invite == sip->sip_request->rq_method ? 100 : -1 ;
                        }

                        if( sip_method_invite == sip->sip_request->rq_method ) {
                            nta_msg_treply( m_nta, msg_ref_create( msg ), 100, NULL, TAG_END() ) ;  
                        }

                        string transactionId ;
                        int status = m_pPendingRequestController->processNewRequest( msg, sip, tp_incoming, transactionId ) ;

                        //write attempt record
                        if( status >= 0 && sip->sip_request->rq_method == sip_method_invite ) {

                            Cdr::postCdr( boost::make_shared<CdrAttempt>( msg, "network" ) );
                        }

                        //reject message if necessary, write stop record
                        if( status > 0  ) {
                            msg_t* reply = nta_msg_create(m_nta, 0) ;
                            msg_ref_create(reply) ;
                            nta_msg_mreply( m_nta, reply, sip_object(reply), status, NULL, msg, TAG_END() ) ;

                            if( sip->sip_request->rq_method == sip_method_invite ) {
                                Cdr::postCdr( boost::make_shared<CdrStop>( reply, "application", Cdr::call_rejected ) );
                            }
                            msg_destroy(reply) ;
                            return -1 ;                    
                        }
                    }
                    break ;

                    case sip_method_prack:
                        assert(0) ;//should not get here
                    break ;

                    case sip_method_cancel:
                    {
                        boost::shared_ptr<PendingRequest_t> p = m_pPendingRequestController->findInviteByCallId( sip->sip_call_id->i_id ) ;
                        if( p ) {
                            DR_LOG(log_info) << "received quick cancel for invite that is out to client for disposition: " << sip->sip_call_id->i_id  ;

                            string encodedMessage ;
                            EncodeStackMessage( sip, encodedMessage ) ;
                            SipMsgData_t meta( msg ) ;

                            client_ptr client = m_pClientController->findClientForNetTransaction( p->getTransactionId() ); 

                            if( client ) {
                                m_pClientController->getIOService().post( boost::bind(&Client::sendSipMessageToClient, client, p->getTransactionId(), 
                                    encodedMessage, meta ) ) ;                                
                            }

                            nta_msg_treply( m_nta, msg, 200, NULL, TAG_END() ) ;  
                            p->cancel() ;
                            nta_msg_treply( m_nta, msg_dup(p->getMsg()), 487, NULL, TAG_END() ) ;
                        }
                        else {
                            nta_msg_treply( m_nta, msg, 481, NULL, TAG_END() ) ;                              
                        }
                    }
                    break ;

                    case sip_method_bye:
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

    bool DrachtioController::setupLegForIncomingRequest( const string& transactionId ) {
        //DR_LOG(log_debug) << "DrachtioController::setupLegForIncomingRequest - entering"  ;
        boost::shared_ptr<PendingRequest_t> p = m_pPendingRequestController->findAndRemove( transactionId ) ;
        if( !p ) {
            return false ;
        }
        sip_t* sip = p->getSipObject() ;
        msg_t* msg = p->getMsg() ;
        tport_t* tp = p->getTport() ;

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
                ", for transactionId: " << transactionId;


            boost::shared_ptr<SipDialog> dlg = boost::make_shared<SipDialog>( leg, irq, sip, msg ) ;
            dlg->setTransactionId( transactionId ) ;

            string contactStr ;
            nta_leg_server_route( leg, sip->sip_record_route, sip->sip_contact ) ;

            m_pDialogController->addIncomingInviteTransaction( leg, irq, sip, transactionId, dlg ) ;            
        }
        else {
            nta_incoming_t* irq = nta_incoming_create( m_nta, NULL, msg, sip, NTATAG_TPORT(tp), TAG_END() ) ;
            if( NULL == irq ) {
                DR_LOG(log_error) << "DrachtioController::setupLegForIncomingRequest - Error creating a transaction for new incoming invite or subscribe" ;
                return false ;
            }
            m_pDialogController->addIncomingRequestTransaction( irq, transactionId ) ;
        }
        msg_ref_create( msg ) ; // we need to add a reference to the original request message
        return true ;
    }
    int DrachtioController::processRequestOutsideDialog( nta_leg_t* defaultLeg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "processRequestOutsideDialog"  ;
        assert(0) ;//deprecating this
        int rc = validateSipMessage( sip ) ;
        if( 0 != rc ) {
            return rc ;
        }
 
        switch (sip->sip_request->rq_method ) {
            case sip_method_invite:
            {
                nta_incoming_treply( irq, SIP_100_TRYING, TAG_END() ) ;                

               /* system-wide minimum session-expires is 90 seconds */
                if( sip->sip_session_expires && sip->sip_session_expires->x_delta < 90 ) {
                      nta_incoming_treply( irq, SIP_422_SESSION_TIMER_TOO_SMALL, 
                        SIPTAG_MIN_SE_STR("90"),
                        TAG_END() ) ; 
                      return 0;
                } 
 
                client_ptr client = m_pClientController->selectClientForRequestOutsideDialog( sip->sip_request->rq_method_name ) ;
                if( !client ) {
                    DR_LOG(log_error) << "No providers available for invite"  ;
                    return 503 ;                    
                }
                string transactionId ;
                generateUuid( transactionId ) ;

                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                msg_t* msg = nta_incoming_getrequest( irq ) ;
                SipMsgData_t meta( msg, irq ) ;

                m_pClientController->getIOService().post( boost::bind(&Client::sendSipMessageToClient, client, transactionId, 
                    encodedMessage, meta ) ) ;
                
                m_pClientController->addNetTransaction( client, transactionId ) ;

                nta_leg_t* leg = nta_leg_tcreate(m_nta, legCallback, this,
                                                   SIPTAG_CALL_ID(sip->sip_call_id),
                                                   SIPTAG_CSEQ(sip->sip_cseq),
                                                   SIPTAG_TO(sip->sip_from),
                                                   SIPTAG_FROM(sip->sip_to),
                                                   TAG_END());
                if( NULL == leg ) {
                    DR_LOG(log_error) << "Error creating a leg for  origination"  ;
                    //TODO: we got a client out there with a dead INVITE now...
                    return 500 ;
                }
                boost::shared_ptr<SipDialog> dlg = boost::make_shared<SipDialog>( leg, irq, sip, msg ) ;
                dlg->setTransactionId( transactionId ) ;

                nta_leg_server_route( leg, sip->sip_record_route, sip->sip_contact ) ;

                m_pDialogController->addIncomingInviteTransaction( leg, irq, sip, transactionId, dlg ) ;

            }
            break ;

            case sip_method_ack:

                /* success case: call has been established */
                nta_incoming_destroy( irq ) ;
                return 0 ;               
            case sip_method_register:
            case sip_method_message:
            case sip_method_options:
            case sip_method_info:
            case sip_method_notify:
            {
                string transactionId ;
                generateUuid( transactionId ) ;

                client_ptr client = m_pClientController->selectClientForRequestOutsideDialog( sip->sip_request->rq_method_name ) ;
                if( !client ) {
                    DR_LOG(log_error) << "No providers available for invite"  ;
                    return 503 ;                    
                }
                msg_t* msg = nta_incoming_getrequest( irq ) ;
                string encodedMessage ;
                EncodeStackMessage( sip, encodedMessage ) ;
                SipMsgData_t meta( msg, irq ) ;

                m_pClientController->getIOService().post( boost::bind(&Client::sendSipMessageToClient, client, transactionId, 
                    encodedMessage, meta ) ) ;
                m_pClientController->addNetTransaction( client, transactionId ) ;
                m_pDialogController->addIncomingRequestTransaction( irq, transactionId ) ;
                return 0 ;
            }
            
            case sip_method_bye:
            case sip_method_cancel:
                DR_LOG(log_error) << "Received BYE or CANCEL for unknown dialog: " << sip->sip_call_id->i_id  ;
                return 481 ;
                
            default:
                DR_LOG(log_error) << "DrachtioController::processRequestOutsideDialog - unsupported method type: " << sip->sip_request->rq_method_name << ": " << sip->sip_call_id->i_id  ;
                return 501 ;
                break ;
                
        }
        
        return 0 ;
    }
    int DrachtioController::processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "DrachtioController::processRequestInsideDialog"  ;

        int rc = validateSipMessage( sip ) ;
        if( 0 != rc ) {
            return rc ;
        }
         
        boost::shared_ptr<SipDialog> dlg ;
        if( m_pDialogController->findDialogByLeg( leg, dlg ) ) {
            if( sip->sip_request->rq_method == sip_method_invite && !sip->sip_to->a_tag && dlg->getSipStatus() >= 200 ) {
               DR_LOG(log_info) << "DrachtioController::processRequestInsideDialog - received INVITE out of order (still waiting ACK from prev transaction)" ;
               return this->processMessageStatelessly( nta_incoming_getrequest( irq ), (sip_t *) sip ) ;
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
      boost::shared_ptr<SipTransport> p = SipTransport::findAppropriateTransport(remoteHost.c_str(), proto) ;
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
        string uri ;
        boost::shared_ptr<UaInvalidData> pUa = boost::make_shared<UaInvalidData>(user, host, expires, tp) ;
        pUa->getUri( uri ) ;

        std::pair<mapUri2InvalidData::iterator, bool> ret = m_mapUri2InvalidData.insert( mapUri2InvalidData::value_type( uri, pUa) );  
        if( ret.second == false ) {
            mapUri2InvalidData::iterator it = ret.first ;
            *(it->second) = *pUa ;
            DR_LOG(log_debug) << "DrachtioController::cacheTportForSubscription updated "  << uri << ", expires: " << expires << ", count is now: " << m_mapUri2InvalidData.size();
        }
        else {
            boost::shared_ptr<UaInvalidData> p = (ret.first)->second ;
            p->extendExpires( expires ) ;
            DR_LOG(log_debug) << "DrachtioController::cacheTportForSubscription added "  << uri << ", expires: " << expires << ", count is now: " << m_mapUri2InvalidData.size();
        }
    }
    void DrachtioController::flushTportForSubscription( const char* user, const char* host ) {
        string uri = "" ;
        uri.append(user) ;
        uri.append("@") ;
        uri.append(host) ;

        mapUri2InvalidData::iterator it = m_mapUri2InvalidData.find( uri ) ;
        if( m_mapUri2InvalidData.end() != it ) {
            m_mapUri2InvalidData.erase( it ) ;
        }
        DR_LOG(log_debug) << "DrachtioController::flushTportForSubscription "  << uri <<  ", count is now: " << m_mapUri2InvalidData.size();
    }
    boost::shared_ptr<UaInvalidData> DrachtioController::findTportForSubscription( const char* user, const char* host ) {
        boost::shared_ptr<UaInvalidData> p ;
        string uri = "" ;

        if( !user ) {
            return p ;
        }
        uri.append(user) ;
        uri.append("@") ;
        uri.append(host) ;
        mapUri2InvalidData::iterator it = m_mapUri2InvalidData.find( uri ) ;
        if( m_mapUri2InvalidData.end() != it ) {
            p = it->second ;
            DR_LOG(log_debug) << "DrachtioController::findTportForSubscription: found transport for " << uri  ;
        }
        else {
            DR_LOG(log_debug) << "DrachtioController::findTportForSubscription: no transport found for " << uri  ;
        }
        return p ;
    }

  void DrachtioController::makeOutboundConnection(const string& transactionId, const string& uri) {
    string host ;
    string port = "9021";
    vector<string> strs;

    boost::split(strs, uri, boost::is_any_of(":"));
    if( strs.size() > 2 ) {
      DR_LOG(log_warning) << "DrachtioController::makeOutboundConnection - invalid uri: " << uri;
      //TODO: send 480, remove pending connection
      return ;              
    }
    host = strs.at(0) ;
    if( 2 == strs.size() ) {
      port = strs.at(1);
    }

    DR_LOG(log_warning) << "DrachtioController::makeOutboundConnection - attempting connection to " << 
      host << ":" << port ;
    m_pClientController->makeOutboundConnection(transactionId, host, port) ;
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

    DR_LOG(log_debug) << "DrachtioController::httpCallRoutingComplete thread id " << boost::this_thread::get_id() << 
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
          boost::unordered_map<unsigned int, std::string>::const_iterator it = responseReasons.find(status) ;
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


    // logging / stats 
    
    void DrachtioController::printStats() {
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
       
       DR_LOG(log_debug) << "size of hash table for server-side transactions                  " << dec << irq_hash  ;
       DR_LOG(log_debug) << "size of hash table for client-side transactions                  " << orq_hash  ;
       DR_LOG(log_info) << "size of hash table for dialogs                                   " << leg_hash  ;
       DR_LOG(log_info) << "number of server-side transactions in the hash table             " << irq_used  ;
       DR_LOG(log_info) << "number of client-side transactions in the hash table             " << orq_used  ;
       DR_LOG(log_info) << "number of dialogs in the hash table                              " << leg_used  ;
       DR_LOG(log_info) << "number of sip messages received                                  " << recv_msg  ;
       DR_LOG(log_info) << "number of sip messages sent                                      " << sent_msg  ;
       DR_LOG(log_info) << "number of sip requests received                                  " << recv_request  ;
       DR_LOG(log_info) << "number of sip requests sent                                      " << sent_request  ;
       DR_LOG(log_debug) << "number of bad sip messages received                              " << bad_message  ;
       DR_LOG(log_debug) << "number of bad sip requests received                              " << bad_request  ;
       DR_LOG(log_debug) << "number of bad sip requests dropped                               " << drop_request  ;
       DR_LOG(log_debug) << "number of bad sip reponses dropped                               " << drop_response  ;
       DR_LOG(log_debug) << "number of client transactions created                            " << client_tr  ;
       DR_LOG(log_debug) << "number of server transactions created                            " << server_tr  ;
       DR_LOG(log_info) << "number of in-dialog server transactions created                  " << dialog_tr  ;
       DR_LOG(log_debug) << "number of server transactions that have received ack             " << acked_tr  ;
       DR_LOG(log_debug) << "number of server transactions that have received cancel          " << canceled_tr  ;
       DR_LOG(log_debug) << "number of requests that were processed stateless                 " << trless_request  ;
       DR_LOG(log_debug) << "number of requests converted to transactions by message callback " << trless_to_tr  ;
       DR_LOG(log_debug) << "number of responses without matching request                     " << trless_response  ;
       DR_LOG(log_debug) << "number of successful responses missing INVITE client transaction " << trless_200  ;
       DR_LOG(log_debug) << "number of requests merged by UAS                                 " << merged_request  ;
       DR_LOG(log_info) << "number of SIP requests sent by stack                             " << sent_request  ;
       DR_LOG(log_info) << "number of SIP responses sent by stack                            " << sent_response  ;
       DR_LOG(log_info) << "number of SIP requests retransmitted by stack                    " << retry_request  ;
       DR_LOG(log_info) << "number of SIP responses retransmitted by stack                   " << retry_response  ;
       DR_LOG(log_info) << "number of retransmitted SIP requests received by stack           " << recv_retry  ;
       DR_LOG(log_debug) << "number of SIP client transactions that has timeout               " << tout_request  ;
       DR_LOG(log_debug) << "number of SIP server transactions that has timeout               " << tout_response  ;
    }
    void DrachtioController::processWatchdogTimer() {
        DR_LOG(log_debug) << "DrachtioController::processWatchdogTimer"  ;
    
        // expire any UaInvalidData
        for(  mapUri2InvalidData::iterator it = m_mapUri2InvalidData.begin(); it != m_mapUri2InvalidData.end(); ) {
            boost::shared_ptr<UaInvalidData> p = it->second ;
            if( p->isExpired() ) {
                string uri  ;
                p->getUri(uri) ;
                DR_LOG(log_debug) << "DrachtioController::processWatchdogTimer expiring transport for webrtc client: "  << uri << " " << (void *) p->getTport() ;
                m_mapUri2InvalidData.erase(it++) ;
            }
            else {
                ++it ;
            }
        }

        this->printStats() ;
        m_pDialogController->logStorageCount() ;
        m_pClientController->logStorageCount() ;
        m_pPendingRequestController->logStorageCount() ;
        m_pProxyController->logStorageCount() ;

        DR_LOG(log_info) << "m_mapUri2InvalidData size:                                       " << m_mapUri2InvalidData.size()  ;

#ifdef SOFIA_MSG_DEBUG_TRACE
        DR_LOG(log_debug) << "number allocated msg_t                                           " << sofia_msg_count()  ;
#endif
    }

}

