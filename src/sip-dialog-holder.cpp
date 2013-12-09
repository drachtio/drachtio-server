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

#include "sip-dialog-holder.hpp"
#include "controller.hpp"

namespace {
    void cloneSendSipRequestWithinDialog(su_root_magic_t* p, su_msg_r msg, void* arg ) {
        drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogHolder::SipMessageData* d = reinterpret_cast<drachtio::SipDialogHolder::SipMessageData*>( arg ) ;
        pController->getDialogHolder()->doSendSipRequestWithinDialog( d ) ;
    }
}

namespace drachtio {

	SipDialogHolder::SipDialogHolder(DrachtioController* pController, su_clone_r* pClone) : m_pController(pController), m_pClone(pClone) {

	}
	SipDialogHolder::~SipDialogHolder() {

	}

	int SipDialogHolder::sendSipRequestWithinDialog( boost::shared_ptr<JsonMsg> pMsg, const string& rid, boost::shared_ptr<SipDialog>& dlg ) {
       DR_LOG(log_debug) << "SipDialogHolder::sendSipRequestWithinDialog thread id: " << boost::this_thread::get_id() << endl ;

        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneSendSipRequestWithinDialog, 
            sizeof( SipDialogHolder::SipMessageData ) );
        if( rv < 0 ) {
            return  false;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        string transactionId ;
        SipMessageData* msgData = new(place) SipMessageData( transactionId, rid, pMsg ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  false;
        }
        return true ;
	}

    void SipDialogHolder::doSendSipRequestWithinDialog( SipMessageData* pData ) {
        nta_leg_t* leg = NULL ;
        nta_outgoing_t* orq = NULL ;
        string rid( pData->getRequestId() ) ;
        boost::shared_ptr<JsonMsg> pMsg = pData->getMsg() ;
        ostringstream o ;

        DR_LOG(log_debug) << "SipDialogHolder::doSendSipRequestWithinDialog in thread " << boost::this_thread::get_id() << " with rid " << rid << endl ;

        /*
            OK, here is what we do:

             1. Create an outgoing transaction (nta_outgoing_tcreate).  

             ..save something so we can handle response?
 
        */
        string strFrom, strTo, strRequestUri, strMethod ;
        json_spirit::mObject obj ;

        try {


        } catch( std::runtime_error& err ) {
            DR_LOG(log_error) << err.what() << endl;
        }                       

        /* we must explicitly delete an object allocated with placement new */

        pData->~SipMessageData() ;
    }

	void SipDialogHolder::addDialog( boost::shared_ptr<SipDialog> dlg ) {
		const string strDialogId = dlg->getDialogId() ;
		DR_LOG(log_debug) << "SipDialogHolder::addDialog call-id " << dlg->getCallId() << " dialog id " << strDialogId << endl ;
		nta_leg_t *leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
		assert( leg ) ;

        m_mapLeg2Dialog.insert( mapLeg2Dialog::value_type(leg,dlg)) ;	
        m_mapId2Dialog.insert( mapId2Dialog::value_type(strDialogId, dlg)) ;

        m_pController->getClientController()->addDialogForTransaction( dlg->getTransactionId(), strDialogId ) ;
	}

	bool SipDialogHolder::findDialogByLeg( nta_leg_t* leg, boost::shared_ptr<SipDialog>& dlg ) {
		mapLeg2Dialog::iterator it = m_mapLeg2Dialog.find( leg ) ;
		if( m_mapLeg2Dialog.end() == it ) return false ;
		dlg = it->second ;
		return true ;
	}
	bool SipDialogHolder::findDialogById( const string& strDialogId, boost::shared_ptr<SipDialog>& dlg ) {
		mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
		if( m_mapId2Dialog.end() == it ) return false ;
		dlg = it->second ;
		return true ;
	}

	void SipDialogHolder::clearDialog( const string& strDialogId ) {
		mapId2Dialog::iterator it = m_mapId2Dialog.find( strDialogId ) ;
		if( m_mapId2Dialog.end() == it ) {
			assert(0) ;
			return ;
		}
		boost::shared_ptr<SipDialog> dlg = it->second ;
		nta_leg_t* leg = nta_leg_by_call_id( m_pController->getAgent(), dlg->getCallId().c_str() );
		m_mapId2Dialog.erase( it ) ;

		mapLeg2Dialog::iterator itLeg = m_mapLeg2Dialog.find( leg ) ;
		if( m_mapLeg2Dialog.end() == itLeg ) {
			assert(0) ;
			return ;
		}
		m_mapLeg2Dialog.erase( itLeg ) ;

	}
	void SipDialogHolder::clearDialog( nta_leg_t* leg ) {
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


}