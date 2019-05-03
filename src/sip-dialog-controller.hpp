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

#define START_LEN (512)
#define HDR_LEN (4192)
#define BODY_LEN (8384)

namespace drachtio {

	class DrachtioController ;

	/* invites in progress */
	class IIP {
	public:
		IIP( nta_leg_t* leg, nta_incoming_t* irq, const string& transactionId, std::shared_ptr<SipDialog> dlg ) : 
			m_leg(leg), m_irq(irq), m_orq(NULL), m_strTransactionId(transactionId), m_dlg(dlg),m_role(uas_role),m_rel(NULL) {}

		IIP( nta_leg_t* leg, nta_outgoing_t* orq, const string& transactionId, std::shared_ptr<SipDialog> dlg ) : 
			m_leg(leg), m_irq(NULL), m_orq(orq), m_strTransactionId(transactionId), m_dlg(dlg),m_role(uac_role),m_rel(NULL) {}

		~IIP() {
		}

		nta_leg_t* leg(void) { return m_leg; }
		nta_incoming_t* irq(void) { return m_irq; }
		nta_outgoing_t* orq(void) { return m_orq; }
		nta_reliable_t* rel(void) { return m_rel; }
		void setReliable(nta_reliable_t* rel) { m_rel = rel; }
		void destroyReliable(void) {
			if( m_rel ) {
				nta_reliable_destroy( m_rel ) ;
				m_rel = NULL ;
			}
		}
		const string& getTransactionId(void) { return m_strTransactionId; }
		std::shared_ptr<SipDialog> dlg(void) { return m_dlg; }

	private:
		string 			m_strTransactionId ;
		nta_leg_t* 		m_leg ;
		nta_incoming_t*	m_irq ;
		nta_outgoing_t*	m_orq ;
		nta_reliable_t*	m_rel ;
		std::shared_ptr<SipDialog> 	m_dlg ;
		agent_role		m_role ;
	} ;

	/* requests in progress (sent by application, may be inside or outside a dialog) */
	class RIP {
	public:
		RIP( const string& transactionId ) : 
			m_transactionId(transactionId) {}
		RIP( const string& transactionId, const string& dialogId ) : 
			m_transactionId(transactionId), m_dialogId(dialogId) {}
		RIP( const string& transactionId, const string& dialogId,  std::shared_ptr<SipDialog> dlg, bool clearDialogOnResponse = false ) : 
			m_transactionId(transactionId), m_dialogId(dialogId), m_dlg(dlg), m_bClearDialogOnResponse(clearDialogOnResponse) {
			}

		~RIP() {}

		const string& getTransactionId(void) { return m_transactionId; }
		const string& getDialogId(void) { return m_dialogId; }
		bool shouldClearDialogOnResponse(void) { return m_bClearDialogOnResponse;}

	private:
		string 												m_transactionId ;
		string												m_dialogId ;
		bool													m_bClearDialogOnResponse;
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
				const string& startLine, const string& headers, const string& body ) {
				strncpy( m_szClientMsgId, clientMsgId.c_str(), MSG_ID_LEN ) ;
				if( !transactionId.empty() ) strncpy( m_szTransactionId, transactionId.c_str(), MSG_ID_LEN ) ;
				if( !requestId.empty() ) strncpy( m_szRequestId, requestId.c_str(), MSG_ID_LEN ) ;
				if( !dialogId.empty() ) strncpy( m_szDialogId, dialogId.c_str(), MSG_ID_LEN ) ;
				strncpy( m_szStartLine, startLine.c_str(), START_LEN ) ;
				strncpy( m_szHeaders, headers.c_str(), HDR_LEN ) ;
				strncpy( m_szBody, body.c_str(), BODY_LEN ) ;
			}
			SipMessageData(const string& clientMsgId, const string& transactionId, const string& requestId, const string& dialogId,
				const string& startLine, const string& headers, const string& body, const string& routeUrl ) {
				strncpy( m_szClientMsgId, clientMsgId.c_str(), MSG_ID_LEN ) ;
				if( !transactionId.empty() ) strncpy( m_szTransactionId, transactionId.c_str(), MSG_ID_LEN ) ;
				if( !requestId.empty() ) strncpy( m_szRequestId, requestId.c_str(), MSG_ID_LEN ) ;
				if( !dialogId.empty() ) strncpy( m_szDialogId, dialogId.c_str(), MSG_ID_LEN ) ;
				strncpy( m_szStartLine, startLine.c_str(), START_LEN ) ;
				strncpy( m_szHeaders, headers.c_str(), HDR_LEN ) ;
				strncpy( m_szBody, body.c_str(), BODY_LEN ) ;
				strncpy( m_szRouteUrl, routeUrl.c_str(), START_LEN ) ;
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
			char	m_szClientMsgId[MSG_ID_LEN];
			char	m_szTransactionId[MSG_ID_LEN];
			char	m_szRequestId[MSG_ID_LEN];
			char	m_szDialogId[MSG_ID_LEN];
			char	m_szStartLine[START_LEN];
			char	m_szHeaders[HDR_LEN];
			char	m_szBody[BODY_LEN];
			char	m_szRouteUrl[START_LEN];
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
    void notifyTerminateStaleDialog( std::shared_ptr<SipDialog> dlg ) ;

    bool isManagingTransaction( const string& transactionId ) {
    	return m_mapTransactionId2IIP.end() != m_mapTransactionId2IIP.find( transactionId ) ;
    }

		void logStorageCount(bool bDetail = false)  ;

		/// IIP helpers 
		void addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, const string& transactionId, std::shared_ptr<SipDialog> dlg ) ;
		void addOutgoingInviteTransaction( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip, std::shared_ptr<SipDialog> dlg ) ;

