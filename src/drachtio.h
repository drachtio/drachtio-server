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
#ifndef __DRACHTIO_H__
#define __DRACHTIO_H__

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/syslog_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/tokenizer.hpp>
#include <boost/thread.hpp>

#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operato
#include <boost/lexical_cast.hpp>
#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_tag.h>
#include <sofia-sip/sip_extra.h>
#include <sofia-sip/msg_types.h>

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace attrs = boost::log::attributes;
namespace keywords = boost::log::keywords;

namespace drachtio {
    
    class DrachtioController ;
    
    //typedef boost::tokenizer<boost::char_separator<char> > tokenizer ;
    
	enum severity_levels {
		log_notice,
		log_error,
		log_warning,
	    log_info,
	    log_debug
	};

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", severity_levels) ;

	enum agent_role {
		uac_role
		,uas_role
	}; 

	void generateUuid(std::string& uuid) ;

	void parseGenericHeader( msg_common_t* p, std::string& hvalue) ;

	bool isImmutableHdr( const std::string& hdr ) ;

	bool getTagTypeForHdr( const std::string& hdr, tag_type_t& tag ) ;

	void normalizeSipUri( std::string& uri ) ;
  
	void replaceHostInUri( std::string& uri, const std::string& hostport )  ;

	sip_method_t methodType( const std::string& method ) ;

	bool isLocalSipUri( const std::string& uri ) ;

	void* my_json_malloc( size_t bytes ) ;

	void my_json_free( void* ptr ) ;
 
 }

typedef boost::tokenizer<boost::char_separator<char> > tokenizer ;

#ifdef DRACHTIO_MAIN
drachtio::DrachtioController* theOneAndOnlyController = NULL ;
#else
extern drachtio::DrachtioController* theOneAndOnlyController ;
#endif


#define DR_LOG(level) BOOST_LOG_SEV(theOneAndOnlyController->getLogger(), level)

#endif
