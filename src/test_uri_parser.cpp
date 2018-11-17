#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>
#include <iostream>

#include <regex>
#include  <boost/algorithm/string.hpp>

#include "drachtio.h"

using namespace std ;

bool parseSipUri(const string& uri, string& scheme, string& userpart, string& hostpart, string& port, 
  vector< pair<string,string> >& params) {

  try {
    std::regex re("^<?(sip|sips):(?:([^;]+)@)?([^;|^>|^:]+)(?::(\\d+))?(?:;([^>]+))?>?$");
    std::regex re2("^<?(sip|sips):(?:([^;]+)@)?(\\[[0-9a-fA-F:]+\\])(?::(\\d+))?(?:;([^>]+))?>?$");
    std::smatch mr;

    if (!std::regex_search(uri, mr, re) && !std::regex_search(uri, mr, re2)) {
      return false;
    }
    scheme = mr[1] ;
    userpart = mr[2] ;
    hostpart = mr[3] ;
    port = mr[4] ;

    string paramString = mr[5] ;
    if (paramString.length() > 0) {
      vector<string> strs;
      boost::split(strs, paramString, boost::is_any_of(";"));
      for (vector<string>::iterator it = strs.begin(); it != strs.end(); ++it) {
        vector<string> kv ;
        boost::split(kv, *it, boost::is_any_of("="));
        params.push_back(pair<string,string>(kv[0], kv.size() == 2 ? kv[1] : ""));
      }
    }

  } catch (std::regex_error& e) {
    cout << "regex error: " << e.what();
    return false;
  }
  return true;  
}

int main( int argc, char **argv) {
  string scheme, userpart, hostpart, port ;
  vector< pair<string,string> > vecParam ;

  string s ;
  cout << "Enter string to parse as a sip uri: " ;
  getline(cin, s); 

  if (!parseSipUri(s, scheme, userpart, hostpart, port, vecParam) ) {
    cout << "No match" << endl ;
    exit(0);    
  }

  cout << "scheme:     " << scheme << endl ;
  cout << "userpart:   " << userpart << endl ;
  cout << "hostpart:   " << hostpart << endl ;
  cout << "port  :     " << port << endl ;
  for (std::vector< pair<string,string> >::const_iterator it = vecParam.begin(); it != vecParam.end(); ++it) {
    pair<string,string> p = *it ;
    cout << "parameter:   (" << it->first << ", " << it->second << ")" << endl ;
  }

}
