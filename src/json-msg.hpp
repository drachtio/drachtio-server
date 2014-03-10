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
#ifndef __JSON_MSG_H__
#define __JSON_MSG_H__

#include <string>
#include <boost/tokenizer.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <jansson.h>

using namespace std ;

typedef boost::tokenizer<boost::char_separator<char> > tokenizer ;

namespace drachtio {

	class JsonMsg : public boost::enable_shared_from_this<JsonMsg> {
	public:

		JsonMsg() : m_value(0) {}
		JsonMsg( json_t *value ) : m_value(value) {
			json_incref(m_value) ;
		} 
		JsonMsg( const string& strMsg ) : m_value(0) {
			try {
				set( strMsg ) ;
			} catch( std::runtime_error& e) {
				cerr << "error: " << e.what() << " json - " << strMsg << endl ;
				throw e ;
			}
		}
		template<typename InputIterator> JsonMsg( InputIterator begin, InputIterator end ) : m_value(0) {
			try {
				set( begin, end ) ;
			} catch( std::runtime_error& e) {
				cerr << "error: " << e.what() << endl ;
				throw e ;
			}
		}
		~JsonMsg() {
			if( m_value) json_decref(m_value) ;
		}

		json_t* value(void) { return m_value; }

		template<typename InputIterator> void set( InputIterator begin, InputIterator end ) {
			string strJson( begin, end) ;
			json_error_t error ;
			json_t* v = json_loads(strJson.c_str(), 0, &error) ;
			if( !v ) {
				cerr << "JsonMsg::set - " << error.text << " at line " << error.line << ", column " << error.column << endl ;
				throw std::runtime_error(error.text) ;
			}
			m_value = v ;
		}
		void set( json_t* json ) {
			if( m_value ) json_decref( m_value ) ;
			m_value = json ;
		}
		void set( const string& strJson ) {
			json_error_t error ;
			json_t* v = json_loads(strJson.c_str(), 0, &error) ;
			if( !v ) {
				cerr << "JsonMsg::set - " << error.text << " at line " << error.line << ", column " << error.column << endl ;
				throw std::runtime_error(error.text) ;
			}
			m_value = v ;			
		}
		void stringify(string& json) const { 
			assert(m_value) ;
			char * c = json_dumps(m_value, JSON_SORT_KEYS | JSON_COMPACT | JSON_ENCODE_ANY ) ;
			if( !c ) {
				throw std::runtime_error("JsonMsg::stringify: encode operation failed") ;
			}

			ostringstream o ;
			o << strlen(c) << "#" << c ;
			json = o.str() ;
#ifdef DEBUG
			my_json_free(c) ;
#else 
			free(c) ;
#endif
		}
		bool isNull() const { return m_value && m_value == json_null(); }

	protected:

		json_t *			 	m_value ;
	} ;
}

#endif
