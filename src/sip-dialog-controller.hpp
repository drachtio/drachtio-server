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
#include <boost/enable_shared_from_this.hpp>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/nta.h>

#include "sip-dialog.hpp"
#include "client-controller.hpp"

#define START_LEN (512)
#define HDR_LEN (4192)
#define BODY_LEN (4192)

namespace drachtio {

	class DrachtioController ;

	/* invites in progress */
	class IIP {
	public:
		IIP( nta_leg_t* leg, nta_incoming_t* irq, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) : 
			m_leg(leg), m_irq(irq), m_orq(NULL), m_strTransactionId(transactionId), m_dlg(dlg),m_role(uas_role),m_rel(NULL) {}

		IIP( nta_leg_t* leg, nta_outgoing_t* orq, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) : 
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
		boost::shared_ptr<SipDialog> dlg(void) { return m_dlg; }

	private:
		string 			m_strTransactionId ;
		nta_leg_t* 		m_leg ;
		nta_incoming_t*	m_irq ;
		nta_outgoing_t*	m_orq ;
		nta_reliable_t*	m_rel ;
		boost::shared_ptr<SipDialog> 	m_dlg ;
		agent_role		m_role ;
	} ;

	/* requests in progress (sent by application, may be inside or outside a dialog) */
	class RIP {
	public:
		RIP( const string& transactionId ) : 
			m_transactionId(transactionId) {}
		RIP( const string& transactionId, const string& dialogId ) : 
			m_transactionId(transactionId), m_dialogId(dialogId) {}

		~RIP() {}

		const string& getTransactionId(void) { return m_transactionId; }
		const string& getDialogId(void) { return m_dialogId; }

	private:
		string 			m_transactionId ;
		string			m_dialogId ;
	} ;


	class SipDialogController : public boost::enable_shared_from_this<SipDialogController> {
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
			~SipMessageData() {}
			SipMessageData& operator=(const SipMessageData& md) {
				strncpy( m_szClientMsgId, md.m_szClientMsgId, MSG_ID_LEN) ;
				strncpy( m_szTransactionId, md.m_szTransactionId, MSG_ID_LEN) ;
				strncpy( m_szDialogId, md.m_szDialogId, MSG_ID_LEN) ;
				strncpy( m_szRequestId, md.m_szRequestId, MSG_ID_LEN) ;
				strncpy( m_szStartLine, md.m_szStartLine, START_LEN ) ;
				strncpy( m_szHeaders, md.m_szHeaders, HDR_LEN ) ;
				strncpy( m_szBody, md.m_szBody, BODY_LEN ) ;
				return *this ;
			}

			const char* getClientMsgId() { return m_szClientMsgId; } 
			const char* getTransactionId() { return m_szTransactionId; } 
			const char* getDialogId() { return m_szDialogId; } 
			const char* getRequestId() { return m_szRequestId; } 
			const char* getHeaders() { return m_szHeaders; } 
			const char* getStartLine() { return m_szStartLine; } 
			const char* getBody() { return m_szBody; } 

		private:
			char	m_szClientMsgId[MSG_ID_LEN];
			char	m_szTransactionId[MSG_ID_LEN];
			char	m_szRequestId[MSG_ID_LEN];
			char	m_szDialogId[MSG_ID_LEN];
			char	m_szStartLine[START_LEN];
			char	m_szHeaders[HDR_LEN];
			char	m_szBody[BODY_LEN];
		} ;

		//NB: sendXXXX are called when client is sending a message
		bool sendRequestInsideDialog( const string& clientMsgId, const string& dialogId, const string& startLine, const string& headers, const string& body, string& transactionId ) ;
		bool sendRequestOutsideDialog( const string& clientMsgId, const string& startLine, const string& headers, const string& body, string& transactionId, string& dialogId ) ;
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

        void notifyRefreshDialog( boost::shared_ptr<SipDialog> dlg ) ;
        void notifyTerminateStaleDialog( boost::shared_ptr<SipDialog> dlg ) ;

	    bool isManagingTransaction( const string& transactionId ) {
	    	return m_mapTransactionId2IIP.end() != m_mapTransactionId2IIP.find( transactionId ) ;
	    }

		void logStorageCount(void)  ;

