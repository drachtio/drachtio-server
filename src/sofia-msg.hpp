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

#include <jansson.h>

#include "drachtio.h"

using namespace std ;

namespace {
	const std::string doublequote("\"");
	const std::string slashquote("\\\"");
} ;

namespace drachtio {
	const std::string doublequote("\"");
	const std::string slashquote("\\\"");
	
	class SofiaMsg {
	public:
		SofiaMsg(  nta_incoming_t* irq, sip_t const *sip ) ;
		SofiaMsg(  nta_outgoing_t* orq, sip_t const *sip ) ;
		~SofiaMsg() {
			if( m_json ) json_decref( m_json ) ;
		}

		void populateHeaders( sip_t const *sip, json_t* json ) ;

		json_t* value(void) const { assert(m_json); return m_json; }
		json_t* detach(void) { 
			assert(m_json); 
			json_t* json = m_json ;
			m_json = NULL ;
			return json ;
		}

		bool str(string& str) const { 
			if( !m_json ) return false ;

			char* c = json_dumps( m_json, JSON_COMPACT | JSON_SORT_KEYS | JSON_ENCODE_ANY) ;
			assert( c != NULL ) ;
			if( NULL == c ) return false ;

			str.assign( c ) ;
#ifdef DEBUG
			my_json_free(c) ;		
#else
			free( c ) ;
#endif
			return true; 
		}

		struct generic_msg_parser {
			static json_t* 	toJson( msg_generic_t* g ) {
				json_t* json = json_array() ;
				msg_generic_t* p = g ;
				do {
					json_array_append_new( json, json_string( p->g_string) ) ;
					p = p->g_next ;
				} while( p ) ;
				return json ;
			}
		} ;
		struct generic_msg_list_parser {
			static json_t* toJson( msg_list_t* list ) {
				json_t* json = json_array() ;			
				msg_list_t* l = list ;
				do {
					if( l->k_items ) {
						json_t* array = json_array() ;			
						for (const msg_param_t* p = l->k_items; p && *p; p++) {
							json_array_append_new( array, json_string( *p ) ) ;
						}
						json_array_append_new( json, array ) ;
					}
				} while( (l = l->k_next) ) ;
				return json ;
			}
		} ;
		struct generic_msg_params_parser {
			static json_t* toJson( const msg_param_t* params ) {
				json_t* json = json_array() ;			
				if( params ) {
					for (const msg_param_t* p = params; p && *p; p++) {
						string val = *p ;
						boost::replace_all( val, doublequote, slashquote) ;
						json_array_append_new( json, json_string( val.c_str() ) ) ;
					}			
				}
				return json ;					
			}
		} ;


