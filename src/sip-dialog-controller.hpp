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
#ifndef __SIP_DIALOG_CONTROLLER_HPP__
#define __SIP_DIALOG_CONTROLLER_HPP__

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <algorithm>
#include <vector>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/nta.h>

#include "sip-dialog.hpp"
#include "client-controller.hpp"
#include "timer-queue.hpp"
#include "timer-queue-manager.hpp"
#include "invite-in-progress.hpp"

#define START_LEN (512)
#define HDR_LEN (8400)
#define BODY_LEN (12288)

namespace drachtio {

	class DrachtioController ;

	/* requests in progress (sent by application, may be inside or outside a dialog) */
	class RIP {
	public:
        RIP( const string& transactionId ) ;
        RIP( const string& transactionId, const string& dialogId );
        RIP( const string& transactionId, const string& dialogId,  std::shared_ptr<SipDialog> dlg, bool clearDialogOnResponse = false ) ;

        ~RIP();

		const string& getTransactionId(void) { return m_transactionId; }
		const string& getDialogId(void) { return m_dialogId; }
		bool shouldClearDialogOnResponse(void) { return m_bClearDialogOnResponse;}

	private:
		string 												m_transactionId ;
		string												m_dialogId ;
		bool												m_bClearDialogOnResponse;
		std::shared_ptr<SipDialog> 	m_dlg ;
	} ;

	/**
	 * manages timer D: the timer that governs how long we keep around ACKs
	 * for UAC INVITEs to be able to respond to retransmitted final responses
	 */
	class TimerDHandler {
	public:
		TimerDHandler() {}
		~TimerDHandler() {}

		void setTimerQueueManager(std::shared_ptr<TimerQueueManager> pTQM) { 
			m_pTQM = pTQM;
		}
		void addInvite(nta_outgoing_t* invite);
		void addAck(nta_outgoing_t*	ack);
		bool resendIfNeeded(nta_outgoing_t* invite);
		bool clearTimerD(nta_outgoing_t* invite);
		size_t countTimerD() { return m_mapInvite2Ack.size();}
		size_t countPending() { return m_mapCallIdAndCSeq2Invite.size();}

	private:
		void timerD(nta_outgoing_t*	invite, const string& callIdAndCSeq);

		std::shared_ptr<TimerQueueManager> m_pTQM;
		typedef std::unordered_map<string, nta_outgoing_t*> mapCallIdAndCSeq2Invite;
		typedef std::unordered_map<nta_outgoing_t*, nta_outgoing_t*> mapInvite2Ack;
		
		mapCallIdAndCSeq2Invite	m_mapCallIdAndCSeq2Invite;
		mapInvite2Ack 					m_mapInvite2Ack;
	} ;

	class SipDialogController : public std::enable_shared_from_this<SipDialogController> {
	public:
		SipDialogController(DrachtioController* pController, su_clone_r* pClone );
		~SipDialogController() ;

