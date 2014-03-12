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

//#include <boost/log/filters.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/core.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>

#include <sofia-sip/msg_addr.h>

#include <jansson.h>

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

#include "controller.hpp"

/* clone static functions, used to post a message into the main su event loop from the worker client controller thread */
namespace {
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
        
        if( theOneAndOnlyController->getCurrentLoglevel() < drachtio::log_error ) return ;
        
        static bool loggingSipMsg = false ;
        static ostringstream sipMsg ;
 
        if( loggingSipMsg && 0 == strcmp(fmt, "\n") ) {
            sipMsg << endl ;
            return ;
        }
        char output[MAXLOGLEN+1] ;
        vsnprintf( output, MAXLOGLEN, fmt, ap ) ;
        va_end(ap) ;

        if( !loggingSipMsg ) {
            if( ::strstr( output, "recv ") == output || ::strstr( output, "send ") == output ) {
                loggingSipMsg = true ;
                sipMsg.str("") ;
                /* remove the message separator that sofia puts in there */
                char* szStartSeparator = strstr( output, "   " MSG_SEPARATOR ) ;
                if( NULL != szStartSeparator ) *szStartSeparator = '\0' ;
             }
        }
        else if( NULL != ::strstr( fmt, MSG_SEPARATOR) ) {
            loggingSipMsg = false ;
            sipMsg.flush() ;
            DR_LOG( drachtio::log_info ) << sipMsg.str() <<  " " << endl;
            return ;
        }
        
        if( loggingSipMsg ) {
            int i = 0 ;
            while( ' ' == output[i] && '\0' != output[i]) i++ ;
            sipMsg << ( output + i ) ;
        }
        else {
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

 }

namespace drachtio {

    DrachtioController::DrachtioController( int argc, char* argv[] ) : m_bDaemonize(false), m_bLoggingInitialized(false),
        m_configFilename(DEFAULT_CONFIG_FILENAME) {
        
        if( !parseCmdArgs( argc, argv ) ) {
            usage() ;
            exit(-1) ;
        }
        
        m_Config = boost::make_shared<DrachtioConfig>( m_configFilename.c_str() ) ;

        if( !m_Config->isValid() ) {
            exit(-1) ;
        }
        this->installConfig() ;

#ifdef DEBUG
        json_set_alloc_funcs(my_json_malloc, my_json_free);
#endif

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
        DR_LOG(log_notice) << "Logging threshold:                     " << (int) m_current_severity_threshold << endl ;
    }

    void DrachtioController::handleSigHup( int signal ) {
        
        if( !m_ConfigNew ) {
            DR_LOG(log_notice) << "Re-reading configuration file" << endl ;
            m_ConfigNew.reset( new DrachtioConfig( m_configFilename.c_str() ) ) ;
            if( !m_ConfigNew->isValid() ) {
                DR_LOG(log_error) << "Error reading configuration file; no changes will be made.  Please correct the configuration file and try to reload again" << endl ;
                m_ConfigNew.reset() ;
            }
        }
        else {
            DR_LOG(log_error) << "Ignoring signal; already have a new configuration file to install" << endl ;
        }
        
    
    }

