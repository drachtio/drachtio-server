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
#ifndef __DRACHTIO_H__
#define __DRACHTIO_H__

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <unordered_map>
#include <chrono>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif

#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/syslog_backend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/tokenizer.hpp>

#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operato
#include <boost/lexical_cast.hpp>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_tag.h>
#include <sofia-sip/tport.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/msg_types.h>
#include <sofia-sip/nta.h>

#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include "sip-transports.hpp"

using namespace std ;

const string DR_CRLF = "\r\n" ;
const string DR_CRLF2 = "\r\n\r\n" ;

// metrics
const string STATS_COUNTER_BUILD_INFO = "drachtio_build_info";
const string STATS_COUNTER_SIP_REQUESTS_IN = "drachtio_sip_requests_in_total";
const string STATS_COUNTER_SIP_REQUESTS_OUT = "drachtio_sip_requests_out_total";
const string STATS_COUNTER_SIP_RESPONSES_IN = "drachtio_sip_responses_in_total";
const string STATS_COUNTER_SIP_RESPONSES_OUT = "drachtio_sip_responses_out_total";

const string STATS_GAUGE_START_TIME = "drachtio_time_started";
const string STATS_GAUGE_STABLE_DIALOGS = "drachtio_stable_dialogs";
const string STATS_GAUGE_PROXY = "drachtio_proxy_cores";
const string STATS_GAUGE_REGISTERED_ENDPOINTS = "drachtio_registered_endpoints";
const string STATS_GAUGE_CLIENT_APP_CONNECTIONS = "drachtio_app_connections";

// sofia status
const string STATS_GAUGE_SOFIA_SERVER_HASH_SIZE = "drachtio_sofia_server_txn_hash_size";
const string STATS_GAUGE_SOFIA_CLIENT_HASH_SIZE = "drachtio_sofia_client_txn_hash_size";
const string STATS_GAUGE_SOFIA_DIALOG_HASH_SIZE = "drachtio_sofia_dialog_hash_size";
const string STATS_GAUGE_SOFIA_NUM_SERVER_TXNS = "drachtio_sofia_server_txns";
const string STATS_GAUGE_SOFIA_NUM_CLIENT_TXNS = "drachtio_sofia_client_txns";
const string STATS_GAUGE_SOFIA_NUM_DIALOGS = "drachtio_sofia_dialogs";
const string STATS_GAUGE_SOFIA_MSG_RECV = "drachtio_sofia_msgs_recv";
const string STATS_GAUGE_SOFIA_MSG_SENT = "drachtio_sofia_msgs_sent";
const string STATS_GAUGE_SOFIA_REQ_RECV = "drachtio_sofia_requests_recv_total";
const string STATS_GAUGE_SOFIA_REQ_SENT = "drachtio_sofia_requests_sent";
const string STATS_GAUGE_SOFIA_BAD_MSGS = "drachtio_sofia_bad_msgs_recv";
const string STATS_GAUGE_SOFIA_BAD_REQS = "drachtio_sofia_bad_reqs_recv";
const string STATS_GAUGE_SOFIA_RETRANS_REQ = "drachtio_sofia_retransmitted_requests";
const string STATS_GAUGE_SOFIA_RETRANS_RES = "drachtio_sofia_retransmitted_responses";

const string STATS_HISTOGRAM_INVITE_RESPONSE_TIME_IN = "drachtio_call_answer_seconds_in";
const string STATS_HISTOGRAM_INVITE_RESPONSE_TIME_OUT = "drachtio_call_answer_seconds_out";
const string STATS_HISTOGRAM_INVITE_PDD_IN = "drachtio_call_pdd_seconds_in";
const string STATS_HISTOGRAM_INVITE_PDD_OUT = "drachtio_call_pdd_seconds_out";

#define TIMER_C_MSECS (185000)
#define TIMER_B_MSECS (NTA_SIP_T1 * 64)
#define TIMER_D_MSECS (32500)
#define TIMER_H_MSECS (NTA_SIP_T1 * 64)