		/// IIP helpers 
		void addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, sip_t const *sip, const string& transactionId, boost::shared_ptr<SipDialog> dlg ) {
	        const char* a_tag = nta_incoming_tag( irq, NULL) ;
	        nta_leg_tag( leg, a_tag ) ;
	        dlg->setLocalTag( a_tag ) ;

			boost::lock_guard<boost::mutex> lock(m_mutex) ;

	        boost::shared_ptr<IIP> p = boost::make_shared<IIP>(leg, irq, transactionId, dlg) ;
	        m_mapIrq2IIP.insert( mapIrq2IIP::value_type(irq, p) ) ;
	        m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(transactionId, p) ) ;   
	        m_mapLeg2IIP.insert( mapLeg2IIP::value_type(leg,p)) ;   

	        this->bindIrq( irq ) ;
		}
		void addOutgoingInviteTransaction( nta_leg_t* leg, nta_outgoing_t* orq, sip_t const *sip, boost::shared_ptr<SipDialog> dlg ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;

			boost::shared_ptr<IIP> p = boost::make_shared<IIP>(leg, orq, dlg->getTransactionId(), dlg) ;
			m_mapOrq2IIP.insert( mapOrq2IIP::value_type(orq, p) ) ;
			m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(dlg->getTransactionId(), p) ) ;   
			m_mapLeg2IIP.insert( mapLeg2IIP::value_type(leg,p)) ;   			
		}
		void addIncomingPrackTransaction( const string& transactionId, boost::shared_ptr<IIP> p ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			m_mapTransactionId2IIP.insert( mapTransactionId2IIP::value_type(transactionId, p) ) ;   			
		}
		void addReliable( nta_reliable_t* rel, boost::shared_ptr<IIP>& iip) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			m_mapRel2IIP.insert( mapRel2IIP::value_type(rel, iip) ) ;
		}
		bool findIIPByIrq( nta_incoming_t* irq, boost::shared_ptr<IIP>& iip ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
	        mapIrq2IIP::iterator it = m_mapIrq2IIP.find( irq ) ;
	        if( m_mapIrq2IIP.end() == it ) return false ;
	        iip = it->second ;
	        return true ;			
		}
		bool findIIPByOrq( nta_outgoing_t* orq, boost::shared_ptr<IIP>& iip ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
	        mapOrq2IIP::iterator it = m_mapOrq2IIP.find( orq ) ;
	        if( m_mapOrq2IIP.end() == it ) return false ;
	        iip = it->second ;
	        return true ;						
		}
		bool findIIPByLeg( nta_leg_t* leg, boost::shared_ptr<IIP>& iip ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			mapLeg2IIP::iterator it = m_mapLeg2IIP.find( leg ) ;
			if( m_mapLeg2IIP.end() == it ) return false ;
			iip = it->second ;
			return true ;			
		}
		bool findIIPByTransactionId( const string& transactionId, boost::shared_ptr<IIP>& iip ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			mapTransactionId2IIP::iterator it = m_mapTransactionId2IIP.find( transactionId ) ;
			if( m_mapTransactionId2IIP.end() == it ) return false ;
			iip = it->second ;
			return true ;			
		}
		bool findIIPByReliable( nta_reliable_t* rel, boost::shared_ptr<IIP>& iip ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
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
		void addDialog( boost::shared_ptr<SipDialog> dlg ) {
			const string strDialogId = dlg->getDialogId() ;
			nta_leg_t *leg = nta_leg_by_call_id( m_agent, dlg->getCallId().c_str() );
			assert( leg ) ;

			boost::lock_guard<boost::mutex> lock(m_mutex) ;
	        m_mapLeg2Dialog.insert( mapLeg2Dialog::value_type(leg,dlg)) ;	
	        m_mapId2Dialog.insert( mapId2Dialog::value_type(strDialogId, dlg)) ;

	        m_pClientController->addDialogForTransaction( dlg->getTransactionId(), strDialogId ) ;		
		}
		bool findDialogByLeg( nta_leg_t* leg, boost::shared_ptr<SipDialog>& dlg ) {
			/* look in invites-in-progress first */
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
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
		bool findDialogById(  const string& strDialogId, boost::shared_ptr<SipDialog>& dlg ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
			if( m_mapId2Dialog.end() == it ) return false ;
			dlg = it->second ;
			return true ;
		}
		bool findDialogByCallId( const string& strCallId, boost::shared_ptr<SipDialog>& dlg ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			mapId2Dialog::iterator it = m_mapId2Dialog.find( strCallId + ";uas" ) ;
			if( m_mapId2Dialog.end() == it ) it = m_mapId2Dialog.find( strCallId + ";uac" ) ;
			if( m_mapId2Dialog.end() == it ) {
				return false ;
			}
			dlg = it->second ;
			return true ;
		}

		/// RIP helpers
		void addRIP( nta_outgoing_t* orq, boost::shared_ptr<RIP> rip) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			m_mapOrq2RIP.insert( mapOrq2RIP::value_type(orq,rip)) ;
		}
		bool findRIPByOrq( nta_outgoing_t* orq, boost::shared_ptr<RIP>& rip ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
	        mapOrq2RIP::iterator it = m_mapOrq2RIP.find( orq ) ;
	        if( m_mapOrq2RIP.end() == it ) return false ;
	        rip = it->second ;
	        return true ;						
		}
		void clearRIP( nta_outgoing_t* orq ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			mapOrq2RIP::iterator it = m_mapOrq2RIP.find( orq ) ;
			nta_outgoing_destroy( orq ) ;
			if( m_mapOrq2RIP.end() == it ) return  ;
			m_mapOrq2RIP.erase( it ) ;						
		}

		/// IRQ helpers
		void addIncomingRequestTransaction( nta_incoming_t* irq, const string& transactionId) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			m_mapTransactionId2Irq.insert( mapTransactionId2Irq::value_type(transactionId, irq)) ;
		}
		bool findIrqByTransactionId( const string& transactionId, nta_incoming_t*& irq ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
	        mapTransactionId2Irq::iterator it = m_mapTransactionId2Irq.find( transactionId ) ;
	        if( m_mapTransactionId2Irq.end() == it ) return false ;
	        irq = it->second ;
	        return true ;						
		}
		nta_incoming_t* findAndRemoveTransactionIdForIncomingRequest( const string& transactionId ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			nta_incoming_t* irq = NULL ;
			mapTransactionId2Irq::iterator it = m_mapTransactionId2Irq.find( transactionId ) ;
			if( m_mapTransactionId2Irq.end() != it ) {
				irq = it->second ;
				m_mapTransactionId2Irq.erase( it ) ;
			}
			return irq ;
		}


	protected:
		boost::shared_ptr<SipDialog> clearIIP( nta_leg_t* leg ) ;
		
		void clearDialog( const string& strDialogId ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;
			
			mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
			if( m_mapId2Dialog.end() == it ) {
				assert(0) ;
				return ;
			}
			boost::shared_ptr<SipDialog> dlg = it->second ;
			nta_leg_t* leg = nta_leg_by_call_id( m_agent, dlg->getCallId().c_str() );
			m_mapId2Dialog.erase( it ) ;

			mapLeg2Dialog::iterator itLeg = m_mapLeg2Dialog.find( leg ) ;
			if( m_mapLeg2Dialog.end() == itLeg ) {
				assert(0) ;
				return ;
			}
			m_mapLeg2Dialog.erase( itLeg ) ;				
		}
		void clearDialog( nta_leg_t* leg ) {
			boost::lock_guard<boost::mutex> lock(m_mutex) ;

			mapLeg2Dialog::iterator it = m_mapLeg2Dialog.find( leg ) ;
			if( m_mapLeg2Dialog.end() == it ) {
				assert(0) ;
				return ;
			}
			boost::shared_ptr<SipDialog> dlg = it->second ;
			string strDialogId = dlg->getDialogId() ;
			m_mapLeg2Dialog.erase( it ) ;

			mapId2Dialog::iterator itId = m_mapId2Dialog.find( strDialogId ) ;
			if( m_mapId2Dialog.end() == itId ) {
				assert(0) ;
				return ;
			}
			m_mapId2Dialog.erase( itId );			
		}

 		bool searchForHeader( tagi_t* tags, tag_type_t header, string& value ) ;

		void bindIrq( nta_incoming_t* irq ) ;


	private:
		DrachtioController* m_pController ;
		su_clone_r*			m_pClone ;
		sip_contact_t*		m_my_contact ;

		/* since access to the various maps below can be triggered either by arriva or network message, or client message - 
			each in a different thread - we use this mutex to protect them.  To keep things straight, the mutex lock operations
			are utilized in the low-level addXX, findXX, and clearXX methods that appear in this header file.  There should be
			NO direct access to the maps nor use of the mutex in the .cpp (the exception being the method to log storage counts)
		*/
       	boost::mutex 		m_mutex ;

		nta_agent_t*		m_agent ;
		boost::shared_ptr< ClientController > m_pClientController ;
 
 		/// INVITEs in progress

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

		/* we need to lookup invites in progress by nta_reliable_t when we get an PRACK from the network */
		typedef boost::unordered_map<nta_reliable_t*, boost::weak_ptr<IIP> > mapRel2IIP ;
		mapRel2IIP m_mapRel2IIP ;


        /// Stable Dialogs 

  		/* we need to lookup dialogs by leg when we get a request from the network */
        typedef boost::unordered_map<nta_leg_t*, boost::shared_ptr<SipDialog> > mapLeg2Dialog ;
        mapLeg2Dialog m_mapLeg2Dialog ;

 		/* we need to lookup dialogs by dialog id when we get a request from the client  */
       typedef boost::unordered_map<string, boost::shared_ptr<SipDialog> > mapId2Dialog ;
        mapId2Dialog m_mapId2Dialog ;

        /// Requests sent by client

        /* we need to lookup responses to requests sent by the client inside a dialog */
       typedef boost::unordered_map<nta_outgoing_t*, boost::shared_ptr<RIP> > mapOrq2RIP ;
        mapOrq2RIP m_mapOrq2RIP ;

        /// Requests received from the network 

        /* we need to lookup incoming transactions by transaction id when we get a response from the client */
        typedef boost::unordered_map<string, nta_incoming_t*> mapTransactionId2Irq ;
        mapTransactionId2Irq m_mapTransactionId2Irq ;


	} ;

}

#endif
