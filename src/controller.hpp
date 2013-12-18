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
#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/nta_stateless.h>

#include <sys/stat.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include <boost/log/common.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/syslog_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/bimap.hpp>

#include "drachtio.h"
#include "drachtio-config.hpp"
#include "client-controller.hpp"
#include "sip-dialog-controller.hpp"
#include "sip-dialog.hpp"

using namespace std ;

namespace drachtio {
	
	class DrachtioController ;

	using boost::shared_ptr;
	using boost::scoped_ptr;

	class DrachtioController {
	public:

        	DrachtioController( int argc, char* argv[]  ) ;
        	~DrachtioController() ;

        	void handleSigHup( int signal ) ;
        	void run() ;
        	src::severity_logger_mt<severity_levels>& getLogger() const { return *m_logger; }
                src::severity_logger_mt< severity_levels >* createLogger() ;
              
                boost::shared_ptr<DrachtioConfig> getConfig(void) { return m_Config; }
                boost::shared_ptr<SipDialogController> getDialogController(void) { return m_pDialogController ; }
                boost::shared_ptr<ClientController> getClientController(void) { return m_pClientController ; }
                su_root_t* getRoot(void) { return m_root; }
                
                enum severity_levels getCurrentLoglevel() { return m_current_severity_threshold; }

                /* network --> client messages */
                int processRequestOutsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) ;
                int processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) ;

                /* client --> network messages */
                int sendRequestInsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid ) ;

                /* called by dialog maker when a dialog has been produced */
                void notifyDialogConstructionComplete( boost::shared_ptr<SipDialog> dlg ) ;

                bool isSecret( const string& secret ) {
                	return m_Config->isSecret( secret ) ;
                }

                nta_agent_t* getAgent(void) { return m_nta; }
                su_home_t* getHome(void) { return m_home; }

                const sip_contact_t* getMyContact(void) { return m_my_contact; }
                void getMyHostport( string& str ) {
                	str = m_my_contact->m_url->url_host ;
                	if( m_my_contact->m_url->url_port ) {
                		str.append(":") ;
                		str.append( m_my_contact->m_url->url_port ) ;
                	}
                }

                void printStats(void) ;
                void processWatchdogTimer(void) ;

                sip_time_t getTransactionTime( nta_incoming_t* irq ) ;
                void getTransactionSender( nta_incoming_t* irq, string& host, unsigned int& port ) ;

	private:
        	DrachtioController() ;

        	bool parseCmdArgs( int argc, char* argv[] ) ;
        	void usage() ;

        	void generateOutgoingContact( sip_contact_t* const incomingContact, string& strContact ) ;
        	
        	void daemonize() ;
        	void initializeLogging() ;
        	void deinitializeLogging() ;
        	bool installConfig() ;
        	void logConfig() ;

        	scoped_ptr< src::severity_logger_mt<severity_levels> > m_logger ;
        	boost::mutex m_mutexGlobal ;
        	boost::shared_mutex m_mutexConfig ; 
        	bool m_bLoggingInitialized ;
        	string m_configFilename ;
                
                string  m_user ;    //system user to run as

                shared_ptr< sinks::synchronous_sink< sinks::syslog_backend > > m_sink ;
                shared_ptr<DrachtioConfig> m_Config, m_ConfigNew ;
                int m_bDaemonize ;
                severity_levels m_current_severity_threshold ;

                shared_ptr< ClientController > m_pClientController ;

                boost::shared_ptr<SipDialogController> m_pDialogController ;
 
                su_home_t* 	m_home ;
                su_root_t* 	m_root ;
                su_timer_t*     m_timer ;
                nta_agent_t*	m_nta ;
                nta_leg_t*      m_defaultLeg ;
                string          m_my_via ;
                sip_contact_t*  m_my_contact ;

        	su_clone_r 	m_clone ;

        } ;

} ;


#endif //__CONTROLLER_H__
