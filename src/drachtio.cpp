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
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <errno.h>
#include <stdio.h>

#include <boost/tokenizer.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/thread.hpp>

#include <sofia-sip/url.h>
#include <sofia-sip/nta.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>




#include "drachtio.h"
#include "controller.hpp"

using namespace std ;
 
namespace {
    unsigned int json_allocs = 0 ;
    unsigned int json_bytes = 0 ;
    boost::mutex  json_lock ;
} ;

namespace drachtio {

    typedef boost::unordered_map<string,tag_type_t> mapHdr2Tag ;

    typedef boost::unordered_set<string> setHdr ;

    typedef boost::unordered_map<string,sip_method_t> mapMethod2Type ;

	/* headers that are allowed to be set by the client in responses to sip requests */
	mapHdr2Tag m_mapHdr2Tag = boost::assign::map_list_of
		( string("user_agent"), siptag_user_agent_str ) 
        ( string("subject"), siptag_subject_str ) 
        ( string("max_forwards"), siptag_max_forwards_str ) 
        ( string("proxy_require"), siptag_proxy_require_str ) 
        ( string("accept_contact"), siptag_accept_contact_str ) 
        ( string("reject_contact"), siptag_reject_contact_str ) 
        ( string("expires"), siptag_expires_str ) 
        ( string("date"), siptag_date_str ) 
        ( string("retry_after"), siptag_retry_after_str ) 
        ( string("timestamp"), siptag_timestamp_str ) 
        ( string("min_expires"), siptag_min_expires_str ) 
        ( string("priority"), siptag_priority_str ) 
        ( string("call_info"), siptag_call_info_str ) 
        ( string("organization"), siptag_organization_str ) 
        ( string("server"), siptag_server_str ) 
        ( string("in_reply_to"), siptag_in_reply_to_str ) 
        ( string("accept"), siptag_accept_str ) 
        ( string("accept_encoding"), siptag_accept_encoding_str ) 
        ( string("accept_language"), siptag_accept_language_str ) 
        ( string("allow"), siptag_allow_str ) 
        ( string("require"), siptag_require_str ) 
        ( string("supported"), siptag_supported_str ) 
        ( string("unsupported"), siptag_unsupported_str ) 
        ( string("event"), siptag_event_str ) 
        ( string("allow_events"), siptag_allow_events_str ) 
        ( string("subscription_state"), siptag_subscription_state_str ) 
        ( string("proxy_authenticate"), siptag_proxy_authenticate_str ) 
        ( string("proxy_authentication_info"), siptag_proxy_authentication_info_str ) 
        ( string("proxy_authorization"), siptag_proxy_authorization_str ) 
        ( string("authorization"), siptag_authorization_str ) 
        ( string("www_authenticate"), siptag_www_authenticate_str ) 
        ( string("authentication_info"), siptag_authentication_info_str ) 
        ( string("error_info"), siptag_error_info_str ) 
        ( string("warning"), siptag_warning_str ) 
        ( string("refer_to"), siptag_refer_to_str ) 
        ( string("referred_by"), siptag_referred_by_str ) 
        ( string("replaces"), siptag_replaces_str ) 
        ( string("session_expires"), siptag_session_expires_str ) 
        ( string("min_se"), siptag_min_se_str ) 
        ( string("path"), siptag_path_str ) 
        ( string("service_route"), siptag_service_route_str ) 
        ( string("reason"), siptag_reason_str ) 
        ( string("security_client"), siptag_security_client_str ) 
        ( string("security_server"), siptag_security_server_str ) 
        ( string("security_verify"), siptag_security_verify_str ) 
        ( string("privacy"), siptag_privacy_str ) 
        ( string("etag"), siptag_etag_str ) 
        ( string("if_match"), siptag_if_match_str ) 
        ( string("mime_version"), siptag_mime_version_str ) 
        ( string("content_type"), siptag_content_type_str ) 
        ( string("content_encoding"), siptag_content_encoding_str ) 
        ( string("content_language"), siptag_content_language_str ) 
        ( string("content_disposition"), siptag_content_disposition_str ) 
        ( string("request_disposition"), siptag_request_disposition_str ) 
        ( string("error"), siptag_error_str ) 
        ( string("refer_sub"), siptag_refer_sub_str ) 
        ( string("alert_info"), siptag_alert_info_str ) 
        ( string("reply_to"), siptag_reply_to_str ) 
        ( string("p_asserted_identity"), siptag_p_asserted_identity_str ) 
        ( string("p_preferred_identity"), siptag_p_preferred_identity_str ) 
        ( string("remote_party_id"), siptag_remote_party_id_str ) 
        ( string("payload"), siptag_payload_str ) 
        ( string("from"), siptag_from_str ) 
        ( string("to"), siptag_from_str ) 
        ( string("call_id"), siptag_call_id_str ) 
        ( string("cseq"), siptag_cseq_str ) 
        ( string("via"), siptag_via_str ) 
        ( string("route"), siptag_route_str ) 
        ( string("contact"), siptag_contact_str ) 
        ( string("rseq"), siptag_rseq_str ) 
        ( string("rack"), siptag_rack_str ) 
        ( string("record_route"), siptag_record_route_str ) 
        ( string("content_length"), siptag_content_length_str ) 
		;

