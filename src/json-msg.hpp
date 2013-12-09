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

#include "json_spirit.h"

using namespace std ;

typedef boost::tokenizer<boost::char_separator<char> > tokenizer ;

namespace drachtio {

	class JsonMsg : public boost::enable_shared_from_this<JsonMsg> {
	public:

		JsonMsg() {}
		JsonMsg( json_spirit::mValue& value ) : m_value(value) {

		} 
		JsonMsg( const string& strMsg ) : m_strRaw(strMsg) {
			try {
				set( strMsg ) ;
			} catch( std::runtime_error& e) {
				cerr << "error: " << e.what() << " json - " << strMsg << endl ;
				throw e ;
			}
		}
		template<typename InputIterator> JsonMsg( InputIterator begin, InputIterator end ) {
			try {
				set( begin, end ) ;
			} catch( std::runtime_error& e) {
				cerr << "error: " << e.what() << endl ;
				throw e ;
			}
		}
		~JsonMsg() {}


		bool has( const char * szPath ) const {
			try {
				get_by_path_or_throw( szPath ) ; 
				return true ;
			} catch( std::runtime_error& err ) {}
			return false ;
		}
		template< typename T> bool get(const char * szPath, T& t) const {
			bool bReturn = false ;
			try {
				const json_spirit::mValue& value = get_by_path_or_throw( szPath ) ;
				t = value.get_value<T>() ;
				bReturn = true ;
			} catch( std::runtime_error& err ) {}
			return bReturn ;
		}
		template< typename T> T get_or_default(const char * szPath, const T defaultValue) const {
			T t; 
			try {
				const json_spirit::mValue& value = get_by_path_or_throw( szPath ) ;
				t = value.get_value<T>() ;
			} catch( ... ) {
				t = defaultValue ;
			}
			return t ;
		}
		template< typename T> bool get_array(const char * szPath, std::vector<T>& t) const {
			bool bReturn = false ;
			try {
				const json_spirit::mArray& array_val = get_by_path_or_throw( szPath ).get_array() ;
				for( json_spirit::mArray::const_iterator it = array_val.begin(); it != array_val.end(); it++ ) {
					t.push_back( it->get_value<T>() ) ;
				}
				bReturn = true ;
			} catch( std::runtime_error& err ) {
				cerr << "Exception in JsonMsg::get_array: " << err.what() << endl ;
			}
			catch( ... ) {
				cerr << "Unknown exception" << endl ;
			}
			return bReturn ;
		}

		template<typename InputIterator> void set( InputIterator begin, InputIterator end ) {
			m_strRaw.assign( begin, end ) ;
			if( !json_spirit::read( m_strRaw, m_value ) ) throw std::runtime_error("Invalid JSON") ;
		}
		void set( const string& strJson ) {
			if( !json_spirit::read( strJson, m_value ) ) throw std::runtime_error("Invalid JSON") ;
		}
		void stringify(string& json) const { 
			json = json_spirit::write( m_value ) ;
			ostringstream o ;
			o << json.length() << "#" << json ;
			json = o.str() ;
			return ;
		}
		bool isNull() const { return m_value.is_null(); }
		const string& getRawText() const { return m_strRaw; }

	protected:

		const json_spirit::mValue& get_by_path_or_throw( const char * szPath ) const {

			if( 0 == strlen(szPath) ) throw std::runtime_error("Invalid path") ;

			string strPath( szPath ) ;

			boost::char_separator<char> sep(".") ;
			tokenizer tok( strPath, sep) ;
			bool isArray = false ;
			bool isObject = true ;
			const json_spirit::mValue* pValue = &m_value ;

			for( tokenizer::iterator it = tok.begin(); it != tok.end(); ++it ) {
				const string& el = *it ;

				if( 0 == el.compare("<array>") ) {
					isArray = true ;
					isObject = false ;
					continue ;
				}

				json_spirit::Value_type type = pValue->type() ;

				const json_spirit::mObject& obj = pValue->get_obj() ;
	           	json_spirit::mObject::const_iterator itChild = obj.find( el ) ;
	           	if( obj.end() == itChild ) {
	           		ostringstream o ;
	           		o << "Path component not found: " << el ;
	           		throw std::runtime_error(o.str()) ;
	           	}

	           	const json_spirit::mValue& childObj = itChild->second ; 

	           	pValue = &childObj ;

	           	isArray = false ;
	           	isObject = true ;

			}
			return *pValue ;

		}

		json_spirit::mValue 	m_value ;
		string 					m_strRaw ;
	} ;
}

#endif