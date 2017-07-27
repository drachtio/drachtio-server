#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <iostream>

#include "sofia-sip/su.h"
#include "sofia-sip/su_alloc.h"

#include "sofia-sip/nta.h"
#include "sofia-sip/sip.h"
#include "sofia-sip/sip_parser.h"
#include "sofia-sip/msg.h"

su_home_t *home ;

void printParams( const msg_param_t params[] ) {
  if( !params ) return ;

  int i;
  msg_param_t p;

  std:: cout << "params" << std::endl ;
  for (i = 0; (p = params[i]); i++) {
    std::cout << "   " << p << std::endl ;
  }

}
void printUrl( url_t* u ) {
  std::cout << "url: " << std::endl ;

  if( !u ) return ;

  std::cout << "  scheme:   " << ((u)->url_scheme ? (u)->url_scheme : "") << std::endl ;
  std::cout << "  user:     " << ((u)->url_user ? (u)->url_user : "") << std::endl ;
  std::cout << "  password: " <<  ((u)->url_password ? (u)->url_password : "") << std::endl ;
  std::cout << "  host:     " << ((u)->url_host ? (u)->url_host : "") << std::endl ;
  std::cout << "  port:     " << ((u)->url_port ? (u)->url_port : "") << std::endl ;
  std::cout << "  path:     " << ((u)->url_path ? (u)->url_path : "") << std::endl ;
  std::cout << "  params:   " << ((u)->url_params ? (u)->url_params : "") << std::endl ;
  std::cout << "  headers:  " << ((u)->url_headers ? (u)->url_headers : "") << std::endl ;
  std::cout << "  fragment: " << ((u)->url_fragment ? (u)->url_fragment : "") << std::endl ;

}
void printDisplay(const char* display) {
  std::cout << "display: " << (display ? display : "") << std::endl ;
}
void printComment(const char* comment) {
  std::cout << "comment: " << (comment ? comment : "") << std::endl ;
}

int parseNameAddr( std::string& input, int dispose ) {
  int rc = 0 ;
  char buf[255] ;
  char *s ;
  const char* return_display = NULL ;
  url_t url[1] ;
  const msg_param_t* params = NULL ;
  const char* comment = NULL;

  s = strncpy( buf, input.c_str(), 255) ;

  rc = sip_name_addr_d(home,
      &s,
      &return_display,
      url,
       &params,
       &comment) ;

  if( 0 == rc ) {
    printDisplay( return_display ) ;
    printUrl( url ) ;
    printParams( params ) ;

    // params, if any were parsed, were placed into a home memory structure so should be freed
    // note: as far as I know nothing else is allocated from home during the decode
    if( params && dispose ) {
      su_free(home, (void *) params) ;
    }
  }
  else {
    std::cout << "failure parsing as a sip name-addr: " << input << std::endl ;
  }
  return rc ;
}

void replaceHostPort( std::string& input ) {
  int rc = 0 ;
  char buf[255] ;
  char obuf[255] ;
  char *s ;
  const char* display = NULL ;
  url_t url[1] ;
  const msg_param_t* params = NULL ;
  const char* comment = NULL;
  std::string newHost = "123.123.123.123" ;
  std::string newPort = "5066" ;

  std::cout << "replaceHostPort input " << input << std::endl ;

  s = strncpy( buf, input.c_str(), 255) ;

  rc = sip_name_addr_d(home,
      &s,
      &display,
      url,
       &params,
       &comment) ;

  if( 0 == rc ) {

    url->url_host = newHost.c_str() ;
    url->url_port = newPort.c_str() ;

    int nChars = sip_name_addr_e(obuf, 255,
       0,
       display,
       1, 
       url,
       params,
       comment) ;

    std::cout << "After reencoding we have " << obuf << std::endl ;

    if( params ) {
      su_free(home, (void *) params) ;
    }
  }
  else {
    std::cout << "replaceHostPort - couldn't replace because decode failed " << rc << std::endl ;
  }

}

int main( int argc, char **argv) {

  home = (su_home_t*) su_home_new(sizeof *home);
  std::string input ;

  std::getline(std::cin, input) ;

  std::cout << std::endl << "parsing " << input << std::endl << std::endl ;

  parseNameAddr( input, false ) ;

  replaceHostPort( input ) ;

  su_home_unref(home);

}

 