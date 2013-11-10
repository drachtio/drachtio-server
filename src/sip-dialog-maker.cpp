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
#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "sip-dialog-maker.hpp"
#include "controller.hpp"


namespace {
    void cloneRespondToSipRequest(su_root_magic_t* p, su_msg_r msg, void* arg ) {
    	drachtio::DrachtioController* pController = reinterpret_cast<drachtio::DrachtioController*>( p ) ;
        drachtio::SipDialogMaker::InviteResponseData* d = reinterpret_cast<drachtio::SipDialogMaker::InviteResponseData*>( arg ) ;
        pController->getDialogMaker()->doRespondToSipRequest( d ) ;
    }
}


namespace drachtio {

	SipDialogMaker::mapHdr2Tag SipDialogMaker::m_mapHdr2Tag = boost::assign::map_list_of
		( string("user_agent"), siptag_user_agent_str ) 
		;


	SipDialogMaker::SipDialogMaker( DrachtioController* pController, su_clone_r* pClone ) : m_pController(pController), m_pClone(pClone) {
		map<string,string> t = boost::assign::map_list_of
				( string("Via"), string("") ) 
				;
	}
	SipDialogMaker::~SipDialogMaker() {

	}

    void SipDialogMaker::respondToSipRequest( const string& msgId, boost::shared_ptr<JsonMsg> pMsg  ) {
       DR_LOG(log_debug) << "answerInvite thread id: " << boost::this_thread::get_id() << endl ;

        su_msg_r msg = SU_MSG_R_INIT ;
        int rv = su_msg_create( msg, su_clone_task(*m_pClone), su_root_task(m_pController->getRoot()),  cloneRespondToSipRequest, 
        	sizeof( SipDialogMaker::InviteResponseData ) );
        if( rv < 0 ) {
            return  ;
        }
        void* place = su_msg_data( msg ) ;

        /* we need to use placement new to allocate the object in a specific address, hence we are responsible for deleting it (below) */
        InviteResponseData* msgData = new(place) InviteResponseData( msgId, pMsg ) ;
        rv = su_msg_send(msg);  
        if( rv < 0 ) {
            return  ;
        }
    }
    void SipDialogMaker::doRespondToSipRequest( InviteResponseData* pData ) {
        string msgId( pData->getMsgId() ) ;
        boost::shared_ptr<JsonMsg> pMsg = pData->getMsg() ;

        DR_LOG(log_debug) << "responding to invite in thread " << boost::this_thread::get_id() << " with msgId " << msgId << endl ;

        mapMsgId2IIP::iterator it = m_mapMsgId2IIP.find( msgId ) ;
        if( m_mapMsgId2IIP.end() != it ) {
            boost::shared_ptr<IIP> iip = it->second ;
            nta_leg_t* leg = iip->leg() ;
            nta_incoming_t* irq = iip->irq() ;

            int code ;
            string status ;
            pMsg->get<int>("data.code", code ) ;
            pMsg->get<string>("data.status", status);

            /* iterate through data.opts.headers, adding headers to the response */
            json_spirit::mObject obj ;
            if( pMsg->get<json_spirit::mObject>("data.opts.headers", obj) ) {
            	int nHdrs = obj.size() ;
            	tagi_t *tags = new tagi_t[nHdrs+1] ;
            	int i = 0; 
            	for( json_spirit::mConfig::Object_type::iterator it = obj.begin() ; it != obj.end(); it++, i++ ) {

            		/* default to skip, as this may not be a header we are allowed to set, or value might not be provided correctly (as a string) */
					tags[i].t_tag = tag_skip ;
					tags[i].t_value = (tag_value_t) 0 ;            			

            		string hdr = boost::to_lower_copy( boost::replace_all_copy( it->first, "-", "_" ) );
            		mapHdr2Tag::const_iterator itTag = SipDialogMaker::m_mapHdr2Tag.find( hdr ) ;
            		if( itTag != SipDialogMaker::m_mapHdr2Tag.end() ) {
	            		try {
		            		string value = it->second.get_str() ;
			           		DR_LOG(log_debug) << "Adding header '" << hdr << "' with value '" << value << "'" << endl ;

			           		tags[i].t_tag = itTag->second ;
			           		tags[i].t_value = (tag_value_t) value.c_str() ;
 

	            		} catch( std::runtime_error& err ) {
	            			DR_LOG(log_error) << "Error attempting to read string value for header " << hdr << ": " << err.what() << endl;
	            		}            			
            		} 
            		else {
            			DR_LOG(log_error) << "Error attempting to set a value for header '" << it->first << "': this header can not be overwritten by client" << endl;
            		}
            	}

            	tags[nHdrs].t_tag = tag_null ;
				tags[nHdrs].t_value = (tag_value_t) 0 ;            	

	            nta_incoming_treply( irq, code, status.empty() ? NULL : status.c_str(), TAG_NEXT(tags) ) ;           	

            	delete[] tags ;
            }
            else {
	            nta_incoming_treply( irq, code, status.empty() ? NULL : status.c_str(), TAG_END() ) ;           	
            }

 
            if( code >= 200 ) {
                m_mapMsgId2IIP.erase( it ) ;
            }
         }
        else {
            DR_LOG(log_warning) << "Unable to find invite-in-progress with msgId " << msgId << endl ;
        }

        /* we must explicitly delete an object allocated with placement new */
        pData->~InviteResponseData() ;
    }
	void SipDialogMaker::addIncomingInviteTransaction( nta_leg_t* leg, nta_incoming_t* irq, const string& msgId ) {
	    boost::shared_ptr<IIP> p = boost::make_shared<IIP>(leg, irq, msgId) ;
	    m_mapIrq2IIP.insert( mapIrq2IIP::value_type(irq, p) ) ;
	    m_mapMsgId2IIP.insert( mapMsgId2IIP::value_type(msgId, p) ) ;		
	}




}