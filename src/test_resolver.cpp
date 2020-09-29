#include <iostream>

#include "sofia-sip/su.h"

#include "dns-resolver.hpp"

using namespace drachtio;

int main(int argc, char* argv[]) {

  if (argc != 2) {
    std::cerr << "Usage: test_resolver <dns-name>" << std::endl;
    return -1;
  }

  if (su_init() != 0) {
    std::cerr << "su_init failed" << std::endl;
    return -1;
  }

  su_root_t* root = su_root_create( NULL ) ;
  if( NULL == root ) {
    std::cerr << "su_root_create failed" << std::endl;
    return -1;
  }

  su_home_t* home = su_home_create() ;
  if( NULL == home ) {
    std::cerr << "su_home_create failed" << std::endl;
    return -1;
  }

  std::shared_ptr<DnsResolver> pResolver = std::make_shared<DnsResolver>(root, home);
  pResolver->resolve(argv[1], [](const DnsResolver::Results_t& results) {
    std::cout << "got results: " << results.size() << " answers " << std::endl;
  });

  su_root_run( root ) ;


  return 0;
}