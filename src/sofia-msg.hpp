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

#include <boost/algorithm/string/replace.hpp>

#include "drachtio.h"
#include "json-msg.hpp"


using namespace std ;

namespace {
	inline stringstream& JSONAPPEND( const char* szName, const char* szValue, stringstream& o, bool comma = true ) {
		if( szValue ) {
			if( comma ) o << ",\"" << szName << "\": " ; 
			else o << "\"" << szName << "\": " ; 
			o << "\"" << szValue  << "\""  ; 
		}
		return o ;
	}
	inline stringstream& JSONAPPEND( const char* szName, int value, stringstream& o, bool comma = true ) {
		if( comma ) o << ",\"" << szName << "\": " ; 
		else o << "\"" << szName << "\": " ; 
		o  << value   ; 
		return o ;
	}
	inline stringstream& JSONAPPEND( const char* szName, bool value, stringstream& o, bool comma = true ) {
		if( comma ) o << ",\"" << szName << "\": " ; 
		else o << "\"" << szName << "\": " ; 
		o  << (value ? "true" : "false")  ; 
		return o ;
	}
	inline stringstream& JSONAPPEND( const char* szName, unsigned long value, stringstream& o, bool comma = true ) {
		if( comma ) o << ",\"" << szName << "\": " ; 
		else o << "\"" << szName << "\": " ; 
		o << value   ; 
		return o ;
	}
	inline stringstream& JSONAPPEND( const char* szName, uint32_t value, stringstream& o, bool comma = true ) {
		if( comma ) o << ",\"" << szName << "\": " ; 
		else o << "\"" << szName << "\": " ; 
		o << value   ; 
		return o ;
	}
}

namespace drachtio {

	class SofiaMsg {
	public:
		SofiaMsg(  nta_incoming_t* irq, sip_t const *sip ) ;
		SofiaMsg(  nta_outgoing_t* orq, sip_t const *sip ) ;
		~SofiaMsg() {}

		void populateHeaders( sip_t const *sip, stringstream& o ) ;

		const string& str() const { return m_strMsg; }

		struct generic_msg_parser {
			static stringstream& toJson( msg_generic_t* g, stringstream& o) {
				o << "[" ;
				msg_generic_t* p = g ;
				do {
					if( p != g ) o << "," ;
					o <<  "\"" << p->g_string << "\""  ;
					p = p->g_next ;
				} while( p ) ;
				o << "]" ;
				return o ;
			}
		} ;
		struct generic_msg_list_parser {
			static stringstream& toJson( msg_list_t* list, stringstream& o) {
				o << "[" ;
				msg_list_t* l = list ;
				do {
					if( l != list ) o << "," ;
					int i = 0 ;
					o << "[" ;
					for (const msg_param_t* p = l->k_items; *p; p++, i++) {
						if( i > 0 ) o << "," ;
						o << "\"" << *p << "\""; 
					}
					o << "]" ;
				} while( (l = l->k_next) ) ;

				o << "]" ;
				return o ;				
			}
		} ;
		struct generic_msg_params_parser {
			static stringstream& toJson( const msg_param_t* params, stringstream& o) {
				o << "[" ;
				if( params ) {
					int i = 0 ;
					for (const msg_param_t* p = params; *p; p++, i++) {
						if( i > 0 ) o << "," ;
						o << "\"" << *p << "\""; 
					}			
				}
				o << "]" ;	
				return o ;					
			}
		} ;