		class SipMessageData {
		public:
			SipMessageData() {
				memset(m_szClientMsgId, 0, sizeof(m_szClientMsgId) ) ;
				memset(m_szTransactionId, 0, sizeof(m_szTransactionId) ) ;
				memset(m_szRequestId, 0, sizeof(m_szRequestId) ) ;
				memset(m_szDialogId, 0, sizeof(m_szDialogId) ) ;
				memset(m_szStartLine, 0, sizeof(m_szStartLine) ) ;
				memset(m_szHeaders, 0, sizeof(m_szHeaders) ) ;
				memset(m_szBody, 0, sizeof(m_szBody) ) ;
				memset(m_szRouteUrl, 0, sizeof(m_szRouteUrl) ) ;
			}
			SipMessageData(const string& clientMsgId, const string& transactionId, const string& requestId, const string& dialogId,
				const string& startLine, const string& headers, const string& body ) : SipMessageData() {
				memcpy( m_szClientMsgId, clientMsgId.c_str(), std::min(MSG_ID_LEN, (int) clientMsgId.length()) ) ;
				if( !transactionId.empty() ) memcpy( m_szTransactionId, transactionId.c_str(), std::min(MSG_ID_LEN, (int) transactionId.length())) ;
				if( !requestId.empty() ) memcpy( m_szRequestId, requestId.c_str(), std::min(MSG_ID_LEN, (int) requestId.length()));
				if( !dialogId.empty() )  memcpy( m_szDialogId, dialogId.c_str(), std::min(MAX_DIALOG_ID_LEN, (int) dialogId.length()));
				memcpy( m_szStartLine, startLine.c_str(), std::min(START_LEN, (int) startLine.length()));
				memcpy( m_szHeaders, headers.c_str(), std::min(HDR_LEN, (int) headers.length())) ;
				memcpy( m_szBody, body.c_str(), std::min(BODY_LEN, (int) body.length()));
			}
			SipMessageData(const string& clientMsgId, const string& transactionId, const string& requestId, const string& dialogId,
				const string& startLine, const string& headers, const string& body, const string& routeUrl )  : SipMessageData() {
				memcpy( m_szClientMsgId, clientMsgId.c_str(), std::min(MSG_ID_LEN, (int) clientMsgId.length())) ;
				if( !transactionId.empty() ) memcpy( m_szTransactionId, transactionId.c_str(), std::min(MSG_ID_LEN, (int) transactionId.length())) ;
				if( !requestId.empty() ) memcpy( m_szRequestId, requestId.c_str(), std::min(MSG_ID_LEN, (int) requestId.length())) ;
				if( !dialogId.empty() ) memcpy( m_szDialogId, dialogId.c_str(), std::min(MSG_ID_LEN, (int) dialogId.length()) ) ;
				memcpy( m_szStartLine, startLine.c_str(), std::min(START_LEN, (int) startLine.length()) ) ;
				memcpy( m_szHeaders, headers.c_str(), std::min(HDR_LEN, (int) headers.length()) ) ;
				memcpy( m_szBody, body.c_str(), std::min(BODY_LEN, (int) body.length()) ) ;
				memcpy( m_szRouteUrl, routeUrl.c_str(), std::min(START_LEN, (int) routeUrl.length()) ) ;
			}
			~SipMessageData() {}
			SipMessageData& operator=(const SipMessageData& md) {
				strncpy( m_szClientMsgId, md.m_szClientMsgId, MSG_ID_LEN) ;
				strncpy( m_szTransactionId, md.m_szTransactionId, MSG_ID_LEN) ;
				strncpy( m_szDialogId, md.m_szDialogId, MSG_ID_LEN) ;
				strncpy( m_szRequestId, md.m_szRequestId, MSG_ID_LEN) ;
				strncpy( m_szStartLine, md.m_szStartLine, START_LEN ) ;
				strncpy( m_szHeaders, md.m_szHeaders, HDR_LEN ) ;
				strncpy( m_szBody, md.m_szBody, BODY_LEN ) ;
				strncpy( m_szRouteUrl, md.m_szRouteUrl, START_LEN ) ;
				return *this ;
			}

			const char* getClientMsgId() { return m_szClientMsgId; } 
			const char* getTransactionId() { return m_szTransactionId; } 
			const char* getDialogId() { return m_szDialogId; } 
			const char* getRequestId() { return m_szRequestId; } 
			const char* getHeaders() { return m_szHeaders; } 
			const char* getStartLine() { return m_szStartLine; } 
			const char* getBody() { return m_szBody; } 
			const char* getRouteUrl() { return m_szRouteUrl; } 

		private:
			char	m_szClientMsgId[MSG_ID_LEN+1];
			char	m_szTransactionId[MSG_ID_LEN+1];
			char	m_szRequestId[MSG_ID_LEN+1];
			char	m_szDialogId[MAX_DIALOG_ID_LEN+1];
			char	m_szStartLine[START_LEN+1];
			char	m_szHeaders[HDR_LEN+1];
			char	m_szBody[BODY_LEN+1];
			char	m_szRouteUrl[START_LEN+1];
		} ;

		//NB: sendXXXX are called when client is sending a message
		bool sendRequestInsideDialog( const string& clientMsgId, const string& dialogId, const string& startLine, const string& headers, const string& body, string& transactionId ) ;
		bool sendRequestOutsideDialog( const string& clientMsgId, const string& startLine, const string& headers, const string& body, string& transactionId, string& dialogId, string& routeUrl ) ;
    bool respondToSipRequest( const string& msgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) ;		
		bool sendCancelRequest( const string& msgId, const string& transactionId, const string& startLine, const string& headers, const string& body ) ;

		//NB: doSendXXX correspond to the above, and are run in the stack thread
		void doSendRequestInsideDialog( SipMessageData* pData ) ;
		void doSendRequestOutsideDialog( SipMessageData* pData ) ;
        void doRespondToSipRequest( SipMessageData* pData ) ;	
		void doSendCancelRequest( SipMessageData* pData ) ;

		//NB: processXXX are called when an incoming message is received from the network
    int processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) ;
    int processResponseOutsideDialog( nta_outgoing_t* request, sip_t const* sip )  ;
    int processResponseInsideDialog( nta_outgoing_t* request, sip_t const* sip ) ;
		int processResponseToRefreshingReinvite( nta_outgoing_t* request, sip_t const* sip ) ;
    int processCancelOrAck( nta_incoming_magic_t* p, nta_incoming_t* irq, sip_t const *sip ) ;
    int processPrack( nta_reliable_t *rel, nta_incoming_t *prack, sip_t const *sip) ;