	/* headers that are not allowed to be set by the client in responses to sip requests */
	setHdr m_setImmutableHdrs = boost::assign::list_of
		( string("from") ) 
		( string("to") ) 
		( string("call_id") ) 
		( string("cseq") ) 
        ( string("via") ) 
        ( string("route") ) 
        ( string("contact") ) 
        ( string("rseq") ) 
        ( string("rack") ) 
        ( string("record_route") ) 
        ( string("content_length") ) 
		;

   mapMethod2Type m_mapMethod2Type = boost::assign::map_list_of
        ( string("INVITE"), sip_method_invite ) 
        ( string("ACK"), sip_method_ack ) 
        ( string("CANCEL"), sip_method_cancel ) 
        ( string("BYE"), sip_method_bye ) 
        ( string("OPTIONS"), sip_method_options ) 
        ( string("REGISTER"), sip_method_register ) 
        ( string("INFO"), sip_method_info ) 
        ( string("PRACK"), sip_method_prack ) 
        ( string("UPDATE"), sip_method_update ) 
        ( string("MESSAGE"), sip_method_message ) 
        ( string("SUBSCRIBE"), sip_method_subscribe ) 
        ( string("NOTIFY"), sip_method_notify ) 
        ( string("REFER"), sip_method_refer ) 
        ( string("PUBLISH"), sip_method_publish ) 
        ;


	bool isImmutableHdr( const string& hdr ) {
		return m_setImmutableHdrs.end() != m_setImmutableHdrs.find( hdr ) ;
	}

	bool getTagTypeForHdr( const string& hdr, tag_type_t& tag ) {
		mapHdr2Tag::const_iterator it = m_mapHdr2Tag.find( hdr ) ;
		if( it != m_mapHdr2Tag.end() ) {
		    tag = it->second ;
		    return true ;
		}		
		return false ;
	}

	void generateUuid(string& uuid) {
	    boost::uuids::uuid id = boost::uuids::random_generator()();
        uuid = boost::lexical_cast<string>(id) ;
    }	

	void parseGenericHeader( msg_common_t* p, string& hvalue) {
		string str((const char*) p->h_data, p->h_len) ;
		boost::char_separator<char> sep(": \r\n") ;
        tokenizer tok( str, sep) ;
        if( distance( tok.begin(), tok.end() ) > 1 ) hvalue = *(++tok.begin() ) ;
 	}

    void normalizeSipUri( std::string& uri ) {
        if( string::npos == uri.find("sip:") ) {
            uri = "sip:" + uri ;
        }
    }

    void replaceHostInUri( std::string& uri, const std::string& hostport ) {
        url_t *url = url_make(theOneAndOnlyController->getHome(), uri.c_str() ) ;
        uri = url->url_scheme ;
        uri.append(":") ;
        if( url->url_user ) {
            uri.append( url->url_user ) ;
            uri.append("@") ;
        }
        uri.append( hostport ) ;


    }

    sip_method_t methodType( const string& method ) {
        mapMethod2Type::const_iterator it = m_mapMethod2Type.find( method ) ;
        if( m_mapMethod2Type.end() == it ) return sip_method_unknown ;
        return it->second ;
    }
 
    bool isLocalSipUri( const string& requestUri ) {

        static bool initialized = false ;
        static boost::unordered_set<string> setLocalUris ;

        if( !initialized ) {
            initialized = true ;

            nta_agent_t* agent = theOneAndOnlyController->getAgent() ;
            tport_t *t = nta_agent_tports( agent ) ;
            for (tport_t* tport = t; tport; tport = tport_next(tport) ) {
                const tp_name_t* tpn = tport_name( tport );
                if( 0 == strcmp( tpn->tpn_host, "*") ) 
                    continue ;

                string localUri = tpn->tpn_host ;
                localUri += ":" ;
                localUri += (tpn->tpn_port ? tpn->tpn_port : "5060");

                setLocalUris.insert( localUri ) ;

                if( 0 == strcmp(tpn->tpn_host,"127.0.0.1") ) {
                    localUri = "localhost:" ;
                    localUri += (tpn->tpn_port ? tpn->tpn_port : "5060");
                    setLocalUris.insert( localUri ) ;
                }
            }
       }

        url_t *url = url_make(theOneAndOnlyController->getHome(), requestUri.c_str() ) ;
        string uri = url->url_host ;
        uri += ":" ;
        uri += ( url->url_port ? url->url_port : "5060") ;

        return setLocalUris.end() != setLocalUris.find( uri ) ;
    }

    void* my_json_malloc( size_t bytes ) {
        boost::lock_guard<boost::mutex> l( json_lock ) ;

        json_allocs++ ;
        json_bytes += bytes ;
        //DR_LOG(log_debug) << "my_json_malloc: alloc'ing " << bytes << " bytes; outstanding allocations: " << json_allocs << ", outstanding memory size: " << json_bytes << endl ;

        /* store size at the beginnng of the block */
        void *ptr = malloc( bytes + 8 ) ;
        *((size_t *)ptr) = bytes ;
 
        return (void*) ((char*) ptr + 8);
    }

    void my_json_free( void* ptr ) {
       boost::lock_guard<boost::mutex> l( json_lock ) ;

        size_t size;
        ptr = (void *) ((char *) ptr - 8) ;
        size = *((size_t *)ptr);

        json_allocs-- ;
        json_bytes -= size ;
        //DR_LOG(log_debug) << "my_json_free: freeing " << size << " bytes; outstanding allocations: " << json_allocs << ", outstanding memory size: " << json_bytes << endl ;

        /* zero memory in debug mode */
        memset( ptr, 0, size + 8 ) ;

    }
 
 }

