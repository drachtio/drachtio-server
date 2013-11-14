#include "sofia-msg.hpp"
#include <boost/algorithm/string.hpp>


#define JSON_SIP_HDR( name, o ) \
do \
	if( sip->sip_ ## name ) { \
			o << ",\"" << #name << "\": " ; \
			name ## _parser::toJson( sip->sip_ ## name, o) ; \
	} \
while(0); 


namespace {
	inline stringstream& JSONAPPEND( const char* szName, const char* szValue, stringstream& o, bool comma = true ) {
		if( szValue ) {
			if( comma ) o << ",\"" << szName << "\": " ; 
			else o << "\"" << szName << "\": " ; 
			o << "\"" << szValue << "\""  ; 
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
		o << "\"" << value << "\""  ; 
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

	SofiaMsg::SofiaMsg( sip_t const *sip ) {
		stringstream o ;

		o << "{" ;

		o << "\"request_uri\": ";
		request_parser::toJson( sip->sip_request, o)  ;

		/* start of headers */
		o << ",\"headers\": {" ;
		o << "\"via\": " ;
		via_parser::toJson( sip->sip_via, o) ;

		JSON_SIP_HDR(from, o)
		JSON_SIP_HDR(to, o)
		JSON_SIP_HDR(call_id, o)
		JSON_SIP_HDR(cseq, o)
		JSON_SIP_HDR(contact, o)
		JSON_SIP_HDR(route, o)
		JSON_SIP_HDR(record_route, o)
		JSON_SIP_HDR(max_forwards, o)
		JSON_SIP_HDR(proxy_require, o)
		JSON_SIP_HDR(content_type, o)
		JSON_SIP_HDR(date, o)
		JSON_SIP_HDR(event, o)
		JSON_SIP_HDR(error_info, o)
		JSON_SIP_HDR(expires, o)
		JSON_SIP_HDR(rack, o)
		JSON_SIP_HDR(min_expires, o)
		JSON_SIP_HDR(refer_to, o)
		JSON_SIP_HDR(referred_by, o)
		JSON_SIP_HDR(replaces, o)
		JSON_SIP_HDR(retry_after, o)
		JSON_SIP_HDR(request_disposition, o)
		JSON_SIP_HDR(reason, o)
		JSON_SIP_HDR(session_expires, o)
		JSON_SIP_HDR(min_se, o)
		JSON_SIP_HDR(subscription_state, o)
		JSON_SIP_HDR(privacy, o)
		JSON_SIP_HDR(timestamp, o)
		JSON_SIP_HDR(etag, o)
		JSON_SIP_HDR(if_match, o)
		JSON_SIP_HDR(mime_version, o)
		JSON_SIP_HDR(content_encoding, o)
		JSON_SIP_HDR(content_language, o)
		JSON_SIP_HDR(content_disposition, o)
		JSON_SIP_HDR(proxy_require, o)

		/* no such header exists in sip_s
		if( sip->caller_prefs ) {
			o << ",\"caller_prefs\": " ;
			caller_prefs_parser::toJson( sip->caller_prefs, o ) ;
		}
		*/

		/* special cases that the macro doesn't cover */
		if( sip->sip_security_server ) {
			o << ",\"security_server\": " ;
			security_agree_parser::toJson( sip->sip_security_server, o) ;
		}
		if( sip->sip_security_client ) {
			o << ",\"security_client\": " ;
			security_agree_parser::toJson( sip->sip_security_client, o) ;
		}
		if( sip->sip_security_verify ) {
			o << ",\"security_verify\": " ;
			security_agree_parser::toJson( sip->sip_security_verify, o) ;
		}


		JSON_SIP_HDR(content_length, o)
		JSON_SIP_HDR(payload, o)



		o << "}" ;
		/* end of headers */

		o << "}" ;
		m_strMsg = o.str() ;

	}
	SofiaMsg::~SofiaMsg() {

	}

	stringstream& SofiaMsg::request_parser::toJson( sip_request_t* req, stringstream& o) {
		o << "{" ;
		JSONAPPEND("method",req->rq_method_name,o, false) ;
		JSONAPPEND("version",req->rq_version,o)  ;
		o << ",\"url\": "  ;
		url_parser::toJson( req->rq_url, o ) ;
		o << "}" ;
		return o ;
	}

	stringstream& SofiaMsg::url_parser::toJson( url_t* url, stringstream& o ) {
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

	stringstream& SofiaMsg::status_parser::toJson( sip_status_t* st, stringstream& o) {
		o << "{" ;
		JSONAPPEND("version", st->st_version, o, false) ;
		JSONAPPEND("status", st->st_status, o)   ;
		JSONAPPEND("phrase", st->st_phrase, o)  ;
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::via_parser::toJson( sip_via_t* via, stringstream& o ) {
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
	stringstream& SofiaMsg::route_parser::toJson( sip_route_t* route, stringstream& o ) {
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
	stringstream& SofiaMsg::record_route_parser::toJson( sip_record_route_t* rroute, stringstream& o ) {
		return route_parser::toJson( rroute, o) ;
	}
	stringstream& SofiaMsg::max_forwards_parser::toJson( sip_max_forwards_t* mf, stringstream& o ) {
		o << mf->mf_count ;
		return o ;
	}
	stringstream& SofiaMsg::to_parser::toJson( sip_to_t* addr, stringstream& o) {
		return addr_parser::toJson( addr, o ) ;
	}
	stringstream& SofiaMsg::from_parser::toJson( sip_from_t* addr, stringstream& o) {
		return addr_parser::toJson( addr, o ) ;
	}
	stringstream& SofiaMsg::addr_parser::toJson( sip_addr_t* addr, stringstream& o) {
		o << "{" ;
		JSONAPPEND("display", addr->a_display,o, false)  ;
		JSONAPPEND("comment", addr->a_comment,o)  ;
		JSONAPPEND("tag", addr->a_tag,o) ;
		o << ",\"url\": "  ;
		url_parser::toJson( addr->a_url, o ) ;
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
	stringstream& SofiaMsg::contact_parser::toJson( sip_contact_t* c, stringstream& o) {
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
	stringstream& SofiaMsg::call_id_parser::toJson( sip_call_id_t* cid, stringstream& o) {
		o << "\"" << cid->i_id << "\"" ;
		return o ;
	}
	stringstream& SofiaMsg::cseq_parser::toJson( sip_cseq_t* cseq, stringstream& o) {
		o << "{" ;
		JSONAPPEND("seq", cseq->cs_seq,o, false)  ;
		JSONAPPEND("method", cseq->cs_method_name,o) ;
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::generic_msg_list_parser::toJson( msg_list_t* list, stringstream& o)  {
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
	stringstream& SofiaMsg::generic_msg_params_parser::toJson( const msg_param_t* params, stringstream& o) {
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

	stringstream& SofiaMsg::content_length_parser::toJson( sip_content_length_t* addr, stringstream& o) {
		o <<  addr->l_length  ;
		return o ;
	}
	stringstream& SofiaMsg::content_type_parser::toJson( sip_content_type_t* ct, stringstream& o) {
		o << "{" ;
		JSONAPPEND("type", ct->c_type,o, false)  ;
		JSONAPPEND("subtype", ct->c_subtype,o) ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( ct->c_params, o) ;			
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::date_parser::toJson( sip_date_t* p, stringstream& o) {
		o << p->d_time  ;
		return o ;
	}
	stringstream& SofiaMsg::event_parser::toJson( sip_event_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("type", p->o_type,o, false)  ;
		JSONAPPEND("id", p->o_id,o) ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( p->o_params, o) ;			
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::error_info_parser::toJson( sip_error_info_t* p, stringstream& o) {
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
	stringstream& SofiaMsg::expires_parser::toJson( sip_expires_t* p, stringstream& o) {
		o << "\"" << p->ex_date << "\"" ;
		return o ;
	}
	stringstream& SofiaMsg::min_expires_parser::toJson( sip_min_expires_t* p, stringstream& o) {
		o << "\"" << p->me_delta << "\"" ;
		return o ;
	}
	stringstream& SofiaMsg::rack_parser::toJson( sip_rack_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("method", p->ra_method_name,o, false)  ;
		JSONAPPEND("response", p->ra_response,o) ;
		JSONAPPEND("cseq", p->ra_cseq,o) ;
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::refer_to_parser::toJson( sip_refer_to_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("display", p->r_display,o, false)  ;
		o << ",\"url\": "  ;
		url_parser::toJson( p->r_url, o ) ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( p->r_params, o) ;			
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::referred_by_parser::toJson( sip_referred_by_t* p, stringstream& o) {
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
	stringstream& SofiaMsg::replaces_parser::toJson( sip_replaces_t* p, stringstream& o) {
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
	stringstream& SofiaMsg::retry_after_parser::toJson( sip_retry_after_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("delta", p->af_delta,o, false)  ;
		JSONAPPEND("comment", p->af_comment,o)  ;
		JSONAPPEND("duration", p->af_duration,o)  ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( p->af_params, o) ;			
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::request_disposition_parser::toJson( sip_request_disposition_t* p, stringstream& o) {
		generic_msg_params_parser::toJson( p->rd_items, o) ;			
		return o ;
	}
	stringstream& SofiaMsg::caller_prefs_parser::toJson( sip_caller_prefs_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("q", p->cp_q,o, false)  ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( p->cp_params, o) ;			
		JSONAPPEND("require", 1 == p->cp_require,o)  ;
		JSONAPPEND("explicit", 1 == p->cp_explicit,o)  ;
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::reason_parser::toJson( sip_reason_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("protocol", p->re_protocol,o, false)  ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( p->re_params, o) ;			
		JSONAPPEND("cause", p->re_cause,o)  ;
		JSONAPPEND("text", p->re_text,o)  ;
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::session_expires_parser::toJson( sip_session_expires_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("delta", p->x_delta,o, false)  ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( p->x_params, o) ;			
		JSONAPPEND("refresher", p->x_refresher,o)  ;
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::min_se_parser::toJson( sip_min_se_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("delta", p->min_delta,o, false)  ;
		o << ",\"params\": " ;
		generic_msg_params_parser::toJson( p->min_params, o) ;			
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::subscription_state_parser::toJson( sip_subscription_state_t* p, stringstream& o) {
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
	stringstream& SofiaMsg::timestamp_parser::toJson( sip_timestamp_t* p, stringstream& o) {
		o << "{" ;
		JSONAPPEND("ts_stamp", p->ts_stamp,o, false)  ;
		JSONAPPEND("ts_delay", p->ts_delay,o)  ;
		o << "}" ;
		return o ;
	}
	stringstream& SofiaMsg::security_agree_parser::toJson( sip_security_server_t* p, stringstream& o) {
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
	stringstream& SofiaMsg::privacy_parser::toJson( sip_privacy_t* p, stringstream& o) {
		generic_msg_params_parser::toJson( p->priv_values, o) ;			
		return o ;
	}
	stringstream& SofiaMsg::etag_parser::toJson( sip_etag_t* p, stringstream& o) {
		o << "\"" << p->g_string << "\"" ;
		return o ;
	}
	stringstream& SofiaMsg::if_match_parser::toJson( sip_if_match_t* p, stringstream& o) {
		o << "\"" << p->g_string << "\"" ;
		return o ;
	}
	stringstream& SofiaMsg::mime_version_parser::toJson( sip_mime_version_t* p, stringstream& o) {
		o << "\"" << p->g_string << "\"" ;
		return o ;
	}
	stringstream& SofiaMsg::content_encoding_parser::toJson( sip_content_encoding_t* p, stringstream& o) {
		generic_msg_list_parser::toJson( p, o) ;			
		return o ;
	}
	stringstream& SofiaMsg::proxy_require_parser::toJson( sip_proxy_require_t* p, stringstream& o) {
		generic_msg_list_parser::toJson( p, o) ;			
		return o ;
	}
	stringstream& SofiaMsg::content_language_parser::toJson( sip_content_language_t* p, stringstream& o) {
		generic_msg_list_parser::toJson( p, o) ;			
		return o ;
	}
	stringstream& SofiaMsg::content_disposition_parser::toJson( sip_content_disposition_t* p, stringstream& o) {
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
	stringstream& SofiaMsg::payload_parser::toJson( sip_payload_t* p, stringstream& o) {
		string payload( p->pl_data, p->pl_len ) ;
		boost::replace_all( payload, "\r\n","\n") ;
		o << "\"" << payload << "\"" ;
		return o ;
	}
	

}