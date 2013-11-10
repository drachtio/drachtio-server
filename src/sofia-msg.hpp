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
		struct contact_parser {
			static stringstream& toJson( sip_contact_t* addr, stringstream& o) ;
		} ;
		struct generic_msg_list_parser {
			static stringstream& toJson( msg_list_t* list, stringstream& o) ;
		} ;


	protected:


	private:
		SofiaMsg() ;

		string	m_strMsg ;

	} ;


}


#endif 