		void addIncomingPrackTransaction( const string& transactionId, std::shared_ptr<IIP> p ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
			m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(transactionId, p) ) ;   			
		}
		void addReliable( nta_reliable_t* rel, std::shared_ptr<IIP>& iip) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
			m_mapRel2IIP.insert( mapRel2IIP::value_type(rel, iip) ) ;
		}
		bool findIIPByIrq( nta_incoming_t* irq, std::shared_ptr<IIP>& iip ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
	        mapIrq2IIP::iterator it = m_mapIrq2IIP.find( irq ) ;
	        if( m_mapIrq2IIP.end() == it ) return false ;
	        iip = it->second ;
	        return true ;			
		}
		bool findIIPByOrq( nta_outgoing_t* orq, std::shared_ptr<IIP>& iip ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
	        mapOrq2IIP::iterator it = m_mapOrq2IIP.find( orq ) ;
	        if( m_mapOrq2IIP.end() == it ) return false ;
	        iip = it->second ;
	        return true ;						
		}
		bool findIIPByLeg( nta_leg_t* leg, std::shared_ptr<IIP>& iip ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
			mapLeg2IIP::iterator it = m_mapLeg2IIP.find( leg ) ;
			if( m_mapLeg2IIP.end() == it ) return false ;
			iip = it->second ;
			return true ;			
		}
		bool findIIPByTransactionId( const string& transactionId, std::shared_ptr<IIP>& iip ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
			mapTransactionId2IIP::iterator it = m_mapTransactionId2IIP.find( transactionId ) ;
			if( m_mapTransactionId2IIP.end() == it ) return false ;
			iip = it->second ;
			return true ;			
		}
		bool findIIPByReliable( nta_reliable_t* rel, std::shared_ptr<IIP>& iip ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
			mapRel2IIP::iterator it = m_mapRel2IIP.find( rel ) ;
			if( m_mapRel2IIP.end() == it ) return false ;
			iip = it->second.lock() ;
			if( !iip ) {
				m_mapRel2IIP.erase(it) ;
				return false ;
			}
			return true ;		
		}

		/// Dialog helpers
		void addDialog( std::shared_ptr<SipDialog> dlg ) {
			const string strDialogId = dlg->getDialogId() ;
			nta_leg_t *leg = nta_leg_by_call_id( m_agent, dlg->getCallId().c_str() );
			assert( leg ) ;

			std::lock_guard<std::mutex> lock(m_mutex) ;
      m_mapLeg2Dialog.insert( mapLeg2Dialog::value_type(leg, dlg)) ;	
      m_mapId2Dialog.insert( mapId2Dialog::value_type(strDialogId, dlg)) ;

      m_pClientController->addDialogForTransaction( dlg->getTransactionId(), strDialogId ) ;		
		}
		bool findDialogByLeg( nta_leg_t* leg, std::shared_ptr<SipDialog>& dlg ) {
			/* look in invites-in-progress first */
			std::lock_guard<std::mutex> lock(m_mutex) ;
			mapLeg2IIP::iterator it = m_mapLeg2IIP.find( leg ) ;
			if( m_mapLeg2IIP.end() == it ) {

				/* if not found, look in stable dialogs */
				mapLeg2Dialog::iterator itLeg = m_mapLeg2Dialog.find( leg ) ;
				if( m_mapLeg2Dialog.end() == itLeg ) return false ;
				dlg = itLeg->second ;
				return true ;
			}
			dlg = it->second->dlg() ;
			return true ;
		}
		bool findDialogById(  const string& strDialogId, std::shared_ptr<SipDialog>& dlg ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
			mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
			if( m_mapId2Dialog.end() == it ) return false ;
			dlg = it->second ;
			return true ;
		}
		bool findDialogByCallId( const string& strCallId, std::shared_ptr<SipDialog>& dlg ) {
			std::lock_guard<std::mutex> lock(m_mutex) ;
			mapId2Dialog::iterator it = m_mapId2Dialog.find( strCallId + ";uas" ) ;
			if( m_mapId2Dialog.end() == it ) it = m_mapId2Dialog.find( strCallId + ";uac" ) ;
			if( m_mapId2Dialog.end() == it ) {
				return false ;
			}
			dlg = it->second ;
			return true ;
		}

		/// RIP helpers
		void addRIP( nta_outgoing_t* orq, std::shared_ptr<RIP> rip) ;
		bool findRIPByOrq( nta_outgoing_t* orq, std::shared_ptr<RIP>& rip ) ;
		void clearRIP( nta_outgoing_t* orq ) ;

		/// IRQ helpers
		void addIncomingRequestTransaction( nta_incoming_t* irq, const string& transactionId) ;
		bool findIrqByTransactionId( const string& transactionId, nta_incoming_t*& irq ) ;
		nta_incoming_t* findAndRemoveTransactionIdForIncomingRequest( const string& transactionId ) ;

		// retransmit final response to invite
		void retransmitFinalResponse(nta_incoming_t* irq, tport_t* tp, std::shared_ptr<SipDialog> dlg);
		void endRetransmitFinalResponse(nta_incoming_t* irq, tport_t* tp, std::shared_ptr<SipDialog> dlg);

		// timers
		void clearSipTimers(std::shared_ptr<SipDialog>& dlg);

	protected:
		std::shared_ptr<SipDialog> clearIIP( nta_leg_t* leg ) ;
		void clearDialog( const string& strDialogId ) ;
		void clearDialog( nta_leg_t* leg ) ;
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
 
 		/// INVITEs in progress

 		/* we need to lookup invites in progress that we've received by nta_incoming_t* when we get a CANCEL from the network */
    typedef std::unordered_map<nta_incoming_t*, std::shared_ptr<IIP> > mapIrq2IIP ;
    mapIrq2IIP m_mapIrq2IIP ;

		/* we need to lookup invites in progress that we've generated by nta_outgoing_t* when we get a response from the network */
    typedef std::unordered_map<nta_outgoing_t*, std::shared_ptr<IIP> > mapOrq2IIP ;
    mapOrq2IIP m_mapOrq2IIP ;

 		/* we need to lookup invites in progress by transactionId when we get a request from a client to respond to the invite (for uas dialogs)
			or when we get a CANCEL from a client (for uac dialogs)
 		*/
   typedef std::unordered_map<string, std::shared_ptr<IIP> > mapTransactionId2IIP ;
    mapTransactionId2IIP m_mapTransactionId2IIP ;

		/* we need to lookup invites in progress by leg when we get an ACK from the network */
		typedef std::unordered_map<nta_leg_t*, std::shared_ptr<IIP> > mapLeg2IIP ;
		mapLeg2IIP m_mapLeg2IIP ;

		/* we need to lookup invites in progress by nta_reliable_t when we get an PRACK from the network */
		typedef std::unordered_map<nta_reliable_t*, std::weak_ptr<IIP> > mapRel2IIP ;
		mapRel2IIP m_mapRel2IIP ;


      /// Stable Dialogs 

		/* we need to lookup dialogs by leg when we get a request from the network */
		typedef std::unordered_map<nta_leg_t*, std::shared_ptr<SipDialog> > mapLeg2Dialog ;
		mapLeg2Dialog m_mapLeg2Dialog ;

		/* we need to lookup dialogs by dialog id when we get a request from the client  */
		typedef std::unordered_map<string, std::shared_ptr<SipDialog> > mapId2Dialog ;
		mapId2Dialog m_mapId2Dialog ;

		/// Requests sent by client

		/* we need to lookup responses to requests sent by the client inside a dialog */
		typedef std::unordered_map<nta_outgoing_t*, std::shared_ptr<RIP> > mapOrq2RIP ;
		mapOrq2RIP m_mapOrq2RIP ;

		/// Requests received from the network 

		/* we need to lookup incoming transactions by transaction id when we get a response from the client */
		typedef std::unordered_map<string, nta_incoming_t*> mapTransactionId2Irq ;
		mapTransactionId2Irq m_mapTransactionId2Irq ;


		// timers for dialogs and leg that we can remove after suitable timeout period waiting for retransmissions
    std::shared_ptr<TimerQueueManager> m_pTQM ;
	} ;

}

#endif
