#include "sofia-msg.hpp"
#include <boost/algorithm/string.hpp>

#include "controller.hpp"


#define JSON_SIP_HDR( name, json ) \
do \
	if( sip->sip_ ## name ) { \
			string str = #name ; \
			boost::replace_all(str,"_","-") ; \
			json_object_set_new_nocheck(json, str.c_str(), name ## _parser::toJson( sip->sip_ ## name )) ; \
	} \
while(0); 

#define JSON_SIP_HDR_EXTRA( name, json ) \
do \
	if( sip_ ## name(sip) ) { \
			string str = #name ; \
			boost::replace_all(str,"_","-") ; \
			json_object_set_new_nocheck(json, str.c_str(), name ## _parser::toJson( sip_ ## name(sip))) ; \
	} \
while(0); 

namespace drachtio {

	SofiaMsg::SofiaMsg( nta_incoming_t* irq, sip_t const *sip ) : m_json( json_object() ) {

		string host ;
		unsigned int port ;
		sip_time_t recv = theOneAndOnlyController->getTransactionTime( irq ) ;
		theOneAndOnlyController->getTransactionSender( irq, host, port ) ;

		json_object_set_new_nocheck(m_json,"source_address",json_string(host.c_str())) ;
		json_object_set_new_nocheck(m_json,"source_port",json_integer(port)) ;
		json_object_set_new_nocheck(m_json,"time",json_integer(recv)) ;
		json_object_set_new_nocheck(m_json,"source",json_string("network")) ;

		if( sip->sip_request ) json_object_set_new_nocheck(m_json,"request_uri",request_parser::toJson( sip->sip_request )) ;
		else if( sip->sip_status ) json_object_set_new_nocheck(m_json,"status", status_parser::toJson( sip->sip_status )) ;
		
		populateHeaders( sip, m_json ) ;

	}	

	SofiaMsg::SofiaMsg( nta_outgoing_t* orq, sip_t const *sip ) : m_json( json_object() ) {

		su_time_t tv = su_now() ;

		json_object_set_new_nocheck(m_json,"time",json_integer(tv.tv_sec)) ;
		json_object_set_new_nocheck(m_json,"source",json_string("application")) ;

		if( sip->sip_request ) json_object_set_new_nocheck(m_json,"request_uri",request_parser::toJson( sip->sip_request )) ;
		else if( sip->sip_status ) json_object_set_new_nocheck(m_json,"status", status_parser::toJson( sip->sip_status )) ;
	
		populateHeaders( sip, m_json ) ;
	}	

	void SofiaMsg::populateHeaders( sip_t const *sip, json_t* json ) {

		json_t* headers = json_object() ;

		/* for this one, the name to method pattern in the macro doesn't work */
		json_object_set_new_nocheck(headers,"call-id",call_id_parser::toJson( sip->sip_call_id )) ;

		JSON_SIP_HDR(via, headers)
		JSON_SIP_HDR(from, headers)
		JSON_SIP_HDR(to, headers)
		JSON_SIP_HDR(cseq, headers)
		JSON_SIP_HDR(contact, headers)
		JSON_SIP_HDR(route, headers)
		JSON_SIP_HDR(record_route, headers)
		JSON_SIP_HDR(max_forwards, headers)
		JSON_SIP_HDR(proxy_require, headers)
		JSON_SIP_HDR(content_type, headers)
		JSON_SIP_HDR(date, headers)
		JSON_SIP_HDR(event, headers)
		JSON_SIP_HDR(error_info, headers)
		JSON_SIP_HDR(expires, headers)
		JSON_SIP_HDR(rack, headers)
		JSON_SIP_HDR(min_expires, headers)
		JSON_SIP_HDR(refer_to, headers)
		JSON_SIP_HDR(referred_by, headers)
		JSON_SIP_HDR(replaces, headers)
		JSON_SIP_HDR(retry_after, headers)
		JSON_SIP_HDR(request_disposition, headers)
		JSON_SIP_HDR(reason, headers)
		JSON_SIP_HDR(session_expires, headers)
		JSON_SIP_HDR(min_se, headers)
		JSON_SIP_HDR(subscription_state, headers)
		JSON_SIP_HDR(privacy, headers)
		JSON_SIP_HDR(timestamp, headers)
		JSON_SIP_HDR(etag, headers)
		JSON_SIP_HDR(if_match, headers)
		JSON_SIP_HDR(mime_version, headers)
		JSON_SIP_HDR(content_encoding, headers)
		JSON_SIP_HDR(content_language, headers)
		JSON_SIP_HDR(content_disposition, headers)
		JSON_SIP_HDR(proxy_require, headers)
		JSON_SIP_HDR(subject, headers)
		JSON_SIP_HDR(priority, headers)
		JSON_SIP_HDR(call_info, headers)
		JSON_SIP_HDR(organization, headers)
		JSON_SIP_HDR(server, headers)
		JSON_SIP_HDR(user_agent, headers)
		JSON_SIP_HDR(in_reply_to, headers)
		JSON_SIP_HDR(accept, headers)
		JSON_SIP_HDR(accept_encoding, headers)
		JSON_SIP_HDR(allow, headers)
		JSON_SIP_HDR(supported, headers)
		JSON_SIP_HDR(unsupported, headers)
		JSON_SIP_HDR(require, headers)
		JSON_SIP_HDR(allow_events, headers)
		JSON_SIP_HDR(accept_language, headers)
		JSON_SIP_HDR(proxy_authenticate, headers)
		JSON_SIP_HDR(proxy_authentication_info, headers)
		JSON_SIP_HDR(proxy_authorization, headers)
		JSON_SIP_HDR(authorization, headers)
		JSON_SIP_HDR(authentication_info, headers)
		JSON_SIP_HDR(www_authenticate, headers)
		JSON_SIP_HDR(warning, headers)
		JSON_SIP_HDR(service_route, headers)
		JSON_SIP_HDR(path, headers)
		JSON_SIP_HDR(content_length, headers)

		JSON_SIP_HDR_EXTRA(refer_sub, headers)
		JSON_SIP_HDR_EXTRA(alert_info, headers)
		JSON_SIP_HDR_EXTRA(reply_to, headers)
		JSON_SIP_HDR_EXTRA(p_asserted_identity, headers)
		JSON_SIP_HDR_EXTRA(p_preferred_identity, headers)
		JSON_SIP_HDR_EXTRA(remote_party_id, headers)

		/* special cases that the macro doesn't cover */
		if( sip->sip_security_server ) json_object_set_new_nocheck(headers, "security-server", security_agree_parser::toJson( sip->sip_security_server )) ;
		if( sip->sip_security_client ) json_object_set_new_nocheck(headers, "security-client", security_agree_parser::toJson( sip->sip_security_client )) ;
		if( sip->sip_security_verify ) json_object_set_new_nocheck(headers, "security-verify", security_agree_parser::toJson( sip->sip_security_verify )) ;

		/* custom headers */
        sip_unknown_t * hdr = sip_unknown(sip) ;
        while( NULL != hdr ) {
			json_object_set_new_nocheck(headers, hdr->un_name, json_string(hdr->un_value)) ;
             hdr = hdr->un_next ;
        }
 
		json_object_set_new_nocheck(json,"headers",headers) ;

		if(  sip->sip_payload ) json_object_set_new_nocheck(json,"body",payload_parser::toJson( sip->sip_payload )) ;

	}
}