    void notifyRefreshDialog( std::shared_ptr<SipDialog> dlg ) ;
    void notifyTerminateStaleDialog( std::shared_ptr<SipDialog> dlg, bool ackbye = false ) ;

    void notifyCancelTimeoutReachedIIP( std::shared_ptr<IIP> dlg ) ;

		void logStorageCount(bool bDetail = false)  ;

		/// IIP helpers 
		void addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, const string& transactionId, std::shared_ptr<SipDialog> dlg, const string& tag ) ;
		void addOutgoingInviteTransaction( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip, std::shared_ptr<SipDialog> dlg ) ;


		/// Dialog helpers
		void addDialog( std::shared_ptr<SipDialog> dlg ) {
			const string strDialogId = dlg->getDialogId() ;
			nta_leg_t *leg = nta_leg_by_call_id( m_agent, dlg->getCallId().c_str() );
			assert( leg ) ;

			SD_Insert(m_dialogs, dlg);
      m_pClientController->addDialogForTransaction( dlg->getTransactionId(), strDialogId ) ;		
		}
		bool findDialogByLeg( nta_leg_t* leg, std::shared_ptr<SipDialog>& dlg ) {
			/* look in invites-in-progress first */
			std::shared_ptr<IIP> iip;
			if (!IIP_FindByLeg(m_invitesInProgress, leg, iip)) {

				/* if not found, look in stable dialogs */
				return SD_FindByLeg(m_dialogs, leg, dlg);
			}
			dlg = iip->dlg() ;
			return true ;
		}
		bool findDialogByCallId( const string& strCallId, std::shared_ptr<SipDialog>& dlg ) {
			string strDialogId = strCallId + ";uas";
			if (!SD_FindByDialogId(m_dialogs, strDialogId, dlg)) {
				strDialogId = strCallId + ";uac";
				return SD_FindByDialogId(m_dialogs, strDialogId, dlg);
			}
			return true;
		}

		/// RIP helpers
		void addRIP( nta_outgoing_t* orq, std::shared_ptr<RIP> rip) ;
		bool findRIPByOrq( nta_outgoing_t* orq, std::shared_ptr<RIP>& rip ) ;
        void clearRIP( nta_outgoing_t* orq ) ;
        void clearRIPByDialogId( const std::string dialogId) ;

		/// IRQ helpers
		void addIncomingRequestTransaction( nta_incoming_t* irq, const string& transactionId) ;
		bool findIrqByTransactionId( const string& transactionId, nta_incoming_t*& irq ) ;
		nta_incoming_t* findAndRemoveTransactionIdForIncomingRequest( const string& transactionId ) ;

		// retransmit final response to invite
		void retransmitFinalResponse(nta_incoming_t* irq, tport_t* tp, std::shared_ptr<SipDialog> dlg);
		void endRetransmitFinalResponse(nta_incoming_t* irq, tport_t* tp, std::shared_ptr<SipDialog> dlg);

		// timers
		void clearSipTimers(std::shared_ptr<SipDialog>& dlg);
		bool stopTimerD(nta_outgoing_t* invite);
        
        void clearDanglingIncomingRequests(std::vector<std::string> txnIds);

	protected:
 		bool searchForHeader( tagi_t* tags, tag_type_t header, string& value ) ;
		void bindIrq( nta_incoming_t* irq ) ;


	private:
		DrachtioController* m_pController ;
		su_clone_r*			m_pClone ;

		/* since access to the various maps below can be triggered either by arriva or network message, or client message - 
			each in a different thread - we use this mutex to protect them.  To keep things straight, the mutex lock operations
			are utilized in the low-level addXX, findXX, and clearXX methods that appear in this header file.  There should be
			NO direct access to the maps nor use of the mutex in the .cpp (the exception being the method to log storage counts)
		*/
   	std::mutex 		m_mutex ;

		nta_agent_t*		m_agent ;
		std::shared_ptr< ClientController > m_pClientController ;

		TimerDHandler 	m_timerDHandler;
 
		InvitesInProgress_t  	m_invitesInProgress;
		StableDialogs_t				m_dialogs;

		// Requests sent by client

		/* we need to lookup responses to requests sent by the client inside a dialog */
		typedef std::unordered_map<nta_outgoing_t*, std::shared_ptr<RIP> > mapOrq2RIP ;
		mapOrq2RIP m_mapOrq2RIP ;
        void logRIP(bool detail);
        
		// Requests received from the network

		/* we need to lookup incoming transactions by transaction id when we get a response from the client */
		typedef std::unordered_map<string, nta_incoming_t*> mapTransactionId2Irq ;
		mapTransactionId2Irq m_mapTransactionId2Irq ;


		// timers for dialogs and leg that we can remove after suitable timeout period waiting for retransmissions
    std::shared_ptr<TimerQueueManager> m_pTQM ;
	} ;

}

#endif
