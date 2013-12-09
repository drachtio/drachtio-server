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

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/nta.h>

#include "sip-dialog.hpp"
#include "json-msg.hpp"

#define MSG_ID_LEN (64)

namespace drachtio {

	class DrachtioController ;

	/* invites in progress */
	class IIP {
	public:
		IIP( nta_leg_t* leg, nta_incoming_t* irq, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) : 
			m_leg(leg), m_irq(irq), m_strTransactionId(transactionId), m_dlg(dlg),m_role(uas_role) {}

		IIP( nta_leg_t* leg, nta_outgoing_t* orq, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) : 
			m_leg(leg), m_orq(orq), m_strTransactionId(transactionId), m_dlg(dlg),m_role(uac_role) {}

		~IIP() {}

		nta_leg_t* leg(void) { return m_leg; }
		nta_incoming_t* irq(void) { return m_irq; }
		nta_outgoing_t* orq(void) { return m_orq; }
		const string& transactionId(void) { return m_strTransactionId; }
		boost::shared_ptr<SipDialog> dlg(void) { return m_dlg; }

	private:
		string 			m_strTransactionId ;
		nta_leg_t* 		m_leg ;
		nta_incoming_t*	m_irq ;
		nta_outgoing_t*	m_orq ;
		boost::shared_ptr<SipDialog> 	m_dlg ;
		agent_role		m_role ;
	} ;

	/* requests in progress (inside a dialog ) */
	class RIP {
	public:
		RIP( const string& transactionId, const string& rid ) : 
			m_transactionId(transactionId), m_rid(rid) {}

		~RIP() {}

		const string& transactionId(void) { return m_transactionId; }
		const string& rid(void) { return m_rid; }

	private:
		string 			m_transactionId ;
		string 			m_rid ;
	} ;


	class SipDialogController {
	public:
		SipDialogController(DrachtioController* pController, su_clone_r* pClone );
		~SipDialogController() ;

		class SipMessageData {
		public:
			SipMessageData() {
				memset(m_szTransactionId, 0, sizeof(m_szTransactionId) ) ;
				memset(m_szRequestId, 0, sizeof(m_szRequestId) ) ;
				memset(m_szDialogId, 0, sizeof(m_szDialogId) ) ;
			}
			SipMessageData(const string& dialogId, const string& transactionId, const string& requestId, boost::shared_ptr<JsonMsg> pMsg ) : m_pMsg(pMsg) {
				strncpy( m_szTransactionId, transactionId.c_str(), MSG_ID_LEN ) ;
				strncpy( m_szRequestId, requestId.c_str(), MSG_ID_LEN ) ;
				strncpy( m_szDialogId, dialogId.c_str(), MSG_ID_LEN ) ;
			}
			SipMessageData(const string& transactionId, const string& requestId, boost::shared_ptr<JsonMsg> pMsg ) : m_pMsg(pMsg) {
				strncpy( m_szTransactionId, transactionId.c_str(), MSG_ID_LEN ) ;
				strncpy( m_szRequestId, requestId.c_str(), MSG_ID_LEN ) ;
			}
			~SipMessageData() {}
			SipMessageData& operator=(const SipMessageData& md) {
				m_pMsg = md.m_pMsg ;
				strncpy( m_szTransactionId, md.m_szTransactionId, MSG_ID_LEN) ;
				strncpy( m_szRequestId, md.m_szRequestId, MSG_ID_LEN) ;
				return *this ;
			}

			const char* getDialogId() { return m_szDialogId; } 
			const char* getTransactionId() { return m_szTransactionId; } 
			const char* getRequestId() { return m_szRequestId; } 
			boost::shared_ptr<JsonMsg> getMsg(void) { return m_pMsg; }

		private:
			char	m_szTransactionId[MSG_ID_LEN];
			char	m_szRequestId[MSG_ID_LEN];
			char	m_szDialogId[MSG_ID_LEN];
			boost::shared_ptr<JsonMsg> m_pMsg ;
		} ;

