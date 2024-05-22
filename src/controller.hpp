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
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>

#include <sys/stat.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif


#include <boost/log/common.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/syslog_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>

#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include "drachtio.h"
#include "drachtio-config.hpp"
#include "client-controller.hpp"
#include "sip-dialog-controller.hpp"
#include "sip-dialog.hpp"
#include "pending-request-controller.hpp"
#include "sip-proxy-controller.hpp"
#include "ua-invalid.hpp"
#include "sip-transports.hpp"
#include "request-router.hpp"
#include "stats-collector.hpp"
#include "blacklist.hpp"

using namespace std ;

namespace drachtio {
	
    class DrachtioController ;

  class StackMsg {
  public:
    StackMsg( const char *szLine ) ;
    ~StackMsg() {}

    void appendLine(char *szLine, bool done) ;
    bool isIncoming(void) const { return m_bIncoming; }
    bool isComplete(void) const { return m_bComplete ;}
    const string& getSipMessage(void) const { return m_sipMessage; }
    const SipMsgData_t& getSipMetaData(void) const { return m_meta; }
    const string& getFirstLine(void) const { return m_firstLine;}

  private:
    StackMsg() {}

    SipMsgData_t    m_meta ;
    bool            m_bIncoming ;
    bool            m_bComplete ;
    string          m_sipMessage ;
    string          m_firstLine ;
    ostringstream   m_os ;
  } ;

	class DrachtioController {
	public:

  	DrachtioController( int argc, char* argv[]  ) ;
  	~DrachtioController() ;

    void handleSigHup( int signal ) ;
    void handleSigTerm( int signal ) ;
    void handleSigPipe( int signal ) ;
  	void run() ;
  	src::severity_logger_mt<severity_levels>& getLogger() const { return *m_logger; }
    src::severity_logger_mt< severity_levels >* createLogger() ;
  
    std::shared_ptr<DrachtioConfig> getConfig(void) { return m_Config; }
    std::shared_ptr<SipDialogController> getDialogController(void) { return m_pDialogController ; }
    std::shared_ptr<ClientController> getClientController(void) { return m_pClientController ; }
    std::shared_ptr<RequestHandler> getRequestHandler(void) { return m_pRequestHandler ; }
    std::shared_ptr<PendingRequestController> getPendingRequestController(void) { return m_pPendingRequestController ; }
    std::shared_ptr<SipProxyController> getProxyController(void) { return m_pProxyController ; }
    su_root_t* getRoot(void) { return m_root; }
    Blacklist* getBlacklist() { return m_pBlacklist; }
  
    enum severity_levels getCurrentLoglevel() { return m_current_severity_threshold; }

    /* network --> client messages */
    int processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) ;

    /* stateless callback for messages not associated with a leg */
    int processMessageStatelessly( msg_t* msg, sip_t* sip ) ;

    bool setupLegForIncomingRequest( const string& transactionId, const string& tag ) ;

    /* callback from http outbound requests for route selection */
    void httpCallRoutingComplete(const string& transactionId, long response_code, const string& response) ;

    bool isSecret( const string& secret ) {
    	return m_secret.empty() ? m_Config->isSecret( secret ) : 0 == m_secret.compare(secret);
    }

    nta_agent_t* getAgent(void) { return m_nta; }
    su_home_t* getHome(void) { return m_home; }

    void getMyHostports( vector<string>& vec, bool localIpsOnly = false) ;

    bool getMySipAddress( const char* proto, string& host, string& port, bool ipv6 = false ) ;

    void printStats(bool bDetail) ;
    void processWatchdogTimer(void) ;

    const tport_t* getTportForProtocol( const string& remoteHost, const char* proto ) ;

    sip_time_t getTransactionTime( nta_incoming_t* irq ) ;
    void getTransactionSender( nta_incoming_t* irq, string& host, unsigned int& port ) ;

    void setLastSentStackMessage(shared_ptr<StackMsg> msg) { m_lastSentMsg = msg; }
    void setLastRecvStackMessage(shared_ptr<StackMsg> msg) { m_lastRecvMsg = msg; }

    bool isDaemonized(void) { return m_bDaemonize; }
    void cacheTportForSubscription( const char* user, const char* host, int expires, tport_t* tp ) ; 
    void flushTportForSubscription( const char* user, const char* host ) ; 
    std::shared_ptr<UaInvalidData> findTportForSubscription( const char* user, const char* host ) ;

    RequestRouter& getRequestRouter(void) { return m_requestRouter; }
    StatsCollector& getStatsCollector(void) { return m_statsCollector; }

    void makeOutboundConnection(const string& transactionId, const string& uri);
    void makeConnectionForTag(const string& transactionId, const string& tag);
    void selectInboundConnectionForTag(const string& transactionId, const string& tag);