#define MSG_ID_LEN (128)
#define MAX_DIALOG_ID_LEN (1024)
#define URI_LEN (256)

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace attrs = boost::log::attributes;
namespace keywords = boost::log::keywords;

namespace drachtio {
    
  class DrachtioController ;
        
	enum severity_levels {
		log_none,
		log_notice,
		log_error,
		log_warning,
	  log_info,
	  log_debug
	};

  typedef std::unordered_map<string, string> mapSipHeader_t ;


	BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", severity_levels) ;

	enum agent_role {
		uac_role
		,uas_role
	}; 

	void getSourceAddressForMsg(msg_t *msg, string& host);

  void makeUniqueSipTransactionIdentifier(sip_t* sip, string& str) ;

	void getTransportDescription( const tport_t* tp, string& desc ) ;

	bool parseTransportDescription( const string& desc, string& proto, string& host, string& port ) ;

	void generateUuid(std::string& uuid) ;

	void parseGenericHeader( msg_common_t* p, std::string& hvalue) ;

	bool isImmutableHdr( const std::string& hdr ) ;

	bool getTagTypeForHdr( const std::string& hdr, tag_type_t& tag ) ;

	bool normalizeSipUri( std::string& uri, int brackets ) ;
  
	bool replaceHostInUri( std::string& uri, const char* szHost, const char* szPort ) ;

	sip_method_t methodType( const std::string& method ) ;

	bool isLocalSipUri( const std::string& uri ) ;

	void* my_json_malloc( size_t bytes ) ;

	void my_json_free( void* ptr ) ;

	void splitLines( const std::string& s, std::vector<std::string>& vec ) ;

	void splitTokens( const std::string& s, std::vector<std::string>& vec ) ;

	void splitMsg( const string& msg, string& meta, string& startLine, string& headers, string& body ) ;

	sip_method_t parseStartLine( const string& startLine, string& methodName, string& requestUri ) ;

	bool FindValueForHeader( const string& headers, const char* hdrName, string& hdrValue) ;

	bool FindCSeqMethod( const string& headers, string& method ) ;

	void EncodeStackMessage( const sip_t* sip, string& encodedMessage ) ;

	bool GetValueForHeader( const string& headers, const char *szHeaderName, string& headerValue ) ;

	tagi_t* makeTags( const string& hdrs, const string& transport, const char* szExternalIP = NULL ) ;
	tagi_t* makeSafeTags( const string& hdrs) ;
	void deleteTags( tagi_t* tags ) ;

	int ackResponse( msg_t* msg ) ;

  bool parseSipUri(const string& uri, string& scheme, string& userpart, string& hostpart, string& port, 
        vector< pair<string,string> >& params) ;

	string urlencode(const string &s);

  int utf8_strlen(const string& str);

	bool isRfc1918(const char* szHost);

	bool sipMsgHasNatEqualsYes( const sip_t* sip, bool weAreUac, bool checkContact );

	static char const rfc3261prefix[] =  "z9hG4bK" ;

	class SipMsgData_t {
	public:
		SipMsgData_t() {} ;
		SipMsgData_t(const string& str ) ;
		SipMsgData_t(msg_t* msg) ;
		SipMsgData_t(msg_t* msg, nta_incoming_t* irq, const char* source = "network") ;
		SipMsgData_t(msg_t* msg, nta_outgoing_t* orq, const char* source = "application") ;

		const string& getProtocol() const { return m_protocol; }
		const string& getBytes() const { return m_bytes; }
		const string& getAddress() const { return m_address; }
		const string& getPort() const { return m_port; }
		const string& getTime() const { return m_time; }
		const string& getSource() const { return m_source; }
		const string& getDestAddress() const { return m_destAddress;}
		const string& getDestPort() const { return m_destPort;}

		void setDestAddress(string& dest) { m_destAddress = dest;}
		void setDestPort(string& dest) { m_destPort = dest;}

