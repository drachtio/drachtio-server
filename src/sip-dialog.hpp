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
#ifndef __SIP_DIALOG_HPP__
#define __SIP_DIALOG_HPP__

#include <sys/time.h>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <sofia-sip/nta.h>
#include <sofia-sip/nta_tport.h>

#include "timer-queue.hpp"

using namespace std ;

namespace drachtio {

	class SipDialog : public boost::enable_shared_from_this<SipDialog> {
	public:
		SipDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, msg_t *msg  ) ;
		SipDialog( const string& dialogId, const string& transactionId, nta_leg_t* leg, 
			nta_outgoing_t* orq, sip_t const *sip, msg_t *msg, const string& transport ) ;
		~SipDialog() ;

		int processRequest( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip ) ;

		struct Endpoint_t {
			Endpoint_t() : m_signalingPort(0) {}

			string			m_strSignalingAddress ;
			unsigned int 	m_signalingPort ;
			string			m_strSdp ;
			string 			m_strContentType ;
			string			m_strTag ;
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

		const string& getCallId(void) const { return m_strCallId; }
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
		void setRemoteContentType( string& type ) { m_remoteEndpoint.m_strContentType = type ; }
		void setLocalContentType( string& type ) { m_localEndpoint.m_strContentType = type ; }
		void setRemoteSignalingAddress( const char* szAddress ) { m_remoteEndpoint.m_strSignalingAddress = szAddress; }
		void setLocalSignalingAddress( const char* szAddress ) { m_localEndpoint.m_strSignalingAddress = szAddress; }
		void setRemoteSignalingPort( unsigned int port ) { m_remoteEndpoint.m_signalingPort = port; }
		void setLocalSignalingPort( unsigned int port ) { m_localEndpoint.m_signalingPort = port; }
		const string& getLocalSignalingAddress(void) { return m_localEndpoint.m_strSignalingAddress; }
		unsigned int getLocalSignalingPort(void) { return m_localEndpoint.m_signalingPort; }

		const string& getTransportAddress(void) const { return m_transportAddress; }
		const string& getTransportPort(void) const { return m_transportPort; }
		const string& getProtocol(void) const { return m_protocol; }
		void getTransportDesc(string desc) const { desc = m_protocol + "/" + m_transportAddress + ":" + m_transportPort; }

		const string& getSourceAddress(void) const { return m_sourceAddress;}
		unsigned int getSourcePort(void) const { return m_sourcePort; }
		void setSourceAddress( const string& host ) { m_sourceAddress = host; }
		void setSourcePort( unsigned int port ) { m_sourcePort = port; }

		const string& getDialogId(void) { 
			if( m_dialogId.empty() ) {
				m_dialogId = m_strCallId;
				m_dialogId.append( we_are_uac == m_type ? ";uac" : ";uas");
			}
			return m_dialogId ;
		}
		const string& getTransactionId(void) const { return m_transactionId; }

		void setTransactionId(const string& strValue) { m_transactionId = strValue; }

		void setSessionTimer( unsigned long nSecs, SessionRefresher_t whoIsResponsible ) ;
		bool hasSessionTimer(void) { return NULL != m_timerSessionRefresh; }
		void cancelSessionTimer(void) ;
		void doSessionTimerHandling(void) ;
		DialogType_t getRole(void) { return m_type; }
		bool areWeRefresher(void) { return we_are_refresher == m_refresher; }
		unsigned long getSessionExpiresSecs(void) { return m_nSessionExpiresSecs; }
		unsigned long getMinSE(void) { return m_nMinSE; }
		void setMinSE(unsigned long secs) { m_nMinSE = secs;}

		bool hasAckBeenSent(void) { return m_bAckSent;}
		void ackSent(nta_outgoing_t* ack = NULL) { m_bAckSent = true; if (ack) { m_ackOrq = ack; }}
		void retransmitAck(void) ;
		tport_t* getTport(void) { return m_tp ;}
		void setTport(tport_t* tp) ;

		nta_leg_t* getNtaLeg(void) { return m_leg; }

		void setTimerD(TimerEventHandle& handle) { m_timerD = handle; }
		TimerEventHandle getTimerD(void) { return m_timerD; }
		void clearTimerD() { m_timerD = NULL;}

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

	protected:
		bool 				m_bInviteDialog;

		string 			m_dialogId ;
		string 			m_transactionId ;
		DialogType_t	m_type ;
		Endpoint_t		m_localEndpoint ;
		Endpoint_t		m_remoteEndpoint ;
		string			m_strCallId ;
		unsigned int	m_recentSipStatus ;
		time_t			m_startTime ;
		time_t			m_connectTime ;
		time_t			m_endTime ;
		ReleaseCause_t	m_releaseCause ;

		string 			m_transportAddress ;
		string 			m_transportPort ;
		string 			m_transportProtocol ;

    /* session timer */
    unsigned long 	m_nSessionExpiresSecs ;
    unsigned long 	m_nMinSE ;
    su_timer_t*     m_timerSessionRefresh ;
    SessionRefresher_t	m_refresher ;
    boost::weak_ptr<SipDialog>* m_ppSelf ;

		string 			m_sourceAddress ;
		unsigned int 	m_sourcePort ;
		string      m_protocol ;

		/* ACK is automatically sent except in case of delayed SDP offer, so we need to track */
		bool			m_bAckSent ;

		nta_leg_t* 	m_leg; 
		tport_t* 	m_tp ;
		nta_outgoing_t*  m_ackOrq;

		// sip timers
    TimerEventHandle  m_timerD ;
    TimerEventHandle  m_timerG ;
    TimerEventHandle  m_timerH ;
    uint32_t					m_durationTimerG;
    uint32_t					m_countTimerG;
	}  ;

}

#endif