    bool isAggressiveNatEnabled(void) { return m_bAggressiveNatDetection; }
    bool isNatDetectionDisabled(void) { return m_bDisableNatDetection; }

    unsigned int getTcpKeepaliveInterval() { return m_tcpKeepaliveSecs; }

	private:

  	DrachtioController() ;

  	bool parseCmdArgs( int argc, char* argv[] ) ;
    void getEnv(void);
  	void usage() ;
  	
  	void daemonize() ;
  	void initializeLogging() ;
  	void deinitializeLogging() ;
  	bool installConfig() ;
  	void logConfig() ;
    int validateSipMessage( sip_t const *sip ) ;
    void initStats(void);

    void processRejectInstruction(const string& transactionId, unsigned int status, const char* reason = NULL) ;
    void processRedirectInstruction(const string& transactionId, vector<string>& vecContact) ;
    void processProxyInstruction(const string& transactionId, bool recordRoute, bool followRedirects, 
        bool simultaneous, const string& provisionalTimeout, const string& finalTimeout, vector<string>& vecDestination) ;
    void processOutboundConnectionInstruction(const string& transactionId, const char* uri) ;
    void processTaggedConnectionInstruction(const string& transactionId, const char* tag) ;

    void finishRequest( const string& transactionId, const boost::system::error_code& err, 
        unsigned int status_code, const string& body) ;


  	std::unique_ptr< src::severity_logger_mt<severity_levels> > m_logger ;
  	std::mutex m_mutexGlobal ;
  	bool m_bLoggingInitialized ;
  	string m_configFilename ;

    // command-line option overrides
    string  m_user ;    //system user to run as
    unsigned int m_adminTcpPort; 
    unsigned int m_adminTlsPort; 
    string m_tlsKeyFile, m_tlsCertFile, m_tlsChainFile, m_dhParam;
    unsigned int m_mtu;

    string m_publicAddress ;

    boost::shared_ptr< sinks::synchronous_sink< sinks::syslog_backend > > m_sinkSysLog ;
    boost::shared_ptr<  sinks::synchronous_sink< sinks::text_file_backend > > m_sinkTextFile ;
    boost::shared_ptr<  sinks::synchronous_sink< sinks::text_ostream_backend > > m_sinkConsole ;

    std::shared_ptr<DrachtioConfig> m_Config, m_ConfigNew ;
    int m_bDaemonize ;
    int m_bNoConfig ;
    int m_bConsoleLogging;

    severity_levels m_current_severity_threshold ;
    int m_nSofiaLoglevel ;
    string m_strHomerAddress;
    unsigned int m_nHomerPort;
    uint32_t m_nHomerId;
    string m_secret;
    string m_adminAddress;
    string m_redisAddress;
    string m_redisSentinels;
    string m_redisMaster;
    string m_redisPassword;
    string m_redisKey;
    unsigned int m_redisPort;
    unsigned int m_redisRefreshSecs;

    std::shared_ptr<ClientController> m_pClientController ;
    std::shared_ptr<RequestHandler> m_pRequestHandler ;
    std::shared_ptr<SipDialogController> m_pDialogController ;
    std::shared_ptr<SipProxyController> m_pProxyController ;
    std::shared_ptr<PendingRequestController> m_pPendingRequestController ;
    Blacklist *m_pBlacklist ;

    std::shared_ptr<StackMsg> m_lastSentMsg ;
    std::shared_ptr<StackMsg> m_lastRecvMsg ;

    su_home_t* 	m_home ;
    su_root_t* 	m_root ;
    su_timer_t*     m_timer ;
    nta_agent_t*	m_nta ;
    nta_leg_t*      m_defaultLeg ;
  	su_clone_r 	m_clone ;

    std::vector< std::shared_ptr<SipTransport> >  m_vecTransports;
    
    typedef std::unordered_map<string, std::shared_ptr<UaInvalidData> > mapUri2InvalidData ;
    mapUri2InvalidData m_mapUri2InvalidData ;

    bool    m_bIsOutbound ;
    string  m_strRequestServer ;
    string  m_strRequestPath ;

    RequestRouter   m_requestRouter ;
    StatsCollector  m_statsCollector;

    bool    m_bAggressiveNatDetection;
    string m_strPrometheusAddress;
    unsigned int m_nPrometheusPort;

    bool m_bMemoryDebug;
    unsigned int m_tcpKeepaliveSecs;

    bool m_bDumpMemory;

    float m_minTlsVersion;
    bool m_bDisableNatDetection;

    bool m_bAlwaysSend180;

    bool m_bGloballyReadableLogs;
    bool m_bTlsVerifyClientCert;

    int m_bRejectRegisterWithNoRealm;

    string  m_strUserAgentAutoAnswerOptions;
  } ;

} ;


#endif //__CONTROLLER_H__