		void toMessageFormat(string& s) const {
			s = getSource() + "|" + getBytes() + "|" + getProtocol() + "|" + getAddress() + 
            "|" + getPort() + "|" + getTime() ;
		}

	private:
		void init(msg_t* msg) ;

		string 		m_protocol ;
		string 		m_bytes ;
		string 		m_address ;
		string 		m_port ;
		string 		m_time ;
		string 		m_source ;
		string		m_destAddress;
		string		m_destPort;
	} ;
 }

typedef boost::tokenizer<boost::char_separator<char> > tokenizer ;

#ifdef DRACHTIO_MAIN
drachtio::DrachtioController* theOneAndOnlyController = NULL ;
#else
extern drachtio::DrachtioController* theOneAndOnlyController ;
#endif


#define DR_LOG(level) BOOST_LOG_SEV(theOneAndOnlyController->getLogger(), level) 

#define STATS_COUNTER_CREATE(name, desc) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().counterCreate(name, desc); \
	} \
}

#define STATS_COUNTER_INCREMENT(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().counterIncrement(__VA_ARGS__) ;\
	} \
}
#define STATS_COUNTER_INCREMENT_NOCHECK(...) theOneAndOnlyController->getStatsCollector().counterIncrement(__VA_ARGS__) ;

#define STATS_COUNTER_INCREMENT_BY(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().counterIncrement(__VA_ARGS__); \
	} \
}
#define STATS_COUNTER_INCREMENT_BY_NOCHECK(...) theOneAndOnlyController->getStatsCollector().counterIncrement(__VA_ARGS__);

#define STATS_GAUGE_CREATE(name, desc) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().gaugeCreate(name, desc); \
	} \
}

#define STATS_GAUGE_INCREMENT(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().gaugeIncrement(__VA_ARGS__) ;\
	} \
}
#define STATS_GAUGE_INCREMENT_NOCHECK(...) theOneAndOnlyController->getStatsCollector().gaugeIncrement(__VA_ARGS__) ;

#define STATS_GAUGE_INCREMENT_BY(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().gaugeIncrement(__VA_ARGS__); \
	} \
}
#define STATS_GAUGE_INCREMENT_BY_NOCHECK(...) theOneAndOnlyController->getStatsCollector().gaugeIncrement(__VA_ARGS__);

#define STATS_GAUGE_DECREMENT(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().gaugeDecrement(__VA_ARGS__) ;\
	} \
}
#define STATS_GAUGE_DECREMENT_NOCHECK(...) theOneAndOnlyController->getStatsCollector().gaugeDecrement(__VA_ARGS__);

#define STATS_GAUGE_DECREMENT_BY(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().gaugeDecrement(__VA_ARGS__); \
	} \
}
#define STATS_GAUGE_DECREMENT_BY_NOCHECK(...) theOneAndOnlyController->getStatsCollector().gaugeDecrement(__VA_ARGS__); 

#define STATS_GAUGE_SET(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().gaugeSet(__VA_ARGS__); \
	} \
}
#define STATS_GAUGE_SET_NOCHECK(...) theOneAndOnlyController->getStatsCollector().gaugeSet(__VA_ARGS__);

#define STATS_GAUGE_SET_TO_CURRENT_TIME(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().gaugeSetToCurrentTime(__VA_ARGS__); \
	} \
}
#define STATS_GAUGE_SET_TO_CURRENT_TIME_NOCHECK(...) theOneAndOnlyController->getStatsCollector().gaugeSetToCurrentTime(__VA_ARGS__);

#define STATS_HISTOGRAM_CREATE(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().histogramCreate(__VA_ARGS__); \
	} \
}

#define STATS_HISTOGRAM_OBSERVE(...) \
{ \
	if (theOneAndOnlyController->getStatsCollector().enabled()) { \
		theOneAndOnlyController->getStatsCollector().histogramObserve(__VA_ARGS__) ;\
	} \
}
#define STATS_HISTOGRAM_OBSERVE_NOCHECK(...) theOneAndOnlyController->getStatsCollector().histogramObserve(__VA_ARGS__) ;

#endif
