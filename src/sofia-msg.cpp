#include "sofia-msg.hpp"
#include <boost/algorithm/string.hpp>

#include "controller.hpp"


#define JSON_SIP_HDR( name, o ) \
do \
	if( sip->sip_ ## name ) { \
			string str = #name ; \
			boost::replace_all(str,"_","-") ; \
			o << ",\"" << str << "\": " ; \
			name ## _parser::toJson( sip->sip_ ## name, o) ; \
	} \
while(0); 

#define JSON_SIP_HDR_EXTRA( name, o ) \
do \
	if( sip_ ## name(sip) ) { \
			string str = #name ; \
			boost::replace_all(str,"_","-") ; \
			o << ",\"" << str << "\": " ; \
			o << ",\"" << #name << "\": " ; \
			name ## _parser::toJson( sip_ ## name(sip) , o) ; \
	} \
while(0); 

namespace drachtio {

	SofiaMsg::SofiaMsg( nta_incoming_t* irq, sip_t const *sip ) {
		stringstream o ;

		o << "{" ;

		string host ;
		unsigned int port ;
		sip_time_t recv = theOneAndOnlyController->getTransactionTime( irq ) ;
		theOneAndOnlyController->getTransactionSender( irq, host, port ) ;

		o << "\"source_address\":\"" << host << "\",\"source_port\":" << port << ",\"time\":" << recv << ", \"source\": \"network\"" ;

		if( sip->sip_request ) {
			o << ",\"request_uri\": ";
			request_parser::toJson( sip->sip_request, o)  ;
		}
		else if( sip->sip_status ) {
			o << ",\"status\": " ;
			status_parser::toJson( sip->sip_status, o) ;
		}

		populateHeaders( sip, o ) ;
		
		m_strMsg = o.str() ;

	}	

	SofiaMsg::SofiaMsg( nta_outgoing_t* orq, sip_t const *sip ) {
		stringstream o ;

		su_time_t tv = su_now() ;

		o << "{\"time\":" << tv.tv_sec << ", \"source\": \"application\"" ;

		if( sip->sip_request ) {
			o << ",\"request_uri\": ";
			request_parser::toJson( sip->sip_request, o)  ;
		}
		else if( sip->sip_status ) {
			o << ",\"status\": " ;
			status_parser::toJson( sip->sip_status, o) ;
		}

		populateHeaders( sip, o ) ;
		
		m_strMsg = o.str() ;

	}	

	void SofiaMsg::populateHeaders( sip_t const *sip, stringstream& o ) {

		JSON_SIP_HDR(payload, o)

		/* start of headers */
		o << ",\"headers\": {" ;
		o << "\"call-id\": " ;
		call_id_parser::toJson( sip->sip_call_id, o) ;

		JSON_SIP_HDR(via, o)
		JSON_SIP_HDR(from, o)
		JSON_SIP_HDR(to, o)
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
		JSON_SIP_HDR(subject, o)
		JSON_SIP_HDR(priority, o)
		JSON_SIP_HDR(call_info, o)
		JSON_SIP_HDR(organization, o)
		JSON_SIP_HDR(server, o)
		JSON_SIP_HDR(user_agent, o)
		JSON_SIP_HDR(in_reply_to, o)
		JSON_SIP_HDR(accept, o)
		JSON_SIP_HDR(accept_encoding, o)
		JSON_SIP_HDR(allow, o)
		JSON_SIP_HDR(supported, o)
		JSON_SIP_HDR(unsupported, o)
		JSON_SIP_HDR(require, o)
		JSON_SIP_HDR(allow_events, o)
		JSON_SIP_HDR(accept_language, o)
		JSON_SIP_HDR(proxy_authenticate, o)
		JSON_SIP_HDR(proxy_authentication_info, o)
		JSON_SIP_HDR(proxy_authorization, o)
		JSON_SIP_HDR(authorization, o)
		JSON_SIP_HDR(authentication_info, o)
		JSON_SIP_HDR(www_authenticate, o)
		JSON_SIP_HDR(warning, o)
		JSON_SIP_HDR(service_route, o)
		JSON_SIP_HDR(path, o)

		JSON_SIP_HDR_EXTRA(refer_sub, o)
		JSON_SIP_HDR_EXTRA(alert_info, o)
		JSON_SIP_HDR_EXTRA(reply_to, o)
		JSON_SIP_HDR_EXTRA(p_asserted_identity, o)
		JSON_SIP_HDR_EXTRA(p_preferred_identity, o)
		JSON_SIP_HDR_EXTRA(remote_party_id, o)

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


        sip_unknown_t * hdr = sip_unknown(sip) ;
        while( NULL != hdr ) {
			string str = hdr->un_name ; \
			boost::replace_all(str,"_","-") ; \
        	o << ",\"" << hdr->un_name << "\",: " ;
        	o << "\"" << hdr->un_value << "\"" ;
            hdr = hdr->un_next ;
        }
 
		JSON_SIP_HDR(content_length, o)

		o << "}" ;
		/* end of headers */

		o << "}" ;

	}
}