		void addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) ;
		void addOutgoingInviteTransaction( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) ;

        void respondToSipRequest( const string& transactionId, boost::shared_ptr<JsonMsg> pMsg ) ;		//called from worker thread, posts message into main thread
        void doRespondToSipRequest( SipMessageData* pData ) ;	//does the actual sip messaging, within main thread

		bool sendRequestOutsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& transactionId ) ;
		void doSendRequestOutsideDialog( SipMessageData* pData ) ;

        int processRequestInsideDialog( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip) ;

        int processResponseOutsideDialog( nta_outgoing_t* request, sip_t const* sip )  ;
        int processResponseInsideDialog( nta_outgoing_t* request, sip_t const* sip ) ;
        int processCancel( nta_incoming_t* irq, sip_t const *sip ) ;

	    bool isManagingTransaction( const string& transactionId ) {
	    	return m_mapTransactionId2IIP.end() != m_mapTransactionId2IIP.find( transactionId ) ;
	    }

		int sendRequestInsideDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid, boost::shared_ptr<SipDialog>& dlg ) ;
		void doSendRequestInsideDialog( SipMessageData* pData ) ;

		void addDialog( boost::shared_ptr<SipDialog> dlg ) ;

		bool findDialogByLeg( nta_leg_t* leg, boost::shared_ptr<SipDialog>& dlg ) ;
		bool findDialogById(  const string& strDialogId, boost::shared_ptr<SipDialog>& dlg ) ;
		bool findIIPByIrq( nta_incoming_t* irq, boost::shared_ptr<IIP>& iip ) ;

	protected:
		boost::shared_ptr<SipDialog> clearIIP( nta_leg_t* leg ) ;
		void clearDialog( const string& strDialogId ) ;
		void clearDialog( nta_leg_t* leg ) ;

		tagi_t* makeTags( json_spirit::mObject&  hdrs ) ;

	private:
		DrachtioController* m_pController ;
		su_clone_r*			m_pClone ;
 
 		/* we need to lookup invites in progress that we've received by nta_incoming_t* when we get a CANCEL from the network */
        typedef boost::unordered_map<nta_incoming_t*, boost::shared_ptr<IIP> > mapIrq2IIP ;
        mapIrq2IIP m_mapIrq2IIP ;

		/* we need to lookup invites in progress that we've generated by nta_outgoing_t* when we get a response from the network */
        typedef boost::unordered_map<nta_outgoing_t*, boost::shared_ptr<IIP> > mapOrq2IIP ;
        mapOrq2IIP m_mapOrq2IIP ;

 		/* we need to lookup invites in progress by transactionId when we get a request from a client to respond to the invite (for uas dialogs)
			or when we get a CANCEL from a client (for uac dialogs)
 		*/
       typedef boost::unordered_map<string, boost::shared_ptr<IIP> > mapTransactionId2IIP ;
        mapTransactionId2IIP m_mapTransactionId2IIP ;

 		/* we need to lookup invites in progress by leg when we get an ACK from the network */
       typedef boost::unordered_map<nta_leg_t*, boost::shared_ptr<IIP> > mapLeg2IIP ;
        mapLeg2IIP m_mapLeg2IIP ;

  		/* we need to lookup dialogs by leg when we get a request from the network */
       typedef boost::unordered_map<nta_leg_t*, boost::shared_ptr<SipDialog> > mapLeg2Dialog ;
        mapLeg2Dialog m_mapLeg2Dialog ;

 		/* we need to lookup dialogs by dialog  idwhen we get a request from the client  */
       typedef boost::unordered_map<string, boost::shared_ptr<SipDialog> > mapId2Dialog ;
        mapId2Dialog m_mapId2Dialog ;

        /* we need to lookup responses to requests sent by the client inside a dialog */
       typedef boost::unordered_map<nta_outgoing_t*, boost::shared_ptr<RIP> > mapOrq2RIP ;
        mapOrq2RIP m_mapOrq2RIP ;


	} ;

}

#endif