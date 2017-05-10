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

#include <boost/date_time/posix_time/posix_time.hpp>


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

#include <sofia-sip/msg_addr.h>
#include <sofia-sip/sip_util.h>

#define DEFAULT_CONFIG_FILENAME "/etc/drachtio.conf.xml"
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
        strm << std::hex << std::setw(8) << std::setfill('0') << logging::extract< unsigned int >("LineID", rec) << "|";
        strm << logging::extract<boost::posix_time::ptime>("TimeStamp", rec) << "| ";
        //strm << logging::extract<unsigned int>("Severity", rec) << "| ";

        strm << rec[expr::smessage];
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
            //boost::replace_all(output, "\n", " ") ;
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
        m_configFilename(DEFAULT_CONFIG_FILENAME), m_adminPort(0), m_bNoConfig(false), m_bClusterExperimental(false) {
        
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
        
        m_current_severity_threshold = m_Config->getLoglevel() ;
        
        return true ;
        
    }
    void DrachtioController::logConfig() {
        DR_LOG(log_notice) << "Logging threshold:                     " << (int) m_current_severity_threshold  ;
        if( m_bClusterExperimental ) {
            DR_LOG(log_notice) << "experimental cluster features are enabled" ;
        }
    }

    void DrachtioController::handleSigTerm( int signal ) {
        DR_LOG(log_notice) << "Received SIGTERM; exiting.."  ;
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
                {"user",    required_argument, 0, 'u'},
                {"port",    required_argument, 0, 'p'},
                {"contact",    required_argument, 0, 'c'},
                {"cluster-experimental",    no_argument, 0, 'x'},
                {"version",    no_argument, 0, 'v'},
                {0, 0, 0, 0}
            };
            /* getopt_long stores the option index here. */
            int option_index = 0;
            
            c = getopt_long (argc, argv, "f:i:p:c:",
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
                                        
                case 'f':
                    m_configFilename = optarg ;
                    break;

                case 'u':
                    m_user = optarg ;
                    break;

                case 'c':
                    m_sipContact = optarg ;
                    break;
                                                            
                case 'p':
                    port = optarg ;
                    m_adminPort = ::atoi( port.c_str() ) ;
                    break;

                case 'x':
                    m_bClusterExperimental = true ;
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
        cout << "drachtio -f <path-to-config-file> --user <user-to-run-as> --port <tcp port for admin connections> --daemon --noconfig --version"  ;
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

            if( m_bNoConfig || m_Config->getConsoleLogTarget() ) {
                cout << "adding console logger now" << endl;
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
                            keywords::format = 
                            (
                                expr::stream
                                    << expr::attr< unsigned int >("RecordID")
                                    << ": "
                                    << expr::attr< boost::posix_time::ptime >("TimeStamp")
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

        string url = m_sipContact ;
        vector<string> urls ;
        if( 0 == m_sipContact.length() ) {
            m_Config->getSipUrls( urls ) ;
        }
        else {
            urls.push_back( m_sipContact ) ;
        }

        DR_LOG(log_notice) << "DrachtioController::run: starting sip stack on " << urls[0] ;

        string outboundProxy ;
        if( m_Config->getSipOutboundProxy(outboundProxy) ) {
            DR_LOG(log_notice) << "DrachtioController::run: outbound proxy " << outboundProxy ;
        }

        string tlsKeyFile, tlsCertFile, tlsChainFile ;
        bool hasTlsFiles = m_Config->getTlsFiles( tlsKeyFile, tlsCertFile, tlsChainFile ) ;
        
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
        su_log_set_level(NULL, m_Config->getSofiaLogLevel() ) ;
        setenv("TPORT_LOG", "1", 1) ;
        
        /* this causes su_clone_start to start a new thread */
        su_root_threading( m_root, 0 ) ;
        rv = su_clone_start( m_root, m_clone, this, clone_init, clone_destroy ) ;
        if( rv < 0 ) {
           DR_LOG(log_error) << "DrachtioController::run: Error calling su_clone_start"  ;
           return  ;
        }
         
         /* create our agent */
        bool tlsTransport = string::npos != urls[0].find("sips") || string::npos != urls[0].find("tls") ;
		m_nta = nta_agent_create( m_root,
                                 URL_STRING_MAKE(urls[0].c_str()),               /* our contact address */
                                 stateless_callback,         /* no callback function */
                                 this,                  /* therefore no context */
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
        m_my_contact = nta_agent_contact( m_nta ) ;

        for( vector<string>::iterator it = urls.begin() + 1; it != urls.end(); it++ ) {
            string url = *it ;
            tlsTransport = string::npos != url.find("sips") || string::npos != url.find("tls") ;

            DR_LOG(log_info) << "DrachtioController::run: adding additional contact " << url  ;

            rv = nta_agent_add_tport(m_nta, URL_STRING_MAKE(url.c_str()),
                                 TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_KEY_FILE(tlsKeyFile.c_str())),
                                 TAG_IF( tlsTransport && hasTlsFiles, TPTAG_TLS_CERTIFICATE_FILE(tlsCertFile.c_str())),
                                 TAG_IF( tlsTransport && hasTlsFiles && tlsChainFile.length() > 0, TPTAG_TLS_CERTIFICATE_CHAIN_FILE(tlsChainFile.c_str())),
                                 TAG_IF( tlsTransport &&hasTlsFiles, 
                                    TPTAG_TLS_VERSION( TPTLS_VERSION_TLSv1 | TPTLS_VERSION_TLSv1_1 | TPTLS_VERSION_TLSv1_2 )),
                                 TAG_NULL(),
                                 TAG_END() ) ;

            if( rv < 0 ) {
                DR_LOG(log_error) << "DrachtioController::run: Error adding additional transport"  ;
                return ;            
            }
        }

        tport_t* tp = nta_agent_tports(m_nta);
        while( NULL != (tp = tport_next(tp) ) ) {
            const tp_name_t* tpn = tport_name(tp) ;
            string desc ;
            m_mapProtocol2Tport.insert(mapProtocol2Tport::value_type(tpn->tpn_proto, tp) ) ;
            getTransportDescription( tp, desc ); 
            DR_LOG(log_info) << "Added transport: " << hex << tp << ": " << desc ;
        }
       
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
            " does not match an existing call leg"  ;

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
                    DR_LOG(log_info) << "DrachtioController::processMessageStatelessly: detected spammer due to header value: " << err.what()  ;
                    if( 0 == action.compare("reject") ) {
                        nta_msg_treply( m_nta, msg, 603, NULL, TAG_END() ) ;
                    }
                    //TODO: TARPIT
                    return -1 ;
                }
            }

            if( sip->sip_route && sip->sip_to->a_tag != NULL && url_has_param(sip->sip_route->r_url, "lr") ) {

                //check if we are in the first Route header; if so proxy accordingly

                bool match = 0 == strcmp( tpn->tpn_host, sip->sip_route->r_url->url_host ) &&
                                0 == strcmp( tpn->tpn_port, sip->sip_route->r_url->url_port ) ;

                tport_unref( tp_incoming ) ;

                if( match ) {
                    //request within an established dialog in which we are a stateful proxy
                    if( !m_pProxyController->processRequestWithRouteHeader( msg, sip ) ) {
                       nta_msg_discard( m_nta, msg ) ;                
                    }          
                    return 0 ;          
                }
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
                    {
                        if( m_pPendingRequestController->isRetransmission( sip ) ||
                            m_pProxyController->isRetransmission( sip ) ) {
                        
                            DR_LOG(log_info) << "discarding retransmitted request: " << sip->sip_call_id->i_id  ;
                            nta_msg_discard(m_nta, msg) ;  
                            return -1 ;
                        }

                        if( sip_method_invite == sip->sip_request->rq_method ) {
                            nta_msg_treply( m_nta, msg_ref_create( msg ), 100, NULL, TAG_END() ) ;  
                        }

                        string transactionId ;
                        int status = m_pPendingRequestController->processNewRequest( msg, sip, transactionId ) ;

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

            //DR_LOG(log_debug) << "DrachtioController::setupLegForIncomingRequest - creating an incoming transaction"  ;
            nta_incoming_t* irq = nta_incoming_create( m_nta, NULL, msg, sip, NTATAG_TPORT(tp), TAG_END() ) ;
            if( NULL == irq ) {
                DR_LOG(log_error) << "DrachtioController::setupLegForIncomingRequest - Error creating a transaction for new incoming invite" ;
                return false ;
            }

            //DR_LOG(log_debug) << "DrachtioController::setupLegForIncomingRequest - creating leg"  ;
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

            DR_LOG(log_debug) << "DrachtioController::setupLegForIncomingRequest - created leg: " << hex << leg << ", irq: " << irq << ", for transactionId: " << transactionId; 


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
                /* TODO:  should support optional config to only allow invites from defined addresses */
                //bool ipv6 = NULL != strstr( sip->sip_request->)
                //tport_t* tp = getTportForProtocol( const char* proto, bool ipv6 ) ;
                //tport_t *nta_incoming_transport(m_nta, irq, msg_t *msg);

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
            return m_pDialogController->processRequestInsideDialog( leg, irq, sip ) ;
        }
        assert(false) ;

        return 0 ;
    }
    /*
    int DrachtioController::sendRequestInsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid, const char* dialogId, const char* call_id ) {
        boost::shared_ptr<SipDialog> dlg ;

        assert( dialogId || call_id ) ;
 
        if( dialogId && !m_pDialogController->findDialogById( dialogId, dlg ) ) {
            return -1;
        }   
        else if( call_id && !m_pDialogController->findDialogByCallId( call_id, dlg ) ) {
            return -1;
        }     
        m_pDialogController->sendRequestInsideDialog( pMsg, rid, dlg ) ;

        return 0 ;
    }
    */
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

    tport_t* DrachtioController::getTportForProtocol( const char* proto, bool ipv6 ) {
        DR_LOG(log_debug) << "DrachtioController::getTportForProtocol: " << proto << (ipv6 ? " for ipv6" : "for ipv4")  ;
        tport_t* tp = NULL ;
        std::pair< mapProtocol2Tport::iterator, mapProtocol2Tport::iterator > itRange = m_mapProtocol2Tport.equal_range( proto ) ;
        for( mapProtocol2Tport::iterator it = itRange.first; it != itRange.second; ++it ) {
            tport_t* tp = it->second ;
            const tp_name_t* tpn = tport_name(tp) ;
            if( (ipv6 && NULL != strstr( tpn->tpn_host, "[") && NULL != strstr( tpn->tpn_host, "]") ) ||
                (!ipv6 && NULL == strstr( tpn->tpn_host, "[") && NULL == strstr( tpn->tpn_host, "]")) ) {
                return tp ;
            }
        }
        return tp ;
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
        for( mapProtocol2Tport::iterator it = m_mapProtocol2Tport.begin(); it != m_mapProtocol2Tport.end(); it++ ) {
            string desc ;
            getTransportDescription( it->second, desc ) ;
            vec.push_back( desc ) ;
        }
    }
    bool DrachtioController::getMySipAddress( const char* proto, string& host, string& port, bool ipv6 ) {
        string desc, p ;
        tport_t* tp = getTportForProtocol( proto, ipv6 ) ;
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
        string uri = "" ;
        uri.append(user) ;
        uri.append("@") ;
        uri.append(host) ;
        boost::shared_ptr<UaInvalidData> p ;
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

