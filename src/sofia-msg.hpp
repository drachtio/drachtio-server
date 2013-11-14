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
#ifndef __SOFIA_MSG_HPP__
#define __SOFIA_MSG_HPP__

#include <sofia-sip/nta.h>
#include <sofia-sip/sip.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_extra.h>

#include "drachtio.h"
#include "json-msg.hpp"

using namespace std ;

namespace drachtio {

	class SofiaMsg {
	public:
		SofiaMsg(  sip_t const *sip ) ;
		~SofiaMsg() ;


		const string& str() const { return m_strMsg; }

		struct url_parser {
			static stringstream& toJson( url_t* url, stringstream& o ) ;
		} ;
		struct request_parser {
			static stringstream& toJson( sip_request_t* req, stringstream& o) ;
		} ;
		struct status_parser {
			static stringstream& toJson( sip_status_t* req, stringstream& o) ;
		} ;
		struct via_parser {
			static stringstream& toJson( sip_via_t* via, stringstream& o) ;
		} ;
		struct route_parser {
			static stringstream& toJson( sip_route_t* via, stringstream& o) ;
		} ;
		struct record_route_parser {
			static stringstream& toJson( sip_record_route_t* via, stringstream& o) ;
		} ;
		struct max_forwards_parser {
			static stringstream& toJson( sip_max_forwards_t* mf, stringstream& o) ;
		} ;
		struct call_id_parser {
			static stringstream& toJson( sip_call_id_t* cid, stringstream& o) ;
		} ;
		struct cseq_parser {
			static stringstream& toJson( sip_cseq_t* cid, stringstream& o) ;
		} ;
		struct addr_parser {
			static stringstream& toJson( sip_addr_t* addr, stringstream& o) ;
		} ;
		struct from_parser {
			static stringstream& toJson( sip_from_t* addr, stringstream& o) ;
		} ;
		struct to_parser {
			static stringstream& toJson( sip_to_t* addr, stringstream& o) ;
		} ;
		struct contact_parser {
			static stringstream& toJson( sip_contact_t* addr, stringstream& o) ;
		} ;
		struct content_length_parser {
			static stringstream& toJson( sip_content_length_t* addr, stringstream& o) ;
		} ;
		struct content_type_parser {
			static stringstream& toJson( sip_content_type_t* ct, stringstream& o) ;
		} ;
		struct date_parser {
			static stringstream& toJson( sip_date_t* date, stringstream& o) ;
		} ;
		struct event_parser {
			static stringstream& toJson( sip_event_t* event, stringstream& o) ;
		} ;
		struct error_info_parser {
			static stringstream& toJson( sip_error_info_t* p, stringstream& o) ;
		} ;
		struct expires_parser {
			static stringstream& toJson( sip_expires_t* p, stringstream& o) ;
		} ;
		struct min_expires_parser {
			static stringstream& toJson( sip_min_expires_t* p, stringstream& o) ;
		} ;
		struct rack_parser {
			static stringstream& toJson( sip_rack_t* p, stringstream& o) ;
		} ;
		struct refer_to_parser {
			static stringstream& toJson( sip_refer_to_t* p, stringstream& o) ;
		} ;
		struct referred_by_parser {
			static stringstream& toJson( sip_referred_by_t* p, stringstream& o) ;
		} ;
		struct replaces_parser {
			static stringstream& toJson( sip_replaces_t* p, stringstream& o) ;
		} ;
		struct retry_after_parser {
			static stringstream& toJson( sip_retry_after_t* p, stringstream& o) ;
		} ;
		struct request_disposition_parser {
			static stringstream& toJson( sip_request_disposition_t* p, stringstream& o) ;
		} ;
		struct caller_prefs_parser {
			static stringstream& toJson( sip_caller_prefs_t* p, stringstream& o) ;
		} ;
		struct reason_parser {
			static stringstream& toJson( sip_reason_t* p, stringstream& o) ;
		} ;
		struct session_expires_parser {
			static stringstream& toJson( sip_session_expires_t* p, stringstream& o) ;
		} ;
		struct min_se_parser {
			static stringstream& toJson( sip_min_se_t* p, stringstream& o) ;
		} ;
		struct subscription_state_parser {
			static stringstream& toJson( sip_subscription_state_t* p, stringstream& o) ;
		} ;
		struct timestamp_parser {
			static stringstream& toJson( sip_timestamp_t* p, stringstream& o) ;
		} ;
		struct security_agree_parser {
			static stringstream& toJson( sip_security_server_t* p, stringstream& o) ;
		} ;
		struct privacy_parser {
			static stringstream& toJson( sip_privacy_t* p, stringstream& o) ;
		} ;
		struct etag_parser {
			static stringstream& toJson( sip_etag_t* p, stringstream& o) ;
		} ;
		struct if_match_parser {
			static stringstream& toJson( sip_if_match_t* p, stringstream& o) ;
		} ;
		struct mime_version_parser {
			static stringstream& toJson( sip_mime_version_t* p, stringstream& o) ;
		} ;
		struct content_encoding_parser {
			static stringstream& toJson( sip_content_encoding_t* p, stringstream& o) ;
		} ;
		struct content_language_parser {
			static stringstream& toJson( sip_content_language_t* p, stringstream& o) ;
		} ;
		struct content_disposition_parser {
			static stringstream& toJson( sip_content_disposition_t* p, stringstream& o) ;
		} ;
		struct proxy_require_parser {
			static stringstream& toJson( sip_proxy_require_t* p, stringstream& o) ;
		} ;
		struct payload_parser {
			static stringstream& toJson( sip_payload_t* p, stringstream& o) ;
		} ;





		struct generic_msg_list_parser {
			static stringstream& toJson( msg_list_t* list, stringstream& o) ;
		} ;
		struct generic_msg_params_parser {
			static stringstream& toJson( const msg_param_t* params, stringstream& o) ;
		} ;

	protected:

	private:
		SofiaMsg() ;

		string	m_strMsg ;

	} ;


}


#endif 