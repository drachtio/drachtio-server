/*
Copyright (c) 2024, FirstFive8, Inc

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
#ifndef __SIP_DIALOG_HPP__
#define __SIP_DIALOG_HPP__

#include <sys/time.h>
#include <chrono>
#include <iostream>
#include <set>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/identity.hpp>

#include <sofia-sip/nta.h>
#include <sofia-sip/nta_tport.h>

#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_util.h>
#include <sofia-sip/sip_status.h>


#include "timer-queue.hpp"

using namespace ::boost::multi_index;

namespace drachtio {

  struct DlgPtrTag{};
  struct DlgTimeTag{};
  struct DlgLegTag{};
  struct DialogIdTag{};
  struct DlgRoleTag{};

	class SipDialog : public std::enable_shared_from_this<SipDialog> {
	public:
		SipDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, msg_t *msg  ) ;
		SipDialog( const std::string& transactionId, nta_leg_t* leg, 
			nta_outgoing_t* orq, sip_t const *sip, msg_t *msg, const std::string& transport ) ;
		~SipDialog() ;

		bool operator <(const SipDialog& a) const { return m_tmArrival < a.m_tmArrival; }

    friend std::ostream& operator<<(std::ostream& os, const SipDialog& dlg);

		int processRequest( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip ) ;

		struct Endpoint_t {
			Endpoint_t() : m_signalingPort(0) {}

			std::string			m_strSignalingAddress ;
			unsigned int 	m_signalingPort ;
			std::string			m_strSdp ;
			std::string 			m_strContentType ;
			std::string			m_strTag ;
		} ;

		enum DialogType_t {
			we_are_uac = 0
			,we_are_uas
		} ;

		enum ReleaseCause_t {
			no_release = 0
			,calling_party_release 
			,called_party_release 
			,call_canceled
			,call_rejected
 			,call_rejected_locally_due_to_client_non_availability
			,unknown_release_cause
		} ;

		enum SessionRefresher_t {
			no_refresher = 0
			,we_are_refresher
			,they_are_refresher 
		} ;

		bool isInviteDialog(void) { return m_bInviteDialog; }

		const std::string& getCallId(void) const { return m_strCallId; }
		const Endpoint_t& getLocalEndpoint(void) const { return m_localEndpoint; }
		const Endpoint_t& getRemoteEndpoint(void) const { return m_remoteEndpoint; }
		unsigned int getSipStatus(void) const { return m_recentSipStatus; }
		time_t getStartTime(void) { return m_startTime; }
		time_t getConnectTime(void) { return m_connectTime; }
		time_t getEndTime(void) { return m_endTime; }

		void setSipStatus(unsigned int code) { m_recentSipStatus = code; }
		void setConnectTime(void) { m_connectTime = time(0) ;}
		void setEndTime(void) { m_endTime = time(0) ;}
		bool hasLocalTag(void) const { return !m_localEndpoint.m_strTag.empty(); }
		bool hasRemoteTag(void) const { return !m_remoteEndpoint.m_strTag.empty(); }
		void setLocalTag(const char* tag) { m_localEndpoint.m_strTag.assign( tag );}
		void setRemoteTag(const char* tag) { m_remoteEndpoint.m_strTag.assign( tag );}
		bool hasLocalSdp(void) const { return !m_localEndpoint.m_strSdp.empty(); }
		bool hasRemoteSdp(void) const { return !m_remoteEndpoint.m_strSdp.empty(); }
		void setLocalSdp(const char* sdp) { m_localEndpoint.m_strSdp.assign( sdp );}
		void setLocalSdp(const char* data, unsigned int len) { m_localEndpoint.m_strSdp.assign( data, len );}
		void setRemoteSdp(const char* sdp) { m_remoteEndpoint.m_strSdp.assign( sdp );}
		void setRemoteSdp(const char* data, unsigned int len) { m_remoteEndpoint.m_strSdp.assign( data, len );}
		void setRemoteContentType( std::string& type ) { m_remoteEndpoint.m_strContentType = type ; }
		void setLocalContentType( std::string& type ) { m_localEndpoint.m_strContentType = type ; }
		void setRemoteSignalingAddress( const char* szAddress ) { m_remoteEndpoint.m_strSignalingAddress = szAddress; }
		void setLocalSignalingAddress( const char* szAddress ) { m_localEndpoint.m_strSignalingAddress = szAddress; }
		void setRemoteSignalingPort( unsigned int port ) { m_remoteEndpoint.m_signalingPort = port; }
		void setLocalSignalingPort( unsigned int port ) { m_localEndpoint.m_signalingPort = port; }
		const std::string& getLocalSignalingAddress(void) { return m_localEndpoint.m_strSignalingAddress; }
		unsigned int getLocalSignalingPort(void) { return m_localEndpoint.m_signalingPort; }
		void setLocalContactHeader(const char* szContact) { m_strLocalContact = szContact;}
		const std::string& getLocalContactHeader(void) { return m_strLocalContact; }
		const std::string& getTransportAddress(void) const { return m_transportAddress; }
		const std::string& getTransportPort(void) const { return m_transportPort; }
		const std::string& getProtocol(void) const { return m_protocol; }
		void getTransportDesc(std::string desc) const { desc = m_protocol + "/" + m_transportAddress + ":" + m_transportPort; }

		const std::string& getSourceAddress(void) const { return m_sourceAddress;}
		unsigned int getSourcePort(void) const { return m_sourcePort; }
		void setSourceAddress( const std::string& host ) { m_sourceAddress = host; }
		void setSourcePort( unsigned int port ) { m_sourcePort = port; }
		const std::string& dialogId(void) const { return m_dialogId; }
		const std::string& getDialogId(void) { 
			if (m_dialogId.empty()) {
				m_dialogId = m_strCallId;
				m_dialogId.append(";from-tag=");
				m_dialogId.append( we_are_uac == m_type ? m_localEndpoint.m_strTag :m_remoteEndpoint.m_strTag);
			}
			return m_dialogId ;
		}
		const std::string& getTransactionId(void) const { return m_transactionId; }

		void setTransactionId(const std::string& strValue) { m_transactionId = strValue; }

		void setSessionTimer( unsigned long nSecs, SessionRefresher_t whoIsResponsible ) ;
		bool hasSessionTimer(void) { return NULL != m_timerSessionRefresh; }
		void cancelSessionTimer(void) ;
		void doSessionTimerHandling(void) ;
		DialogType_t getRole(void) const { return m_type; }
		bool areWeRefresher(void) { return we_are_refresher == m_refresher; }
		unsigned long getSessionExpiresSecs(void) { return m_nSessionExpiresSecs; }
		unsigned long getMinSE(void) { return m_nMinSE; }
		void setMinSE(unsigned long secs) { m_nMinSE = secs;}

		tport_t* getTport(void);
		void setTport(tport_t* tp) ;
		void setOrqAck(nta_outgoing_t* orq, bool destroyAckOnClose) { 
			m_orqAck = orq; 
			m_bDestroyAckOnClose = destroyAckOnClose;
		}

		const nta_leg_t* getNtaLeg(void) const { return m_leg; }
		void setTimerG(TimerEventHandle& handle) { 
			m_timerG = handle; 
			if( 0 == m_durationTimerG ) {
				m_durationTimerG = NTA_SIP_T1;
			}
		}
		TimerEventHandle getTimerG(void) { return m_timerG; }
		uint32_t bumpTimerG(void) {
			m_durationTimerG = std::min( m_durationTimerG << 1, (uint32_t) NTA_SIP_T2);
			return m_durationTimerG;
		}
		void clearTimerG() { m_timerG = NULL;}

		void setTimerH(TimerEventHandle& handle) { m_timerH = handle; }
		TimerEventHandle getTimerH(void) { return m_timerH; }
		void clearTimerH() { m_timerH = NULL;}

		void setRouteUri(std::string& routeUri) { m_routeUri = routeUri; }

		bool getRouteUri(std::string& routeUri) {
			if (!m_routeUri.empty()) {
				routeUri = m_routeUri;
				return true;
			}
			return false;
		}
		void clearRouteUri() { m_routeUri.clear(); }

		std::chrono::time_point<std::chrono::steady_clock>& getArrivalTime(void) {
			return m_timeArrive;
		}
		bool hasAlerted() const {
			return m_bAlerting;
		}
		void alerting(void) {
			m_bAlerting = true;
		}

		void doAckBye(void) { m_bAckBye = true; }
		bool isAckBye(void) { return m_bAckBye; }

		sip_time_t ageInSecs(void) {
			return sip_now() - m_tmArrival;
		}

		uint32_t getSeq(void) { return m_seq; }
		void clearSeq(void) {m_seq = 0;}
        
    void addIncomingRequestTransaction(std::string& txnId) {
        m_incomingRequestTransactionIds.insert(txnId);
    }
    void removeIncomingRequestTransaction(std::string& txnId) {
        m_incomingRequestTransactionIds.erase(txnId);
    }
    std::vector<std::string> getIncomingRequestTransactionIds(void) {
        return std::vector<std::string>(m_incomingRequestTransactionIds.begin(), m_incomingRequestTransactionIds.end());
    }

    // New getters for metric labels
    const std::string& getAccountSid() const { return m_accountSid; }
    const std::string& getApplicationSid() const { return m_applicationSid; }
		
	protected:

    void          checkTportState(void);

		bool 				m_bInviteDialog;

		std::string 		m_dialogId ;
		std::string 		m_transactionId ;
		DialogType_t		m_type ;
		Endpoint_t			m_localEndpoint ;
		Endpoint_t			m_remoteEndpoint ;
		std::string			m_strCallId ;
		uint32_t 				m_seq;
		unsigned int		m_recentSipStatus ;
		time_t					m_startTime ;
		time_t					m_connectTime ;
		time_t					m_endTime ;
		ReleaseCause_t	m_releaseCause ;

		std::string 			m_transportAddress ;
		std::string 			m_transportPort ;
		std::string 			m_transportProtocol ;

		std::string			m_strLocalContact;

    /* session timer */
    unsigned long 	m_nSessionExpiresSecs ;
    unsigned long 	m_nMinSE ;
    su_timer_t*     m_timerSessionRefresh ;
    SessionRefresher_t	m_refresher ;
    std::weak_ptr<SipDialog>* m_ppSelf ;

		std::string 			m_sourceAddress ;
		unsigned int 	m_sourcePort ;
		std::string      m_protocol ;

		nta_leg_t* 	m_leg; 
		tport_t* 	m_tp;
		nta_outgoing_t* m_orq;
		nta_outgoing_t* m_orqAck;
		bool 		m_bDestroyAckOnClose;

		std::string 		m_routeUri;

		// sip timers
    TimerEventHandle  m_timerG ;
    TimerEventHandle  m_timerH ;
    uint32_t					m_durationTimerG;
    uint32_t					m_countTimerG;

		//timing
		std::chrono::time_point<std::chrono::steady_clock> m_timeArrive;
		bool m_bAlerting;

		su_duration_t 		m_nSessionTimerDuration;

		// for race condition of sending CANCEL but getting 200 OK to INVITE
		bool 							m_bAckBye;

		// arrival time
		sip_time_t m_tmArrival;
        
    std::set<std::string> m_incomingRequestTransactionIds;

    // New members for storing account and application IDs for metrics
    std::string m_accountSid;
    std::string m_applicationSid;
	}  ;

  typedef multi_index_container<
    std::shared_ptr<SipDialog>,
    indexed_by<
      hashed_unique<
        boost::multi_index::tag<DlgPtrTag>,
        boost::multi_index::identity< std::shared_ptr<SipDialog> >
      >,
      ordered_non_unique<
        boost::multi_index::tag<DlgTimeTag>,
        boost::multi_index::identity<SipDialog> 
      >,
      hashed_unique<
        boost::multi_index::tag<DlgLegTag>,
        boost::multi_index::const_mem_fun<SipDialog, const nta_leg_t*, &SipDialog::getNtaLeg>
      >,
      hashed_unique<
        boost::multi_index::tag<DialogIdTag>,
        boost::multi_index::mem_fun<SipDialog, const std::string&, &SipDialog::getDialogId>
      >,
      hashed_non_unique<
        boost::multi_index::tag<DlgRoleTag>,
        boost::multi_index::const_mem_fun<SipDialog, SipDialog::DialogType_t, &SipDialog::getRole>
      >
    >
  > StableDialogs_t;

	void SD_Insert(StableDialogs_t& dialogs, std::shared_ptr<SipDialog>& dlg);

	bool SD_FindByLeg(const StableDialogs_t& dialogs, nta_leg_t* leg, std::shared_ptr<SipDialog>& dlg);
	bool SD_FindByDialogId(const StableDialogs_t& dialogs, const std::string& dialogId, std::shared_ptr<SipDialog>& dlg);
  void SD_Clear(StableDialogs_t& dialogs, std::shared_ptr<SipDialog>& dlg);
  void SD_Clear(StableDialogs_t& dialogs, const std::string& dialogId);
  void SD_Clear(StableDialogs_t& dialogs, nta_leg_t* nta);
  size_t SD_Size(const StableDialogs_t& dialogs);
  size_t SD_Size(const StableDialogs_t& dialogs, size_t& nUac, size_t& nUas);

  void SD_Log(const StableDialogs_t& dialogs, bool full = false);

}

#endif
