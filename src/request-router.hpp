/*
Copyright (c) 2024, FirstFive8, Inc

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
#ifndef __REQUEST_ROUTER_H__
#define __REQUEST_ROUTER_H__

#include <boost/unordered_map.hpp>

#include "drachtio.h"

using namespace std ;

namespace drachtio {
    
  class RequestRouter {
  public:

    struct Route_t {
      Route_t(const string& httpMethod, const string& url, bool verifyPeer) : httpMethod(httpMethod), url(url), verifyPeer(verifyPeer) {}

      string  httpMethod;
      string  url ;
      bool    verifyPeer ;
    } ;

    RequestRouter() {}
    ~RequestRouter() {}
    
    void clearRoutes(void) {m_mapSipMethod2Route.clear();}
    void addRoute(const string& sipMethod, const string& httpMethod, const string& httpUrl, bool verifyPeer = false);
    bool getRoute(const char* szMethod, string& httpMethod, string& httpUrl, bool& verifyPeer) ;
    int getAllRoutes( vector< string >& vecRoutes ) ;
    int getCountOfRoutes(void) { return m_mapSipMethod2Route.size(); }

  private:

    typedef boost::unordered_map<string, Route_t> mapSipMethod2Route ;
    mapSipMethod2Route m_mapSipMethod2Route ;
  } ;

}  

#endif
