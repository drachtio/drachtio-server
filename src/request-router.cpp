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

#include <boost/algorithm/string.hpp>
#include "request-router.hpp"

namespace drachtio {

  void RequestRouter::addRoute(const string& sipMethod, const string& httpMethod, const string& httpUrl, bool verifyPeer) {
    m_mapSipMethod2Route.insert( mapSipMethod2Route::value_type(boost::to_upper_copy<std::string>(sipMethod), Route_t(httpMethod, httpUrl, verifyPeer)) ) ;
  }
  bool RequestRouter::getRoute(const char* szMethod, string& httpMethod, string& httpUrl, bool& verifyPeer) {
    mapSipMethod2Route::iterator it = m_mapSipMethod2Route.find(szMethod) ;

    if( it == m_mapSipMethod2Route.end() ) {
      it = m_mapSipMethod2Route.find("*") ;
    }
    if( it != m_mapSipMethod2Route.end() ) {
      Route_t& route = it->second ;
      httpMethod = route.httpMethod ;
      httpUrl = route.url ;
      verifyPeer = route.verifyPeer ;
      return true ;        
    } 
    return false ;
  }

  int RequestRouter::getAllRoutes( vector< string >& vecRoutes ) {
    int count = 0 ;
    for( mapSipMethod2Route::iterator it = m_mapSipMethod2Route.begin(); it != m_mapSipMethod2Route.end(); it++, count++ ) {
      Route_t& route = it->second ;
      string sipMethod = it->first ;
      std::ostringstream s ;
      s << "sip-method: " << it->first << ", http-method: " <<  route.httpMethod << ", http-url: " << route.url ;
      if(string::npos != route.url.find("https")) {
        s << ", cert " << (route.verifyPeer ? "will" : "will not") << " be verified";
      }
      vecRoutes.push_back( s.str() ) ;

    }
    return count ;
  }


}