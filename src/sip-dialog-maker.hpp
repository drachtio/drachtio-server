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
#ifndef __SIP_DIALOG_MAKER_HPP__
#define __SIP_DIALOG_MAKER_HPP__

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>

#include <sofia-sip/su_wait.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/su_log.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/nta_stateless.h>

#include "json-msg.hpp"

#define MSG_ID_LEN (64)

namespace drachtio {

	class DrachtioController ;

	/* invites in progress */
	class IIP {
	public:
		IIP( nta_leg_t* leg, nta_incoming_t* irq, const string& msgId ) : m_leg(leg), m_irq(irq), m_strMsgId(msgId) {}
		~IIP() {}

		nta_leg_t* leg(void) { return m_leg; }
		nta_incoming_t* irq(void) { return m_irq; }
		const string& msgId(void) { return m_strMsgId; }

	private:
		string 			m_strMsgId ;
		nta_leg_t* 		m_leg ;
		nta_incoming_t*	m_irq ;
	} ;

	class SipDialogMaker {
	public:
		SipDialogMaker(DrachtioController* pController, su_clone_r* pClone );
		~SipDialogMaker() ;

		class InviteResponseData {
		public:
			InviteResponseData() {
				memset(m_szMsgId, 0, sizeof(m_szMsgId) ) ;
			}
			InviteResponseData(const string& msgId, boost::shared_ptr<JsonMsg> pMsg ) : m_pMsg(pMsg) {
				strncpy( m_szMsgId, msgId.c_str(), MSG_ID_LEN ) ;
			}
			~InviteResponseData() {}
			InviteResponseData& operator=(const InviteResponseData& md) {
				m_pMsg = md.m_pMsg ;
				strncpy( m_szMsgId, md.m_szMsgId, MSG_ID_LEN) ;
				return *this ;
			}

			const char* getMsgId() { return m_szMsgId; } 
			boost::shared_ptr<JsonMsg> getMsg(void) { return m_pMsg; }

		private:
			char	m_szMsgId[MSG_ID_LEN];
			boost::shared_ptr<JsonMsg> m_pMsg ;
		} ;

		void addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, const string& msgId ) ;

        void respondToSipRequest( const string& msgId, boost::shared_ptr<JsonMsg> pMsg ) ;		//called from worker thread, posts message into main thread
        void doRespondToSipRequest( InviteResponseData* pData ) ;	//does the actual sip messaging, within main thread

	private:
		DrachtioController* m_pController ;
		su_clone_r*			m_pClone ;


 
 		/* we need to lookup invites in progress by nta_incoming_t* when we get a CANCEL from the network */
        typedef boost::unordered_map<nta_incoming_t*, boost::shared_ptr<IIP> > mapIrq2IIP ;
        mapIrq2IIP m_mapIrq2IIP ;

 		/* we need to lookup invites in progress by msgId when we get a request from a client to respond to the invite */
       typedef boost::unordered_map<string, boost::shared_ptr<IIP> > mapMsgId2IIP ;
        mapMsgId2IIP m_mapMsgId2IIP ;

        typedef boost::unordered_map<string,tag_type_t> mapHdr2Tag ;
        static mapHdr2Tag m_mapHdr2Tag ;


	} ;

}

#endif