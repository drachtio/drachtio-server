#include "sofia-msg.hpp"


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
		o << "\"" << value << "\""  ; 
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
		o << "\"" << value << "\""  ; 
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

		if( sip->sip_route ) {
			o << ",\"route\": "; 
			route_parser::toJson( sip->sip_route, o) ;
		}
		if( sip->sip_record_route ) {
			o << ",\"record_route\": "; 
			route_parser::toJson( sip->sip_record_route, o) ;
		}
		if( sip->sip_max_forwards ) {
			o << ",\"max_forwards\": "; 
			max_forwards_parser::toJson( sip->sip_max_forwards, o) ;
		}
		if( sip->sip_proxy_require ) {
			o << ",\"proxy_require\": "; 
			generic_msg_list_parser::toJson( sip->sip_proxy_require, o) ;			
		}
		if( sip->sip_from ) {
			o << ",\"from\": "; 
			addr_parser::toJson( sip->sip_from, o) ;			
		}
		if( sip->sip_to ) {
			o << ",\"to\": "; 
			addr_parser::toJson( sip->sip_to, o) ;			
		}
		if( sip->sip_call_id ) {
			o << ",\"call_id\": "; 
			call_id_parser::toJson( sip->sip_call_id, o) ;			
		}
		if( sip->sip_cseq ) {
			o << ",\"cseq\": "; 
			cseq_parser::toJson( sip->sip_cseq, o) ;			
		}
		if( sip->sip_contact ) {
			o << ",\"contact\": "; 
			contact_parser::toJson( sip->sip_contact, o) ;			
		}




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
	stringstream& SofiaMsg::max_forwards_parser::toJson( sip_max_forwards_t* mf, stringstream& o ) {
		o << "{" ;
		JSONAPPEND("count", mf->mf_count, o, false ) ;
		o << "}" ;
		return o ;
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
		//o << "{" ;
		o << "\"" << cid->i_id << "\"" ;
		//JSONAPPEND("id", cid->i_id,o, false) ;
		//o << "}" ;
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
}