		struct url_parser {
			static stringstream& toJson( url_t* url, stringstream& o ) {
				o << "{" ;
				JSONAPPEND("scheme", url->url_scheme, o, false) ;
				JSONAPPEND("user", url->url_user, o) ;
				JSONAPPEND("password", url->url_password, o) ;
				JSONAPPEND("host", url->url_host, o) ;
				JSONAPPEND("port", url->url_port, o)  ;
				JSONAPPEND("path", url->url_path, o)  ;
				JSONAPPEND("params", url->url_params, o) ;
				JSONAPPEND("headers", url->url_headers, o)  ;
				JSONAPPEND("fragment", url->url_fragment, o)  ;
				o << "}" ;
				return o ;
			}
		} ;
		struct request_parser {
			static stringstream& toJson( sip_request_t* req, stringstream& o) {
				o << "{" ;
				JSONAPPEND("method",req->rq_method_name,o, false) ;
				JSONAPPEND("version",req->rq_version,o)  ;
				o << ",\"url\": "  ;
				url_parser::toJson( req->rq_url, o ) ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct status_parser {
			static stringstream& toJson( sip_status_t* st, stringstream& o) {
				o << "{" ;
				JSONAPPEND("version", st->st_version, o, false) ;
				JSONAPPEND("status", st->st_status, o)   ;
				JSONAPPEND("phrase", st->st_phrase, o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct via_parser {
			static stringstream& toJson( sip_via_t* via, stringstream& o) {
				o << "[" ;
				sip_via_t* v = via ;
				do {
					if( v != via ) o << "," ;
					o << "{" ;
					JSONAPPEND("protocol", via->v_protocol, o, false) ;
					JSONAPPEND("host", via->v_host, o) ;
					JSONAPPEND("port", via->v_port, o)  ;
					JSONAPPEND("comment", via->v_comment, o)  ;
					JSONAPPEND("ttl", via->v_ttl, o) ;
					JSONAPPEND("maddr", via->v_maddr, o)  ;
					JSONAPPEND("received", via->v_received, o)  ;
					JSONAPPEND("branch", via->v_branch, o)  ;
					JSONAPPEND("rport", via->v_rport, o)  ;
					JSONAPPEND("comp", via->v_comp, o) ;

					o << ",\"params\": [" ;
					if (via->v_params) {
						int i = 0 ;
						for (const msg_param_t* p = v->v_params; *p; p++, i++) {
							if( i > 0 ) o << "," ;
							o << "\"" << *p << "\""; 
						}
					}
					o << "]";
					o << "}" ; 
				} while( (v = via->v_next) ) ;
				o << "]" ;
				return o ;				
			}
		} ;
		struct route_parser {
			static stringstream& toJson( sip_route_t* route, stringstream& o) {
				o << "[" ;
				sip_route_t* r = route ;
				do {
					if( r != route ) o << ", " ;
					o << "{" ;
					JSONAPPEND("display",r->r_display,o) ;
					o << "\"url\": "  ;
					url_parser::toJson( r->r_url, o ) ;
					o << "}" ;
				} while( (r = r->r_next ) ) ;

				o << "]" ;
				return o ;				
			}
		} ;
		struct record_route_parser {
			static stringstream& toJson( sip_record_route_t* rroute, stringstream& o) {
				return route_parser::toJson( rroute, o) ;
			}
		} ;
		struct max_forwards_parser {
			static stringstream& toJson( sip_max_forwards_t* mf, stringstream& o) {
				o << mf->mf_count ;
				return o ;				
			}
		} ;
		struct call_id_parser {
			static stringstream& toJson( sip_call_id_t* cid, stringstream& o) {
				o << "\"" << cid->i_id << "\"" ;
				return o ;				
			}
		} ;
		struct cseq_parser {
			static stringstream& toJson( sip_cseq_t* cseq, stringstream& o) {
				o << "{" ;
				JSONAPPEND("seq", cseq->cs_seq,o, false)  ;
				JSONAPPEND("method", cseq->cs_method_name,o) ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct addr_parser {
			static stringstream& toJson( sip_addr_t* addr, stringstream& o) {
				o << "{" ;
				o << "\"url\": "  ;
				url_parser::toJson( addr->a_url, o ) ;
				JSONAPPEND("display", addr->a_display,o)  ;
				JSONAPPEND("comment", addr->a_comment,o)  ;
				JSONAPPEND("tag", addr->a_tag,o) ;
				o << ",\"params\": [" ;
				if( addr->a_params ) {
					int i = 0 ;
					for (const msg_param_t* p = addr->a_params; *p; p++, i++) {
						if( i > 0 ) o << "," ;
						o << "\"" << *p << "\""; 
					}			
				}
				o << "]" ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct from_parser {
			static stringstream& toJson( sip_from_t* addr, stringstream& o) {
				return addr_parser::toJson( addr, o ) ;
			}
		} ;
		struct to_parser {
			static stringstream& toJson( sip_to_t* addr, stringstream& o) {
				return addr_parser::toJson( addr, o ) ;
			}
		} ;
		struct contact_parser {
			static stringstream& toJson( sip_contact_t* c, stringstream& o) {
				o << "{" ;
				JSONAPPEND("display", c->m_display,o, false)  ;
				o << ",\"url\": "  ;
				url_parser::toJson( c->m_url, o ) ;
				JSONAPPEND("comment", c->m_comment,o)  ;
				JSONAPPEND("q", c->m_q,o) ;
				JSONAPPEND("expires", c->m_expires,o) ;
				o << ",\"params\": [" ;
				if( c->m_params ) {
					int i = 0 ;
					for (const msg_param_t* p = c->m_params; *p; p++, i++) {
						if( i > 0 ) o << "," ;
						o << "\"" << *p << "\""; 
					}			
				}
				o << "]" ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct content_length_parser {
			static stringstream& toJson( sip_content_length_t* addr, stringstream& o) {
				o <<  addr->l_length  ;
				return o ;				
			}
		} ;
		struct content_type_parser {
			static stringstream& toJson( sip_content_type_t* ct, stringstream& o) {
				o << "{" ;
				JSONAPPEND("type", ct->c_type,o, false)  ;
				JSONAPPEND("subtype", ct->c_subtype,o) ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( ct->c_params, o) ;			
				o << "}" ;
				return o ;		
			}
		} ;
		struct date_parser {
			static stringstream& toJson( sip_date_t* p, stringstream& o) {
				o << p->d_time  ;
				return o ;				
			}
		} ;
		struct event_parser {
			static stringstream& toJson( sip_event_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("type", p->o_type,o, false)  ;
				JSONAPPEND("id", p->o_id,o) ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->o_params, o) ;			
				o << "}" ;
				return o ;				
			}
		} ;
		struct error_info_parser {
			static stringstream& toJson( sip_error_info_t* p, stringstream& o) {
				o << "[" ;
				sip_error_info_t* ei = p ;
				do {
					if( ei != p ) o << "," ;
					o << "{" ;
					o << "\"url\": "  ;
					url_parser::toJson( ei->ei_url, o ) ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( ei->ei_params, o) ;			
					o << "}" ; 
				} while( 0 /* (ei = ei->ei_next) */ ) ; //Commented out because of bug in sofia sip.h where ei_next points to a sip_call_info_t for some reason
				o << "]" ;
				return o ;				
			}
		} ;
		struct expires_parser {
			static stringstream& toJson( sip_expires_t* p, stringstream& o) {
				o << "\"" << p->ex_date << "\"" ;
				return o ;				
			}
		} ;
		struct min_expires_parser {
			static stringstream& toJson( sip_min_expires_t* p, stringstream& o) {
				o << "\"" << p->me_delta << "\"" ;
				return o ;				
			}
		} ;
		struct rack_parser {
			static stringstream& toJson( sip_rack_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("method", p->ra_method_name,o, false)  ;
				JSONAPPEND("response", p->ra_response,o) ;
				JSONAPPEND("cseq", p->ra_cseq,o) ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct refer_to_parser {
			static stringstream& toJson( sip_refer_to_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("display", p->r_display,o, false)  ;
				o << ",\"url\": "  ;
				url_parser::toJson( p->r_url, o ) ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->r_params, o) ;			
				o << "}" ;
				return o ;				
			}
		} ;
		struct referred_by_parser {
			static stringstream& toJson( sip_referred_by_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("display", p->b_display,o, false)  ;
				o << ",\"url\": "  ;
				url_parser::toJson( p->b_url, o ) ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->b_params, o) ;			
				JSONAPPEND("cid", p->b_cid,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct replaces_parser {
			static stringstream& toJson( sip_replaces_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("call_id", p->rp_call_id,o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->rp_params, o) ;			
				JSONAPPEND("to_tag", p->rp_to_tag,o)  ;
				JSONAPPEND("from_tag", p->rp_from_tag,o)  ;
				JSONAPPEND("early_only", 1 == p->rp_early_only,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct retry_after_parser {
			static stringstream& toJson( sip_retry_after_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("delta", p->af_delta,o, false)  ;
				JSONAPPEND("comment", p->af_comment,o)  ;
				JSONAPPEND("duration", p->af_duration,o)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->af_params, o) ;			
				o << "}" ;
				return o ;				
			}
		} ;
		struct request_disposition_parser {
			static stringstream& toJson( sip_request_disposition_t* p, stringstream& o) {
				generic_msg_params_parser::toJson( p->rd_items, o) ;			
				return o ;				
			}
		} ;
		struct caller_prefs_parser {
			static stringstream& toJson( sip_caller_prefs_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("q", p->cp_q,o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->cp_params, o) ;			
				JSONAPPEND("require", 1 == p->cp_require,o)  ;
				JSONAPPEND("explicit", 1 == p->cp_explicit,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct reason_parser {
			static stringstream& toJson( sip_reason_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("protocol", p->re_protocol,o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->re_params, o) ;			
				JSONAPPEND("cause", p->re_cause,o)  ;
				JSONAPPEND("text", p->re_text,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct session_expires_parser {
			static stringstream& toJson( sip_session_expires_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("delta", p->x_delta,o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->x_params, o) ;			
				JSONAPPEND("refresher", p->x_refresher,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct min_se_parser {
			static stringstream& toJson( sip_min_se_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("delta", p->min_delta,o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->min_params, o) ;			
				o << "}" ;
				return o ;				
			}
		} ;
		struct subscription_state_parser {
			static stringstream& toJson( sip_subscription_state_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("substate", p->ss_substate,o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->ss_params, o) ;			
				JSONAPPEND("reason", p->ss_reason,o)  ;
				JSONAPPEND("expires", p->ss_expires,o)  ;
				JSONAPPEND("retry_after", p->ss_retry_after,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct timestamp_parser {
			static stringstream& toJson( sip_timestamp_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("ts_stamp", p->ts_stamp,o, false)  ;
				JSONAPPEND("ts_delay", p->ts_delay,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct security_agree_parser {
			static stringstream& toJson( sip_security_server_t* p, stringstream& o) {
				o << "[" ;
				sip_security_server_t* ss = p ;
				do {
					if( ss != p ) o << "," ;
					o << "{" ;
					JSONAPPEND("msec", p->sa_mec, o, false) ;
					JSONAPPEND("q", p->sa_q, o) ;
					JSONAPPEND("d_alg", p->sa_d_alg, o) ;
					JSONAPPEND("d_qop", p->sa_d_qop, o) ;
					JSONAPPEND("d_ver", p->sa_d_ver, o) ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( p->sa_params, o) ;			
					o << "}" ; 
				} while( (ss = ss->sa_next) ) ; 
				o << "]" ;
				return o ;				
			}
		} ;
		struct privacy_parser {
			static stringstream& toJson( sip_privacy_t* p, stringstream& o) {
				generic_msg_params_parser::toJson( p->priv_values, o) ;			
				return o ;				
			}
		} ;
		struct etag_parser {
			static stringstream& toJson( sip_etag_t* p, stringstream& o) {
				o << "\"" << p->g_string << "\"" ;
				return o ;
			}
		} ;
		struct if_match_parser {
			static stringstream& toJson( sip_if_match_t* p, stringstream& o) {
				o << "\"" << p->g_string << "\"" ;
				return o ;				
			}
		} ;
		struct mime_version_parser {
			static stringstream& toJson( sip_mime_version_t* p, stringstream& o) {
				o << "\"" << p->g_string << "\"" ;
				return o ;				
			}
		} ;
		struct content_encoding_parser {
			static stringstream& toJson( sip_content_encoding_t* p, stringstream& o) {
				generic_msg_list_parser::toJson( p, o) ;			
				return o ;				
			}
		} ;
		struct content_language_parser {
			static stringstream& toJson( sip_content_language_t* p, stringstream& o) {
				generic_msg_list_parser::toJson( p, o) ;			
				return o ;				
			}
		} ;
		struct content_disposition_parser {
			static stringstream& toJson( sip_content_disposition_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("type", p->cd_type,o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->cd_params, o) ;			
				JSONAPPEND("handling", p->cd_handling,o)  ;
				JSONAPPEND("required", 1 == p->cd_required,o)  ;
				JSONAPPEND("optional", 1 == p->cd_optional,o)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct proxy_require_parser {
			static stringstream& toJson( sip_proxy_require_t* p, stringstream& o) {
				generic_msg_list_parser::toJson( p, o) ;			
				return o ;
			}
		} ;

		struct subject_parser {
			static stringstream& toJson( sip_subject_t* p, stringstream& o) { return generic_msg_parser::toJson( p, o ) ; }
		} ;
		struct priority_parser {
			static stringstream& toJson( sip_priority_t* p, stringstream& o) { return generic_msg_parser::toJson( p, o ) ; }
		} ;
		struct call_info_parser {
			static stringstream& toJson( sip_call_info_t* p, stringstream& o) {
				o << "[" ;
				sip_call_info_t* ci = p ;
				do {
					if( ci != p ) o << ", " ;
					o << "{" ;
					o << "\"url\": "  ;
					url_parser::toJson( ci->ci_url, o ) ;
					o << "\"params\":" ;
					generic_msg_params_parser::toJson( ci->ci_params, o) ;
					JSONAPPEND("purpose",ci->ci_purpose,o) ;
					o << "}" ;
				} while( (ci = ci->ci_next ) ) ;
				o << "]" ;
				return o ;		
			}
		} ;
		struct organization_parser {
			static stringstream& toJson( sip_organization_t* p, stringstream& o) { return generic_msg_parser::toJson( p, o ) ; }
		} ;
		struct server_parser {
			static stringstream& toJson( sip_server_t* p, stringstream& o) { return generic_msg_parser::toJson( p, o ) ; }
		} ;
		struct user_agent_parser {
			static stringstream& toJson( sip_user_agent_t* p, stringstream& o) { return generic_msg_parser::toJson( p, o ) ; }
		} ;
		struct in_reply_to_parser {
			static stringstream& toJson( sip_in_reply_to_t* p, stringstream& o) { return generic_msg_list_parser::toJson( p, o ) ; }
		} ;
		struct accept_parser {
			static stringstream& toJson( sip_accept_t* p, stringstream& o) {
				o << "[" ;
				sip_accept_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					JSONAPPEND("type",a->ac_type,o, false) ;
					JSONAPPEND("subtype",a->ac_subtype,o) ;
					JSONAPPEND("q",a->ac_q,o) ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( a->ac_params, o) ;
					o << "}" ;
				} while( (a = a->ac_next ) ) ;
				o << "]" ;
				return o ;		
			}
		} ;
		struct accept_encoding_parser {
			static stringstream& toJson( sip_accept_encoding_t* p, stringstream& o) {
				o << "[" ;
				sip_accept_encoding_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					JSONAPPEND("value",a->aa_value,o, false) ;
					JSONAPPEND("q",a->aa_q,o) ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( a->aa_params, o) ;
					o << "}" ;
				} while( (a = a->aa_next ) ) ;
				o << "]" ;
				return o ;		
			}
		} ;
		struct accept_language_parser {
			static stringstream& toJson( sip_accept_language_t* p, stringstream& o) {
				o << "[" ;
				sip_accept_language_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					JSONAPPEND("value",a->aa_value,o, false) ;
					JSONAPPEND("q",a->aa_q,o) ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( a->aa_params, o) ;
					o << "}" ;
				} while( (a = a->aa_next ) ) ;
				o << "]" ;
				return o ;						
			}
		} ;
		struct allow_parser {
			static stringstream& toJson( sip_allow_t* p, stringstream& o) {
				o << "[" ;
				sip_allow_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					generic_msg_params_parser::toJson( a->k_items, o) ;
				} while( 0 /* (a = a->aa_next ) */ ) ;
				o << "]" ;
				return o ;										
			}
		} ;
		struct supported_parser {
			static stringstream& toJson( sip_supported_t* p, stringstream& o) { return generic_msg_list_parser::toJson( p, o ) ; }
		} ;
		struct unsupported_parser {
			static stringstream& toJson( sip_unsupported_t* p, stringstream& o) { return generic_msg_list_parser::toJson( p, o ) ; }
		} ;
		struct require_parser {
			static stringstream& toJson( sip_require_t* p, stringstream& o) { return generic_msg_list_parser::toJson( p, o ) ; }
		} ;
		struct allow_events_parser {
			static stringstream& toJson( sip_allow_events_t* p, stringstream& o) { return generic_msg_list_parser::toJson( p, o ) ; }
		} ;
		struct proxy_authenticate_parser {
			static stringstream& toJson( msg_auth_t* p, stringstream& o) {
				o << "[" ;
				msg_auth_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					JSONAPPEND("scheme",a->au_scheme,o, false) ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( a->au_params, o) ;
					o << "}" ;
				} while( (a = a->au_next ) ) ;
				o << "]" ;
				return o ;		
			}
		} ;
		struct proxy_authentication_info_parser {
			static stringstream& toJson( msg_auth_info_t* p, stringstream& o) {
				generic_msg_params_parser::toJson( p->ai_params, o) ;
				return o ;		
			}
		} ;
		struct authentication_info_parser {
			static stringstream& toJson( sip_authentication_info_t* p, stringstream& o) {
				generic_msg_params_parser::toJson( p->ai_params, o) ;
				return o ;		
			}
		} ;
		struct proxy_authorization_parser {
			static stringstream& toJson( sip_proxy_authorization_t* p, stringstream& o) {
				return proxy_authenticate_parser::toJson( p, o ) ;
			}
		} ;
		struct authorization_parser {
			static stringstream& toJson( sip_authorization_t* p, stringstream& o) {
				return proxy_authorization_parser::toJson( p, o ) ;
			}
		} ;
		struct www_authenticate_parser {
			static stringstream& toJson( msg_auth_t* p, stringstream& o) {
				return proxy_authenticate_parser::toJson( p, o ) ;
			}
		} ;

		struct warning_parser {
			static stringstream& toJson( sip_warning_t* p, stringstream& o) {
				o << "[" ;
				sip_warning_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					JSONAPPEND("host",a->w_host,o, false) ;
					JSONAPPEND("port",a->w_port,o) ;
					JSONAPPEND("text",a->w_text,o) ;
					JSONAPPEND("code",a->w_code,o) ;
					o << "}" ;
				} while( (a = a->w_next ) ) ;
				o << "]" ;
				return o ;										
			}
		} ;
		struct path_parser {
			static stringstream& toJson( sip_path_t* p, stringstream& o) { return route_parser::toJson( p, o ); }
		} ;
		struct service_route_parser {
			static stringstream& toJson( sip_service_route_t* p, stringstream& o) { return route_parser::toJson( p, o ); }
		} ;

		struct refer_sub_parser {
			static stringstream& toJson( sip_refer_sub_t* p, stringstream& o) {
				o << "{" ;
				JSONAPPEND("value", 0 == strcmp(p->rs_value,"true"),o, false)  ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->rs_params, o) ;			
				o << "}" ;
				return o ;				
			}
		} ;
		struct alert_info_parser {
			static stringstream& toJson( sip_alert_info_t* p, stringstream& o ) {
				o << "[" ;
				sip_alert_info_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					o << "\"url\": "  ;
					url_parser::toJson( p->ai_url, o ) ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( p->ai_params, o) ;			
					o << "}" ;
				} while( (a = a->ai_next ) ) ;
				o << "]" ;
				return o ;														
			}
		} ;
		struct reply_to_parser {
			static stringstream& toJson( sip_reply_to_t* p, stringstream& o ) {
				o << "{" ;
				o << "\"url\": "  ;
				url_parser::toJson( p->rplyto_url, o ) ;
				o << ",\"params\": " ;
				generic_msg_params_parser::toJson( p->rplyto_params, o) ;			
				o << "}" ;
				return o ;				
			}
		} ;
		struct suppress_body_if_match_parser {
			static stringstream& toJson( sip_suppress_body_if_match_t* p, stringstream& o ) {
				o << "{" ;
				JSONAPPEND("tag", p->sbim_tag,o, false)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct suppress_notify_if_match_parser {
			static stringstream& toJson( sip_suppress_notify_if_match_t* p, stringstream& o ) {
				o << "{" ;
				JSONAPPEND("tag", p->snim_tag,o, false)  ;
				o << "}" ;
				return o ;				
			}
		} ;
		struct p_asserted_identity_parser {
			static stringstream& toJson( sip_p_asserted_identity_t* p, stringstream& o ) {
				o << "[" ;
				sip_p_asserted_identity_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					o << "\"url\": "  ;
					url_parser::toJson( p->paid_url, o ) ;
					JSONAPPEND("display", p->paid_display,o)  ;
					o << "}" ;
				} while( (a = a->paid_next ) ) ;
				o << "]" ;
				return o ;														
			}
		} ;
		struct p_preferred_identity_parser {
			static stringstream& toJson( sip_p_preferred_identity_t* p, stringstream& o ) {
				o << "[" ;
				sip_p_preferred_identity_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					o << "\"url\": "  ;
					url_parser::toJson( p->ppid_url, o ) ;
					JSONAPPEND("display", p->ppid_display,o)  ;
					o << "}" ;
				} while( (a = a->ppid_next ) ) ;
				o << "]" ;
				return o ;														
			}
		} ;
		struct remote_party_id_parser {
			static stringstream& toJson( sip_remote_party_id_t* p, stringstream& o ) {
				o << "[" ;
				sip_remote_party_id_t* a = p ;
				do {
					if( a != p ) o << ", " ;
					o << "{" ;
					o << "\"url\": "  ;
					url_parser::toJson( p->rpid_url, o ) ;
					JSONAPPEND("display", p->rpid_display,o)  ;
					o << ",\"params\": " ;
					generic_msg_params_parser::toJson( p->rpid_params, o) ;		
					JSONAPPEND("screen", p->rpid_screen, o);	
					JSONAPPEND("party", p->rpid_party, o);	
					JSONAPPEND("type", p->rpid_id_type, o);	
					JSONAPPEND("privacy", p->rpid_privacy, o);	
					o << "}" ;
				} while( (a = a->rpid_next ) ) ;
				o << "]" ;
				return o ;														
			}
		} ;
		struct payload_parser {
			static stringstream& toJson( sip_payload_t* p, stringstream& o) {
				const std::string doublequote("\"");
				const std::string slashquote("\\\"");
				string payload( p->pl_data, p->pl_len ) ;
				boost::replace_all( payload, "\r\n","\n") ;
				boost::replace_all( payload, doublequote, slashquote) ;
				o << "\"" << payload << "\"" ;
				return o ;				
			}
		} ;


	protected:

	private:
		SofiaMsg() ;

		string	m_strMsg ;

	} ;


}


#endif 