		struct url_parser {
			static json_t* toJson( url_t* url ) {
				json_t* json = json_object() ;
				if( url->url_scheme ) json_object_set_new_nocheck( json, "scheme", json_string( url->url_scheme ) ) ;
				if( url->url_user ) json_object_set_new_nocheck( json, "user", json_string( url->url_user ) ) ;
				if( url->url_password ) json_object_set_new_nocheck( json, "password", json_string( url->url_password ) ) ;
				if( url->url_host ) json_object_set_new_nocheck( json, "host", json_string( url->url_host ) );
				if( url->url_port ) json_object_set_new_nocheck( json, "port", json_string( url->url_port ) );
				if( url->url_path ) json_object_set_new_nocheck( json, "path", json_string( url->url_path ) );

				if( url->url_params ) json_object_set_new_nocheck( json, "params",json_string( url->url_params ) );
				if( url->url_headers ) json_object_set_new_nocheck( json, "headers", json_string( url->url_headers ) );

				if( url->url_fragment ) json_object_set_new_nocheck( json, "fragment", json_string( url->url_fragment ) );
				return json ;
			}
		} ;
		struct request_parser {
			static json_t* toJson( sip_request_t* req ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck( json, "method", json_string( req->rq_method_name ) ) ;
				json_object_set_new_nocheck( json, "version", json_string( req->rq_version ) ) ;
				json_object_set_new_nocheck( json, "url", url_parser::toJson( req->rq_url ) ) ;
				return json ;
			}
		} ;
		struct status_parser {
			static json_t* toJson( sip_status_t* st ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck( json, "version", json_string(  st->st_version ) ) ;
				json_object_set_new_nocheck( json, "status", json_integer(  st->st_status ) ) ;
				json_object_set_new_nocheck( json, "phrase", json_string(  st->st_phrase ) ) ;
				return json;				
			}
		} ;
		struct via_parser {
			static json_t* toJson( sip_via_t* via ) {
				json_t* json = json_array() ;
				sip_via_t* v = via ;
				do {
					json_t* obj = json_object() ;
					json_object_set_new_nocheck( obj, "protocol", json_string(  via->v_protocol ) ) ;
					json_object_set_new_nocheck( obj, "host", json_string(  via->v_host ) ) ;
					if( via->v_port ) json_object_set_new_nocheck( obj, "port", json_string(  via->v_port ) ) ;
					if( via->v_comment ) json_object_set_new_nocheck( obj, "comment", json_string(  via->v_comment ) ) ;
					if( via->v_ttl) json_object_set_new_nocheck( obj, "ttl", json_string(  via->v_ttl ) ) ;
					if( via->v_maddr) json_object_set_new_nocheck( obj, "maddr", json_string(  via->v_maddr ) ) ;
					if( via->v_received) json_object_set_new_nocheck( obj, "received", json_string(  via->v_received ) ) ;
					if( via->v_branch ) json_object_set_new_nocheck( obj, "branch", json_string(  via->v_branch ) ) ;
					if( via->v_rport) json_object_set_new_nocheck( obj, "rport", json_string(  via->v_rport ) ) ;
					if( via->v_comp) json_object_set_new_nocheck( obj, "comp", json_string(  via->v_comp ) ) ;

					json_t* array = json_array() ;
					if (via->v_params) {
						for (const msg_param_t* p = v->v_params; *p; p++) {
							json_array_append_new( array, json_string( *p ) ) ;
						}
					}
					json_object_set_new_nocheck( obj, "params", array ) ;
					json_array_append_new( json, obj ) ;
				} while( (v = via->v_next) ) ;
				return json ;				
			}
		} ;
		struct route_parser {
			static json_t* toJson( sip_route_t* route ) {
				json_t* json = json_array() ;
				sip_route_t* r = route ;
				do {
					json_t* obj = json_object() ;
					if( r->r_display ) json_object_set_new_nocheck(obj, "display", json_string(r->r_display)) ;
					json_object_set_new_nocheck(obj, "url", url_parser::toJson( r->r_url )) ;
					json_array_append_new( json, obj) ;
				} while( (r = r->r_next ) ) ;
				return json ;
			}
		} ;
		struct record_route_parser {
			static json_t* toJson( sip_record_route_t* rroute ) {
				return route_parser::toJson( rroute ) ;
			}
		} ;
		struct max_forwards_parser {
			static json_t* toJson( sip_max_forwards_t* mf ) {
				json_t* json = json_integer( mf->mf_count ) ;
				return json ;
			}
		} ;
		struct call_id_parser {
			static json_t* toJson( sip_call_id_t* cid ) {
				json_t* json = json_string( cid->i_id ) ;
				return json ;
			}
		} ;
		struct cseq_parser {
			static json_t* toJson( sip_cseq_t* cseq ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck( json, "seq", json_integer(cseq->cs_seq)) ;
				json_object_set_new_nocheck( json, "method", json_string(cseq->cs_method_name)) ;
				return json ;
			}
		} ;
		struct addr_parser {
			static json_t* toJson( sip_addr_t* addr ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck(json,"url",url_parser::toJson(addr->a_url)) ;
				if(addr->a_display) json_object_set_new_nocheck(json,"display", json_string(addr->a_display)) ;
				if(addr->a_comment) json_object_set_new_nocheck(json,"comment", json_string(addr->a_comment)) ;
				if(addr->a_tag) json_object_set_new_nocheck(json,"tag", json_string(addr->a_tag)) ;
				json_t* array = json_array() ;
				if( addr->a_params ) {
					for (const msg_param_t* p = addr->a_params; *p; p++) {
						json_array_append_new(array,json_string(*p)) ;
					}			
				}
				json_object_set_new_nocheck(json, "params", array) ;
				return json ;				
			}
		} ;
		struct from_parser {
			static json_t* toJson( sip_from_t* addr ) {
				return addr_parser::toJson( addr ) ;
			}
		} ;
		struct to_parser {
			static json_t* toJson( sip_to_t* addr ) {
				return addr_parser::toJson( addr ) ;
			}
		} ;
		struct contact_parser {
			static json_t* toJson( sip_contact_t* c ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck(json,"url", url_parser::toJson(c->m_url)) ;
				if(c->m_display ) json_object_set_new_nocheck(json,"display",json_string(c->m_display)) ;
				if(c->m_comment ) json_object_set_new_nocheck(json,"comment",json_string(c->m_comment)) ;
				if(c->m_q ) json_object_set_new_nocheck(json,"q",json_string(c->m_q)) ;
				if(c->m_expires ) json_object_set_new_nocheck(json,"expires",json_string(c->m_expires)) ;
				json_t* array = json_array() ;
				if( c->m_params ) {
					for (const msg_param_t* p = c->m_params; *p; p++ ) {
						json_array_append_new(array,json_string(*p)) ;
					}			
				}
				json_object_set_new_nocheck(json,"params",array) ;
				return json ;				
			}
		} ;
		struct content_length_parser {
			static json_t* toJson( sip_content_length_t* addr ) {
				return json_integer( addr->l_length ) ;
			}
		} ;
		struct content_type_parser {
			static json_t* toJson( sip_content_type_t* ct ) {
				json_t* json = json_object() ;
				if( ct->c_type ) json_object_set_new_nocheck(json,"type",json_string(ct->c_type)) ;
				if( ct->c_subtype ) json_object_set_new_nocheck(json,"subtype",json_string(ct->c_subtype)) ;
				json_object_set_new_nocheck( json, "params", generic_msg_params_parser::toJson( ct->c_params))  ;
				return json ;		
			}
		} ;
		struct date_parser {
			static json_t* toJson( sip_date_t* p ) {
				return json_integer( p->d_time ) ;
			}
		} ;
		struct event_parser {
			static json_t* toJson( sip_event_t* p ) {
				json_t* json = json_object() ;
				if( p->o_type ) json_object_set_new_nocheck(json,"type",json_string(p->o_type)) ;
				if( p->o_id ) json_object_set_new_nocheck(json,"id",json_string(p->o_id)) ;
				json_object_set_new_nocheck( json, "params", generic_msg_params_parser::toJson( p->o_params))  ;
				return json ;				
			}
		} ;
		struct error_info_parser {
			static json_t* toJson( sip_error_info_t* p ) {
				json_t* json = json_array() ;
				sip_error_info_t* ei = p ;
				do {
					json_t* obj = json_object() ;
					json_object_set_new_nocheck(obj,"url",url_parser::toJson( ei->ei_url)) ;
					json_object_set_new_nocheck( obj, "params", generic_msg_params_parser::toJson( ei->ei_params))  ;
					json_array_append_new( json, obj ) ;
				} while( 0 /* (ei = ei->ei_next) */ ) ; //Commented out because of bug in sofia sip.h where ei_next points to a sip_call_info_t for some reason
				return json ;				
			}
		} ;
		struct expires_parser {
			static json_t* toJson( sip_expires_t* p ) {
				json_t* json = json_object() ;
				if( p->ex_date ) json_object_set_new_nocheck(json,"date", json_integer(p->ex_date)) ;
				if( p->ex_delta ) json_object_set_new_nocheck(json,"delta", json_integer(p->ex_delta)) ;
				return json ;				
			}
		} ;
		struct min_expires_parser {
			static json_t* toJson( sip_min_expires_t* p ) {
				return json_integer( p->me_delta ) ;
			}
		} ;
		struct rack_parser {
			static json_t* toJson( sip_rack_t* p ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck(json,"method", json_string(p->ra_method_name)) ;
				json_object_set_new_nocheck(json,"response", json_integer(p->ra_response)) ;
				json_object_set_new_nocheck(json,"cseq", json_integer(p->ra_cseq)) ;
				return json ;				
			}
		} ;
		struct refer_to_parser {
			static json_t* toJson( sip_refer_to_t* p ) {
				json_t* json = json_object() ;
				if( p->r_display ) json_object_set_new_nocheck(json,"display",json_string(p->r_display)) ;
				json_object_set_new_nocheck(json,"url",url_parser::toJson( p->r_url ) ) ;
				json_object_set_new_nocheck(json,"param", generic_msg_params_parser::toJson( p->r_params)) ;
				return json ;				
			}
		} ;
		struct referred_by_parser {
			static json_t* toJson( sip_referred_by_t* p ) {
				json_t* json = json_object() ;
				if( p->b_display ) json_object_set_new_nocheck(json,"display",json_string(p->b_display)) ;
				json_object_set_new_nocheck(json,"url",url_parser::toJson( p->b_url ) ) ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->b_params)) ;
				if( p->b_cid ) json_object_set_new_nocheck(json,"cid", json_string(p->b_cid)) ;
				return json ;				
			}
		} ;
		struct replaces_parser {
			static json_t* toJson( sip_replaces_t* p ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck(json,"call_id",json_string(p->rp_call_id)) ;
				json_object_set_new_nocheck(json,"to_tag",json_string(p->rp_to_tag)) ;
				json_object_set_new_nocheck(json,"from_tag",json_string(p->rp_from_tag)) ;
				json_object_set_new_nocheck(json,"early_only",json_boolean(1 == p->rp_early_only)) ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->rp_params)) ;
				return json ;				
			}
		} ;
		struct retry_after_parser {
			static json_t* toJson( sip_retry_after_t* p ) {
				json_t* json = json_object() ;
				if( p->af_delta ) json_object_set_new_nocheck(json,"delta",json_integer(p->af_delta)) ;
				if( p->af_duration ) json_object_set_new_nocheck(json,"duration",json_string(p->af_duration)) ;
				if( p->af_comment ) json_object_set_new_nocheck(json,"comment",json_string(p->af_comment)) ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->af_params)) ;
				return json ;				
			}
		} ;
		struct request_disposition_parser {
			static json_t* toJson( sip_request_disposition_t* p ) {
				return generic_msg_params_parser::toJson( p->rd_items ) ;
			}
		} ;
		struct caller_prefs_parser {
			static json_t* toJson( sip_caller_prefs_t* p ) {
				json_t* json = json_object() ;
				if( p->cp_q ) json_object_set_new_nocheck(json,"q", json_string(p->cp_q)) ;
				if( p->cp_require ) json_object_set_new_nocheck(json,"require", json_integer(p->cp_require)) ;
				if( p->cp_explicit ) json_object_set_new_nocheck(json,"explicit", json_integer(p->cp_explicit)) ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->cp_params)) ;
				return json ;				
			}
		} ;
		struct reason_parser {
			static json_t* toJson( sip_reason_t* p ) {
				json_t* json = json_object();
				if( p->re_protocol ) json_object_set_new_nocheck(json,"protocol",json_string(p->re_protocol)) ;
				if( p->re_cause ) json_object_set_new_nocheck(json,"cause",json_string(p->re_cause)) ;
				if( p->re_text ) json_object_set_new_nocheck(json,"text",json_string(p->re_text)) ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->re_params)) ;
				return json ;				
			}
		} ;
		struct session_expires_parser {

			static json_t* toJson( sip_session_expires_t* p ) {
				json_t* json = json_object() ;
				if( p->x_delta ) json_object_set_new_nocheck(json,"delta",json_integer(p->x_delta)) ;
				if( p->x_refresher ) json_object_set_new_nocheck(json,"refresh",json_string(p->x_refresher)) ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->x_params)) ;
				return json ;				
			}
		} ;
		struct min_se_parser {
			static json_t* toJson( sip_min_se_t* p ) {
				json_t* json = json_object() ;
				if( p->min_delta ) json_object_set_new_nocheck(json,"delta",json_integer(p->min_delta));
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->min_params)) ;
				return json ;				
			}
		} ;
		struct subscription_state_parser {
			static json_t* toJson( sip_subscription_state_t* p ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->ss_params)) ;
				if( p->ss_reason ) json_object_set_new_nocheck(json,"reason",json_string(p->ss_reason)) ;
				if( p->ss_expires ) json_object_set_new_nocheck(json,"expires",json_string(p->ss_expires)) ;
				if( p->ss_retry_after ) json_object_set_new_nocheck(json,"retry_after",json_string(p->ss_retry_after)) ;
				return json;				
			}
		} ;
		struct timestamp_parser {
			static json_t* toJson( sip_timestamp_t* p ) {
				json_t* json = json_object() ;
				if( p->ts_stamp ) json_object_set_new_nocheck(json,"ts_stamp",json_string(p->ts_stamp)) ;
				if( p->ts_delay ) json_object_set_new_nocheck(json,"ts_delay",json_string(p->ts_delay)) ;
				return json ;				
			}
		} ;
		struct security_agree_parser {
			static json_t* toJson( sip_security_server_t* p ) {
				json_t* json = json_array() ;
				sip_security_server_t* ss = p ;
				do {
					json_t* obj = json_object() ;
					if( p->sa_mec ) json_object_set_new_nocheck(obj,"msec",json_string(p->sa_mec)) ;
					if( p->sa_q ) json_object_set_new_nocheck(obj,"msec",json_string(p->sa_q)) ;
					if( p->sa_d_alg ) json_object_set_new_nocheck(obj,"msec",json_string(p->sa_d_alg)) ;
					if( p->sa_d_qop ) json_object_set_new_nocheck(obj,"msec",json_string(p->sa_d_qop)) ;
					if( p->sa_d_ver ) json_object_set_new_nocheck(obj,"msec",json_string(p->sa_d_ver)) ;
					json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->sa_params)) ;
					json_array_append_new( json, obj ) ;
				} while( (ss = ss->sa_next) ) ; 
				return json ;				
			}
		} ;
		struct privacy_parser {
			static json_t* toJson( sip_privacy_t* p) {
				return generic_msg_params_parser::toJson( p->priv_values) ;			
			}
		} ;
		struct etag_parser {
			static json_t* toJson( sip_etag_t* p ) {
				return json_string(p->g_string) ;
			}
		} ;
		struct if_match_parser {
			static json_t* toJson( sip_if_match_t* p ) {
				return json_string(p->g_string) ;
			}
		} ;
		struct mime_version_parser {
			static json_t* toJson( sip_mime_version_t* p ) {
				return json_string(p->g_string) ;
			}
		} ;
		struct content_encoding_parser {
			static json_t* toJson( sip_content_encoding_t* p ) {
				return generic_msg_list_parser::toJson( p ) ;			
			}
		} ;
		struct content_language_parser {
			static json_t* toJson( sip_content_language_t* p ) {
				return generic_msg_list_parser::toJson( p ) ;			
			}
		} ;
		struct content_disposition_parser {
			static json_t* toJson( sip_content_disposition_t* p ) {
				json_t* json = json_object() ;
				if( p->cd_type ) json_object_set_new_nocheck(json,"type",json_string(p->cd_type)) ;
				if( p->cd_handling ) json_object_set_new_nocheck(json,"handling",json_string(p->cd_handling)) ;
				if( p->cd_required ) json_object_set_new_nocheck(json,"required",json_integer(p->cd_required)) ;
				if( p->cd_optional ) json_object_set_new_nocheck(json,"optional",json_integer(p->cd_optional)) ;
				json_object_set_new_nocheck(json,"params", generic_msg_params_parser::toJson( p->cd_params)) ;
				return json ;
			}
		} ;
		struct proxy_require_parser {
			static json_t* toJson( sip_proxy_require_t* p ) {
				return generic_msg_list_parser::toJson( p ) ;			
			}
		} ;

		struct subject_parser {
			static json_t* toJson( sip_subject_t* p ) { return generic_msg_parser::toJson( p ) ; }
		} ;
		struct priority_parser {
			static json_t* toJson( sip_priority_t* p ) { return generic_msg_parser::toJson( p ) ; }
		} ;
		struct call_info_parser {
			static json_t* toJson( sip_call_info_t* p ) {
				json_t* json = json_array() ;
				sip_call_info_t* ci = p ;
				do {
					json_t* obj = json_object() ;
					json_object_set_new_nocheck(obj,"url",url_parser::toJson(ci->ci_url)) ;
					json_object_set_new_nocheck(obj,"params", generic_msg_params_parser::toJson( ci->ci_params ) ) ;
					if( ci->ci_purpose ) json_object_set_new_nocheck(obj,"purpose",json_string(ci->ci_purpose)) ;
					json_array_append_new(json,obj) ;
				} while( (ci = ci->ci_next ) ) ;
				return json ;		
			}
		} ;
		struct organization_parser {
			static json_t* toJson( sip_organization_t* p ) { return generic_msg_parser::toJson( p ) ; }
		} ;
		struct server_parser {
			static json_t* toJson( sip_server_t* p ) { return generic_msg_parser::toJson( p ) ; }
		} ;
		struct user_agent_parser {
			static json_t* toJson( sip_user_agent_t* p ) { return generic_msg_parser::toJson( p ) ; }
		} ;
		struct in_reply_to_parser {
			static json_t* toJson( sip_in_reply_to_t* p ) { return generic_msg_list_parser::toJson( p ) ; }
		} ;
		struct accept_parser {
			static json_t* toJson( sip_accept_t* p ) {
				json_t* json = json_array() ;
				sip_accept_t* a = p ;
				do {
					json_t* obj = json_object() ;
					if( a->ac_type ) json_object_set_new_nocheck(obj,"type",json_string(a->ac_type)) ;
					if( a->ac_subtype ) json_object_set_new_nocheck(obj,"subtype",json_string(a->ac_subtype)) ;
					if( a->ac_q ) json_object_set_new_nocheck(obj,"q",json_string(a->ac_q)) ;
					json_object_set_new_nocheck(obj,"params",generic_msg_params_parser::toJson( a->ac_params)) ;
					json_array_append_new(json,obj) ;
				} while( (a = a->ac_next ) ) ;
				return json ;		
			}
		} ;
		struct accept_encoding_parser {
			static json_t* toJson( sip_accept_encoding_t* p ) {
				json_t* json = json_array() ;
				sip_accept_encoding_t* a = p ;
				do {
					json_t* obj = json_object() ;
					if( a->aa_value ) json_object_set_new_nocheck(obj,"value",json_string(a->aa_value)) ;
					if( a->aa_q ) json_object_set_new_nocheck(obj,"q",json_string(a->aa_q)) ;
					json_object_set_new_nocheck(obj,"params",generic_msg_params_parser::toJson( a->aa_params)) ;
					json_array_append_new(json,obj) ;
				} while( (a = a->aa_next ) ) ;
				return json ;		
			}
		} ;
		struct accept_language_parser {
			static json_t* toJson( sip_accept_language_t* p ) {
				json_t* json = json_array() ;
				sip_accept_language_t* a = p ;
				do {
					json_t* obj = json_object() ;
					if( a->aa_value ) json_object_set_new_nocheck(obj,"value",json_string(a->aa_value)) ;
					if( a->aa_q ) json_object_set_new_nocheck(obj,"q",json_string(a->aa_q)) ;
					json_object_set_new_nocheck(obj,"params",generic_msg_params_parser::toJson( a->aa_params)) ;
					json_array_append_new(json,obj) ;
				} while( (a = a->aa_next ) ) ;
				return json ;						
			}
		} ;
		struct allow_parser {
			static json_t* toJson( sip_allow_t* p ) {
				json_t* json = json_array() ;
				sip_allow_t* a = p ;
				do {
					json_array_append_new(json,generic_msg_params_parser::toJson( a->k_items)) ;
				} while( 0 /* (a = a->aa_next ) */ ) ;
				return json ;										
			}
		} ;
		struct supported_parser {
			static json_t* toJson( sip_supported_t* p ) { return generic_msg_list_parser::toJson( p ) ; }
		} ;
		struct unsupported_parser {
			static json_t* toJson( sip_unsupported_t* p ) { return generic_msg_list_parser::toJson( p ) ; }
		} ;
		struct require_parser {
			static json_t* toJson( sip_require_t* p ) { return generic_msg_list_parser::toJson( p ) ; }
		} ;
		struct allow_events_parser {
			static json_t* toJson( sip_allow_events_t* p ) { return generic_msg_list_parser::toJson( p ) ; }
		} ;
		struct proxy_authenticate_parser {
			static json_t* toJson( msg_auth_t* p ) {
				json_t* json = json_array() ;
				msg_auth_t* a = p ;
				do {
					json_t* obj = json_object() ;
					if( a->au_scheme ) json_object_set_new_nocheck(obj,"scheme",json_string(a->au_scheme)) ;
					json_object_set_new_nocheck(obj,"params",generic_msg_params_parser::toJson( a->au_params)) ;
					json_array_append_new(json,obj) ;
				} while( (a = a->au_next ) ) ;
				return json ;		
			}
		} ;
		struct proxy_authentication_info_parser {
			static json_t* toJson( msg_auth_info_t* p ) {
				return generic_msg_params_parser::toJson( p->ai_params ) ;
			}
		} ;
		struct authentication_info_parser {
			static json_t* toJson( sip_authentication_info_t* p ) {
				return generic_msg_params_parser::toJson( p->ai_params ) ;
			}
		} ;
		struct proxy_authorization_parser {
			static json_t* toJson( sip_proxy_authorization_t* p ) {
				return proxy_authenticate_parser::toJson( p ) ;
			}
		} ;
		struct authorization_parser {
			static json_t* toJson( sip_authorization_t* p ) {
				return proxy_authorization_parser::toJson( p ) ;
			}
		} ;
		struct www_authenticate_parser {
			static json_t* toJson( msg_auth_t* p ) {
				return proxy_authenticate_parser::toJson( p ) ;
			}
		} ;

		struct warning_parser {
			static json_t* toJson( sip_warning_t* p ) {
				json_t* json = json_array() ;
				sip_warning_t* a = p ;
				do {
					json_t* obj = json_object() ;
					if( a->w_host ) json_object_set_new_nocheck(obj,"host",json_string(a->w_host)) ;
					if( a->w_port ) json_object_set_new_nocheck(obj,"port",json_string(a->w_port)) ;
					if( a->w_text ) json_object_set_new_nocheck(obj,"text",json_string(a->w_text)) ;
					if( a->w_code ) json_object_set_new_nocheck(obj,"code",json_integer(a->w_code)) ;
					json_array_append_new(json,obj) ;
				} while( (a = a->w_next ) ) ;
				return json ;										
			}
		} ;
		struct path_parser {
			static json_t* toJson( sip_path_t* p ) { return route_parser::toJson( p ); }
		} ;
		struct service_route_parser {
			static json_t* toJson( sip_service_route_t* p ) { return route_parser::toJson( p ); }
		} ;

		struct refer_sub_parser {
			static json_t* toJson( sip_refer_sub_t* p ) {
				json_t* json = json_object() ;
				json_object_set_new_nocheck(json,"value",json_boolean(0 == strcmp(p->rs_value,"true"))) ;
				json_object_set_new_nocheck(json,"params",generic_msg_params_parser::toJson( p->rs_params)) ;
				return json;				
			}
		} ;
		struct alert_info_parser {
			static json_t* toJson( sip_alert_info_t* p ) {
				json_t* json = json_array() ;
				sip_alert_info_t* a = p ;
				do {
					json_t* obj = json_object() ;
					json_object_set_new_nocheck(obj,"url",url_parser::toJson( p->ai_url)) ;
					json_object_set_new_nocheck(json,"params",generic_msg_params_parser::toJson( p->ai_params)) ;
					json_array_append_new(json,obj);
				} while( (a = a->ai_next ) ) ;
				return json ;														
			}
		} ;
		struct reply_to_parser {
			static json_t* toJson( sip_reply_to_t* p ) {
				json_t* json = json_object();
				json_object_set_new_nocheck(json,"url",url_parser::toJson( p->rplyto_url)) ;
				json_object_set_new_nocheck(json,"params",generic_msg_params_parser::toJson( p->rplyto_params)) ;
				return json ;				
			}
		} ;
		struct suppress_body_if_match_parser {
			static json_t* toJson( sip_suppress_body_if_match_t* p ) {
				json_t* json = json_object();
				if( p->sbim_tag ) json_object_set_new_nocheck(json,"tag",json_string(p->sbim_tag)) ;
				return json ;				
			}
		} ;
		struct suppress_notify_if_match_parser {
			static json_t* toJson( sip_suppress_notify_if_match_t* p ) {
				json_t* json = json_object();
				if( p->snim_tag ) json_object_set_new_nocheck(json,"tag",json_string(p->snim_tag)) ;
				return json ;				
			}
		} ;
		struct p_asserted_identity_parser {
			static json_t* toJson( sip_p_asserted_identity_t* p ) {
				json_t* json = json_array() ;
				sip_p_asserted_identity_t* a = p ;
				do {
					json_t* obj = json_object() ;
					json_object_set_new_nocheck(obj,"url",url_parser::toJson( p->paid_url)) ;
					if( p->paid_display ) json_object_set_new_nocheck(obj, "display",json_string(p->paid_display)) ;
					json_array_append_new(json,obj) ;
				} while( (a = a->paid_next ) ) ;
				return json ;														
			}
		} ;
		struct p_preferred_identity_parser {
			static json_t* toJson( sip_p_preferred_identity_t* p ) {
				json_t* json = json_array() ;
				sip_p_preferred_identity_t* a = p ;
				do {
					json_t* obj = json_object() ;
					json_object_set_new_nocheck(obj,"url",url_parser::toJson( p->ppid_url)) ;
					if( p->ppid_display ) json_object_set_new_nocheck(obj, "display",json_string(p->ppid_display)) ;
					json_array_append_new(json,obj) ;
				} while( (a = a->ppid_next ) ) ;
				return json ;														
			}
		} ;
		struct remote_party_id_parser {
			static json_t* toJson( sip_remote_party_id_t* p ) {
				json_t* json = json_array() ;				
				sip_remote_party_id_t* a = p ;
				do {
					json_t* obj = json_object() ;
					json_object_set_new_nocheck(obj,"url",url_parser::toJson( p->rpid_url)) ;
					if( p->rpid_display ) json_object_set_new_nocheck(obj, "display",json_string(p->rpid_display)) ;
					if( p->rpid_screen ) json_object_set_new_nocheck(obj,"screen",json_string(p->rpid_screen)) ;
					if( p->rpid_party ) json_object_set_new_nocheck(obj,"party",json_string(p->rpid_party)) ;
					if( p->rpid_id_type ) json_object_set_new_nocheck(obj,"type",json_string(p->rpid_id_type)) ;
					if( p->rpid_privacy ) json_object_set_new_nocheck(obj,"privacy",json_string(p->rpid_privacy)) ;
					json_object_set_new_nocheck(obj,"params",generic_msg_params_parser::toJson( p->rpid_params)) ;
					json_array_append_new(json,obj);
				} while( (a = a->rpid_next ) ) ;
				return json ;														
			}
		} ;
		struct payload_parser {
			static json_t* toJson( sip_payload_t* p ) {
				string payload( p->pl_data, p->pl_len ) ;
				boost::replace_all( payload, "\r\n","\n") ;
				boost::replace_all( payload, doublequote, slashquote) ;
				return json_string( payload.c_str() );				
			}
		} ;

	protected:

	private:
		SofiaMsg() ;

		json_t* m_json ;
	} ;
}

#endif 