    bool DrachtioController::parseCmdArgs( int argc, char* argv[] ) {        
        int c ;
        while (1)
        {
            static struct option long_options[] =
            {
                /* These options set a flag. */
                {"daemon", no_argument,       &m_bDaemonize, true},
                
                /* These options don't set a flag.
                 We distinguish them by their indices. */
                {"file",    required_argument, 0, 'f'},
                {"user",    required_argument, 0, 'u'},
                {0, 0, 0, 0}
            };
            /* getopt_long stores the option index here. */
            int option_index = 0;
            
            c = getopt_long (argc, argv, "f:i:",
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
                    cout << "option " << long_options[option_index].name << endl;
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
                cout << argv[optind++] << endl;
        }
                
        return true ;
    }

    void DrachtioController::usage() {
        cout << "drachtio -f <filename> --daemon" << endl ;
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
            cout << pid << endl ;
            exit(EXIT_SUCCESS);
        }
        if( !m_user.empty() ) {
            struct passwd *pw = getpwnam( m_user.c_str() );
            
            if( pw ) {
                int rc = setuid( pw->pw_uid ) ;
                if( 0 != rc ) {
                    cerr << "Error setting userid to user " << m_user << ": " << errno << endl ;
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
       logging::core::get()->remove_sink( m_sink ) ;
        m_sink.reset() ;
    }
    void DrachtioController::initializeLogging() {
        try {
            // Create a syslog sink
            sinks::syslog::facility facility  ;
            string syslogAddress = "localhost" ;
            unsigned int syslogPort = 516 ;
            
            m_Config->getSyslogFacility( facility ) ;
            m_Config->getSyslogTarget( syslogAddress, syslogPort ) ;
            
            m_sink.reset(
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

            m_sink->locked_backend()->set_severity_mapper(mapping);

            // Set the remote address to sent syslog messages to
            m_sink->locked_backend()->set_target_address( syslogAddress.c_str() );
            
            logging::core::get()->add_global_attribute("RecordID", attrs::counter< unsigned int >());
            
            logging::core::get()->set_filter(
               expr::attr<severity_levels>("Severity") <= m_current_severity_threshold
            ) ;

            // Add the sink to the core
            logging::core::get()->add_sink(m_sink);
            
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

        DR_LOG(log_debug) << "Main thread id: " << boost::this_thread::get_id() << endl ;

       /* open stats connection */
        string adminAddress ;
        unsigned int adminPort = m_Config->getAdminPort( adminAddress ) ;
        if( 0 != adminPort ) {
            m_pClientController.reset( new ClientController( this, adminAddress, adminPort )) ;
        }

        string url ;
        m_Config->getSipUrl( url ) ;
        DR_LOG(log_notice) << "starting sip stack on " << url << endl ;
        
        int rv = su_init() ;
        if( rv < 0 ) {
            DR_LOG(log_error) << "Error calling su_init: " << rv << endl ;
            return ;
        }
        ::atexit(su_deinit);
        
        m_root = su_root_create( NULL ) ;
        if( NULL == m_root ) {
            DR_LOG(log_error) << "Error calling su_root_create: " << endl ;
            return  ;
        }
        m_home = su_home_create() ;
        if( NULL == m_home ) {
            DR_LOG(log_error) << "Error calling su_home_create" << endl ;
        }
        su_log_redirect(NULL, __sofiasip_logger_func, NULL);
        
        /* for now set logging to full debug */
        su_log_set_level(NULL, m_Config->getSofiaLogLevel() ) ;
        setenv("TPORT_LOG", "1", 1) ;
        
        /* this causes su_clone_start to start a new thread */
        su_root_threading( m_root, 0 ) ;
        rv = su_clone_start( m_root, m_clone, this, clone_init, clone_destroy ) ;
        if( rv < 0 ) {
           DR_LOG(log_error) << "Error calling su_clone_start" << endl ;
           return  ;
        }
        
        /* enable extended headers */
        if (sip_update_default_mclass(sip_extend_mclass(NULL)) < 0) {
            DR_LOG(log_error) << "Error calling sip_update_default_mclass" << endl ;
            return  ;
        }
 
         /* create our agent */
        char str[URL_MAXLEN] ;
        memset(str, 0, URL_MAXLEN) ;
        strncpy( str, url.c_str(), url.length() ) ;
        
		m_nta = nta_agent_create( m_root,
                                 URL_STRING_MAKE(str),               /* our contact address */
                                 NULL,         /* no callback function */
                                 NULL,                  /* therefore no context */
                                 TAG_NULL(),
                                 TAG_END() ) ;
        
        if( NULL == m_nta ) {
            DR_LOG(log_error) << "Error calling nta_agent_create" << endl ;
            return ;
        }
        
        m_defaultLeg = nta_leg_tcreate(m_nta, defaultLegCallback, this,
                                      NTATAG_NO_DIALOG(1),
                                      TAG_END());
        if( NULL == m_defaultLeg ) {
            DR_LOG(log_error) << "Error creating default leg" << endl ;
            return ;
        }
        
        
        /* save my contact url, via, etc */
        m_my_contact = nta_agent_contact( m_nta ) ;
        ostringstream s ;
        s << "SIP/2.0/UDP " <<  m_my_contact->m_url[0].url_host ;
        if( m_my_contact->m_url[0].url_port ) s << ":" <<  m_my_contact->m_url[0].url_port  ;
        m_my_via.assign( s.str().c_str(), s.str().length() ) ;
        DR_LOG(log_debug) << "My via header: " << m_my_via << endl ;

        m_pDialogController = boost::make_shared<SipDialogController>( this, &m_clone ) ;
              
        /* sofia event loop */
        DR_LOG(log_notice) << "Starting sofia event loop in main thread: " <<  boost::this_thread::get_id() << endl ;

        /* start a timer */
        m_timer = su_timer_create( su_root_task(m_root), 30000) ;
        su_timer_set_for_ever(m_timer, watchdogTimerHandler, this) ;
 
        su_root_run( m_root ) ;
        DR_LOG(log_notice) << "Sofia event loop ended" << endl ;
        
        su_root_destroy( m_root ) ;
        m_root = NULL ;
        su_home_unref( m_home ) ;
        su_deinit() ;

        m_Config.reset();
        this->deinitializeLogging() ;

        
    }
    int DrachtioController::processRequestOutsideDialog( nta_leg_t* defaultLeg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "processRequestOutsideDialog" << endl ;
        int rc = validateSipMessage( sip ) ;
        if( 0 != rc ) {
            return rc ;
        }

        switch (sip->sip_request->rq_method ) {
            case sip_method_invite:
            {
                /* TODO:  should support optional config to only allow invites from defined addresses */

                /* system-wide minimum session-expires is 90 seconds */
                if( sip->sip_session_expires && sip->sip_session_expires->x_delta < 90 ) {
                      nta_incoming_treply( irq, SIP_422_SESSION_TIMER_TOO_SMALL, 
                        SIPTAG_MIN_SE_STR("90"),
                        TAG_END() ) ; 
                      return 0;
                } 

                string transactionId ;
                generateUuid( transactionId ) ;

                nta_incoming_treply( irq, SIP_100_TRYING, TAG_END() ) ;                
 
                 if( !m_pClientController->route_request_outside_dialog( irq, sip, transactionId ) )  {
                    DR_LOG(log_error) << "No providers available for invite" << endl ;
                    return 503 ;
                }

                nta_leg_t* leg = nta_leg_tcreate(m_nta, legCallback, this,
                                                   SIPTAG_CALL_ID(sip->sip_call_id),
                                                   SIPTAG_CSEQ(sip->sip_cseq),
                                                   SIPTAG_TO(sip->sip_from),
                                                   SIPTAG_FROM(sip->sip_to),
                                                   TAG_END());
                if( NULL == leg ) {
                    DR_LOG(log_error) << "Error creating a leg for  origination" << endl ;
                    //TODO: we got a client out there with a dead INVITE now...
                    return 500 ;
                }
                boost::shared_ptr<SipDialog> dlg = boost::make_shared<SipDialog>( leg, irq, sip ) ;
                dlg->setTransactionId( transactionId ) ;

                string contactStr ;
                generateOutgoingContact( sip->sip_contact, contactStr ) ;
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
            case sip_method_notify:
            {
                string transactionId ;
                generateUuid( transactionId ) ;

                if( !m_pClientController->route_request_outside_dialog( irq, sip, transactionId ) )  {
                    DR_LOG(log_error) << "No providers available for register" << endl ;
                    return 503 ;
                }

                m_pDialogController->addIncomingRequestTransaction( irq, transactionId ) ;
                return 0 ;
            }
            
            case sip_method_bye:
            case sip_method_cancel:
                DR_LOG(log_error) << "Received BYE or CANCEL for unknown dialog: " << sip->sip_call_id->i_id << endl ;
                return 481 ;
                
            default:
                DR_LOG(log_error) << "DrachtioController::processRequestOutsideDialog - unsupported method type: " << sip->sip_request->rq_method_name << ": " << sip->sip_call_id->i_id << endl ;
                return 501 ;
                break ;
                
        }
        
        return 0 ;
    }
    int DrachtioController::processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) {
        DR_LOG(log_debug) << "DrachtioController::processRequestInsideDialog" << endl ;

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
    int DrachtioController::sendRequestInsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid ) {
        boost::shared_ptr<SipDialog> dlg ;
        const char *dialogId=NULL ;
        json_error_t err ;
        
        if( 0 > json_unpack_ex(pMsg->value(), &err, 0, "{s:{s:s}}", "data", "dialogId", &dialogId) ) {
            DR_LOG(log_error) << "DrachtioController::sendRequestInsideDialog - failed parsing message: " << err.text << endl ;
            return -1 ;
        }
        if( !m_pDialogController->findDialogById( dialogId, dlg ) ) {
            //DO I need to look in dialog maker also ?  What about an UPDATE sent during an INVITE transaction?
            return -1;
        }
        m_pDialogController->sendRequestInsideDialog( pMsg, rid, dlg ) ;

        return 0 ;
    }
    void DrachtioController::notifyDialogConstructionComplete( boost::shared_ptr<SipDialog> dlg ) {
        DR_LOG(log_debug) << "notifyDialogConstructionComplete: final sip status " << dlg->getSipStatus() << endl ;
        int finalStatus =  dlg->getSipStatus() ;
        if( 200 == finalStatus ) {
            /* hand stable dialogs over to this guy for further management */
            m_pDialogController->addDialog( dlg ) ;
        }
        else {
            /* the rest are destroyed at this point */
            DR_LOG(log_debug) << "notifyDialogConstructionComplete: dialog with call-id " << dlg->getCallId() << 
                " has final sip status of " << finalStatus << " and will be destroyed" << endl ;
        }
    }
    sip_time_t DrachtioController::getTransactionTime( nta_incoming_t* irq ) {
        return nta_incoming_received( irq, NULL ) ;
    }
    void DrachtioController::getTransactionSender( nta_incoming_t* irq, string& host, unsigned int& port ) {
        su_sockaddr_t su[1];
        socklen_t sulen = sizeof su;
        msg_t* msg = nta_incoming_getrequest( irq ) ;
        if( 0 != msg_get_address(msg, su, &sulen) ) {
            throw std::runtime_error("Failed trying to retrieve socket associated with incoming sip message") ;             
        }
        char h[256], s[256] ;
        su_getnameinfo(su, sulen, h, 256, s, 256, NI_NUMERICHOST | NI_NUMERICSERV);

        host = h ;
        port = ::atoi( s ) ;
    }

    void DrachtioController::generateOutgoingContact( sip_contact_t* const incomingContact, string& strContact ) {
        ostringstream o ;
        
        if( incomingContact->m_display && *incomingContact->m_display ) {
            o << incomingContact->m_display ;
        }
        o << "<sip:" ;
        
        if( incomingContact->m_url[0].url_user ) {
            o << incomingContact->m_url[0].url_user ;
            o << "@" ;
        }
        sip_contact_t* contact = nta_agent_contact( m_nta ) ;
        o << contact->m_url[0].url_host ;
        if( contact->m_url[0].url_port && *contact->m_url[0].url_port ) {
            o << ":" ;
            o << contact->m_url[0].url_port ;
        }
        o << ">" ;
        
        DR_LOG(log_debug) << "generated Contact: " << o.str() << endl ;
        
        strContact = o.str() ;
    }


    int DrachtioController::validateSipMessage( sip_t const *sip ) {
        if( sip_method_invite == sip->sip_request->rq_method  && (!sip->sip_contact || !sip->sip_contact->m_url[0].url_host ) ) {
            DR_LOG(log_error) << "Invalid or missing contact header" << endl ;
            return 400 ;
        }
        if( !sip->sip_call_id || !sip->sip_call_id->i_id ) {
            DR_LOG(log_error) << "Invalid or missing call-id header" << endl ;
            return 400 ;
        }
        if( sip_method_invite == sip->sip_request->rq_method  && (!sip->sip_to || !sip->sip_to->a_url[0].url_user ) ) {
            DR_LOG(log_error) << "Invalid or missing to header or dialed number information" << endl ;
            return 400 ;            
        }
        if( sip_method_invite == sip->sip_request->rq_method  && (!sip->sip_from || !sip->sip_from->a_tag ) )  {
            DR_LOG(log_error) << "Missing tag on From header on invite" << endl ;
            return 400 ;            
        }
        return 0 ;
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
       
       DR_LOG(log_debug) << "size of hash table for server-side transactions                  " << irq_hash << endl ;
       DR_LOG(log_debug) << "size of hash table for client-side transactions                  " << orq_hash << endl ;
       DR_LOG(log_info) << "size of hash table for dialogs                                   " << leg_hash << endl ;
       DR_LOG(log_info) << "number of server-side transactions in the hash table             " << irq_used << endl ;
       DR_LOG(log_info) << "number of client-side transactions in the hash table             " << orq_used << endl ;
       DR_LOG(log_info) << "number of dialogs in the hash table                              " << leg_used << endl ;
       DR_LOG(log_info) << "number of sip messages received                                  " << recv_msg << endl ;
       DR_LOG(log_info) << "number of sip messages sent                                      " << sent_msg << endl ;
       DR_LOG(log_info) << "number of sip requests received                                  " << recv_request << endl ;
       DR_LOG(log_info) << "number of sip requests sent                                      " << sent_request << endl ;
       DR_LOG(log_debug) << "number of bad sip messages received                              " << bad_message << endl ;
       DR_LOG(log_debug) << "number of bad sip requests received                              " << bad_request << endl ;
       DR_LOG(log_debug) << "number of bad sip requests dropped                               " << drop_request << endl ;
       DR_LOG(log_debug) << "number of bad sip reponses dropped                               " << drop_response << endl ;
       DR_LOG(log_debug) << "number of client transactions created                            " << client_tr << endl ;
       DR_LOG(log_debug) << "number of server transactions created                            " << server_tr << endl ;
       DR_LOG(log_info) << "number of in-dialog server transactions created                  " << dialog_tr << endl ;
       DR_LOG(log_debug) << "number of server transactions that have received ack             " << acked_tr << endl ;
       DR_LOG(log_debug) << "number of server transactions that have received cancel          " << canceled_tr << endl ;
       DR_LOG(log_debug) << "number of requests that were processed stateless                 " << trless_request << endl ;
       DR_LOG(log_debug) << "number of requests converted to transactions by message callback " << trless_to_tr << endl ;
       DR_LOG(log_debug) << "number of responses without matching request                     " << trless_response << endl ;
       DR_LOG(log_debug) << "number of successful responses missing INVITE client transaction " << trless_200 << endl ;
       DR_LOG(log_debug) << "number of requests merged by UAS                                 " << merged_request << endl ;
       DR_LOG(log_info) << "number of SIP requests sent by stack                             " << sent_request << endl ;
       DR_LOG(log_info) << "number of SIP responses sent by stack                            " << sent_response << endl ;
       DR_LOG(log_info) << "number of SIP requests retransmitted by stack                    " << retry_request << endl ;
       DR_LOG(log_info) << "number of SIP responses retransmitted by stack                   " << retry_response << endl ;
       DR_LOG(log_info) << "number of retransmitted SIP requests received by stack           " << recv_retry << endl ;
       DR_LOG(log_debug) << "number of SIP client transactions that has timeout               " << tout_request << endl ;
       DR_LOG(log_debug) << "number of SIP server transactions that has timeout               " << tout_response << endl ;
    }
    void DrachtioController::processWatchdogTimer() {
        DR_LOG(log_debug) << "DrachtioController::processWatchdogTimer" << endl ;
        this->printStats() ;
        m_pDialogController->logStorageCount() ;
    }

}

