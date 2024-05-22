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

#include <algorithm>
#include <regex>

#include <boost/algorithm/string.hpp>

#include <arpa/inet.h>

#include "sip-transports.hpp"
#include "controller.hpp"
#include "drachtio.h"

namespace {
    /* needed to be able to live in a boost unordered container */
    size_t hash_value( const drachtio::SipTransport& d) {
        std::size_t seed = 0;
        boost::hash_combine(seed, d.getTport());
        return seed;
    }
}

namespace drachtio {
  
  SipTransport::SipTransport(const string& contact, const string& localNet, const string& externalIp) :
    m_strContact(contact), m_strExternalIp(externalIp), m_strLocalNet(localNet), m_tp(NULL), m_tpName(NULL), m_netmask(0) {
    init() ;
  }
  SipTransport::SipTransport(const string& contact, const string& localNet) :
    m_strContact(contact), m_strLocalNet(localNet), m_tp(NULL), m_tpName(NULL), m_netmask(0) {
    init() ;
  }
  SipTransport::SipTransport(const string& contact) : m_strContact(contact), m_tp(NULL), m_tpName(NULL), m_netmask(0) {
    init() ;
  }
  SipTransport::SipTransport(const std::shared_ptr<drachtio::SipTransport> other) :
    m_strContact(other->m_strContact), m_strLocalNet(other->m_strLocalNet), m_strExternalIp(other->m_strExternalIp), 
    m_dnsNames(other->m_dnsNames), m_tp(NULL), m_tpName(NULL), m_netmask(other->m_netmask) {
    init() ;
  }

  SipTransport::~SipTransport() {
  }

  void SipTransport::init() {

    if( !parseSipUri(m_strContact, m_contactScheme, m_contactUserpart, m_contactHostpart, m_contactPort, m_contactParams) ) {
        cerr << "SipTransport::init - contact: " << m_strContact << endl ;
        throw std::runtime_error(string("SipTransport::init - invalid contact ") + m_strContact);
    }

    if( !m_strExternalIp.empty() ) {
      try {
        std::regex re("^([0-9\\.]*)$");
        std::smatch mr;
        if (!std::regex_search(m_strExternalIp, mr, re)) {
          cerr << "SipTransport::init - invalid format for externalIp, must be ipv4 dot decimal address: " << m_strExternalIp << endl ;
          throw std::runtime_error("SipTransport::init - invalid format for externalIp, must be ipv4 dot decimal address");
        }
      } catch( std::regex_error& e) {
        DR_LOG(log_error) << "SipTransport::init - regex error " << e.what();
      }
    }

    bool hasNetmask = false;
    string network, bits;
    uint32_t netbits;

    if( !m_strLocalNet.empty() ) {
      hasNetmask = true;

      try {
        std::regex re("^([0-9\\.]*)\\/(.*)$");
        std::smatch mr;
        if (!std::regex_search(m_strLocalNet, mr, re)) {
          cerr << "SipTransport::init - invalid format for localNet, must be CIDR format: " << m_strLocalNet << endl ;
          throw std::runtime_error("SipTransport::init - invalid format for localNet, must be CIDR format");          
        }
        network = mr[1] ; 
        bits = mr[2];
      } catch (std::regex_error& e) {
        DR_LOG(log_error) << "SipTransport::init - regex error 2 " << e.what();        
      }
    }

    if (hasNetmask) {
      struct sockaddr_in range;
      inet_pton(AF_INET, network.c_str(), &(range.sin_addr));
      m_range = htonl(range.sin_addr.s_addr) ;

      uint32_t netbits = ::atoi( bits.c_str() ) ;    
      m_netmask = ~(~uint32_t(0) >> netbits);    
    }
  }

  bool SipTransport::shouldAdvertisePublic( const char* address ) const {
    if( !hasExternalIp() ) return false ;

    return !isInNetwork(address) ;
  }

  bool SipTransport::isInNetwork(const char* address) const {
    if (!m_netmask) return false;

    struct sockaddr_in addr;
    inet_pton(AF_INET, address, &(addr.sin_addr));

    uint32_t ip = htonl(addr.sin_addr.s_addr) ;

    return (ip & m_netmask) == (m_range & m_netmask) ;
  }

  void SipTransport::getDescription(string& s, bool shortVersion) {
    s = "" ;
    if( hasTport() ) {
      s += getProtocol() ;
      s += "/" ;
      s += getHost() ;
      s += ":" ;
      s += getPort() ;

      if( shortVersion ) return ;

      s += " ";
    }
    s += "(";
    s += getContact() ;
    s += ", external-ip: " ;
    s += getExternalIp() ;
    s += ", local-net: " ;
    s += getLocalNet() ;
    s += ")" ;

    if( m_dnsNames.size() ) {
      s += " dns names: " ;
      int i = 0 ;
      for( vector<string>::const_iterator it = m_dnsNames.begin(); it != m_dnsNames.end(); ++it, i++ ) {
        if( i++ ) {
          s+= ", " ;
        }
        s += *it ;
      }
    }
  }

  void SipTransport::getHostport(string& s, bool localIpsOnly) {
    assert(hasTport()) ;
    s = "" ;
    s += getProtocol() ;
    s += "/" ;
    s += (!localIpsOnly && hasExternalIp()) ? getExternalIp() : getHost() ;
    s += ":" ;
    s += getPort() ;
  }

  void SipTransport::getBindableContactUri(string& contact) {
    contact = m_contactScheme ;
    contact.append(":");

    //host 
    if (hasExternalIp()) {
      contact.append(m_strExternalIp) ;
    }
    else {
      contact.append(m_contactHostpart) ;      
    }

    //port
    if (!m_contactPort.empty()) {
      contact.append(":") ;
      contact.append(m_contactPort) ;
    }

    //parameters
    for (vector< pair<string,string> >::const_iterator it = m_contactParams.begin(); it != m_contactParams.end(); ++it) {
      contact.append(";");
      contact.append(it->first) ;
      if( !it->second.empty() ) {
        contact.append("=");
        contact.append(it->second) ;
      }
    }

    if (hasExternalIp()) {
      contact.append(";maddr=");
      contact.append(m_contactHostpart);
    }
    DR_LOG(log_debug) << "SipTransport::getBindableContactUri: " << contact;
  }

  void SipTransport::getContactUri(string& contact, bool useExternalIp) {
    contact = m_contactScheme ;
    contact.append(":");
    if( !m_contactUserpart.empty() ) {
      contact.append(m_contactUserpart) ;
      contact.append("@") ;
    }
    if( m_tp ) {
      contact.append(useExternalIp && !m_strExternalIp.empty() ? m_strExternalIp : getHost()) ;
      const char* szPort = getPort() ;
      const char* szProto = getProtocol() ;
      if( szPort && strlen(szPort) > 0 ) {
        contact.append(":") ;
        contact.append(szPort);
      }
      if (szProto && strlen(szProto) > 0 && 0 != strcmp(szProto, "udp")) {
        contact.append(";transport=");
        contact.append(szProto);
      }
    }
    else {
      contact.append(useExternalIp && !m_strExternalIp.empty() ? m_strExternalIp : m_contactHostpart) ;
      if( !m_contactPort.empty() ) {
        contact.append(":") ;
        contact.append(m_contactPort);
      }      
      if( m_contactParams.size() > 0 ) {
        for (vector< pair<string,string> >::const_iterator it = m_contactParams.begin(); it != m_contactParams.end(); ++it) {
          pair<string,string> kv = *it ;
          contact.append(";") ;
          contact.append(kv.first);
          if( !kv.second.empty() ) {
            contact.append("=") ;
            contact.append(kv.second) ;
          }
        }
      }
    }

    DR_LOG(log_debug) << "SipTransport::getContactUri - created Contact header: " << contact;
  }
  sip_via_t* SipTransport::makeVia(su_home_t * h, const char* szRemoteHost) {
    bool isInSubnet = szRemoteHost ? this->isInNetwork(szRemoteHost) : false;
    string host = this->getHost();
    if (this->hasExternalIp() && !isInSubnet) {
      host = this->getExternalIp();
    }

    string proto = this->getProtocol();
    if (0 == proto.length()) proto = "UDP";
    boost::to_upper(proto);

    string transport = string("SIP/2.0/") + proto;
    DR_LOG(log_debug) << "SipTransport::makeVia - host " << host << ", port " << this->getPort() << ", transport " << transport ;

    return sip_via_create(h, host.c_str(), this->getPort(), transport.c_str());
  }


  bool SipTransport::isIpV6(void) {
    return hasTport() && NULL != strstr( getHost(), "[") && NULL != strstr( getHost(), "]") ;
  }

  bool SipTransport::isLocalhost(void) {
    return hasTport() && (0 == strcmp(getHost(), "127.0.0.1") || 0 == strcmp(getHost(), "[::1]"));
  }

  bool SipTransport::isLocal(const char* szHost) {
    if( 0 == strcmp(getHost(), szHost) ) {
      return true ;
    }
    else if( 0 == m_contactHostpart.compare(szHost) ) {
      return true ;
    }
    else if( hasExternalIp() && 0 == m_strExternalIp.compare(szHost) ) {
      return true ;
    }
    for( vector<string>::const_iterator it = m_dnsNames.begin(); it != m_dnsNames.end(); ++it ) {
      if( 0 == (*it).compare(szHost) ) {
        return true ;
      }
    }
    return false ;
  }

  uint32_t SipTransport::getOctetMatchCount(const string& address) {
    uint32_t count = 0 ;
    string them[4], mine[4];
    try {
      std::regex re("^(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)");
      std::smatch mr;
      if (std::regex_search(address, mr, re) && mr.size() > 1) {
        them[0] = mr[1] ;
        them[1] = mr[2] ;
        them[2] = mr[3] ;
        them[3] = mr[4] ;

        string host = this->getHost();
        if (std::regex_search(host, mr, re) && mr.size() > 1) {
          mine[0] = mr[1] ;
          mine[1] = mr[2] ;
          mine[2] = mr[3] ;
          mine[3] = mr[4] ;
  
          for(int i = 0; i < 4; i++) {
            if(0 != them[i].compare(mine[i])) return count;
            count++;
          }          
        }        
      }
    } catch (std::regex_error& e) {
      DR_LOG(log_error) << "SipTransport::getOctetMatchCount - regex error " << e.what();
    }

    return count;
  }

  /** static methods */

  SipTransport::mapTport2SipTransport SipTransport::m_mapTport2SipTransport  ;
  std::shared_ptr<SipTransport> SipTransport::m_masterTransport ;

  /**
   * iterate all tport_t* that have been created, and any that are "unassigned" associate
   * them with the provided SipTransport 
   * 
   * @param config - SipTransport that was used to create the currently unassigned tport_t* 
   */
  void SipTransport::addTransports(std::shared_ptr<SipTransport> config, unsigned int mtu) {


    tport_t* tp = nta_agent_tports(theOneAndOnlyController->getAgent());
    m_masterTransport = std::make_shared<SipTransport>( config ) ;
    m_masterTransport->setTport(tp) ;


    while (NULL != (tp = tport_next(tp) )) {
      const tp_name_t* tpn = tport_name(tp) ;
      string desc ;
      getTransportDescription( tp, desc ); 

      if (0 == strcmp(tpn->tpn_proto, "udp") && mtu > 0) {
        tport_set_params(tp, TPTAG_MTU(mtu), TAG_END());   
      }

      mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.find(tp) ;
      if (it == m_mapTport2SipTransport.end()) {
        DR_LOG(log_info) << "SipTransport::addTransports - creating transport: " << hex << tp << ": " << desc ;

        std::shared_ptr<SipTransport> p = std::make_shared<SipTransport>( config ) ;
        p->setTport(tp); 
        p->setTportName(tpn) ;
        m_mapTport2SipTransport.insert(mapTport2SipTransport::value_type(tp, p)) ;

        if (p->getLocalNet().empty()) {
          string network, bits;
          string host = p->hasExternalIp() ? p->getExternalIp() : p->getHost();
          if(0 == host.compare("127.0.0.1")) {
            network = "127.0.0.1";
            bits = "32";
          }
          else if(0 == host.find("192.168.0.")) {
            network = "192.168.0.0";
            bits = "24";
          }
          else if(0 == host.find("172.16.")) {
            network = "172.16.0.0";
            bits = "16";
          }
          else if(0 == host.find("10.")) {
            network = "10.0.0.0";
            bits = "8";
          }

          if (!network.empty()) {
            struct sockaddr_in range;
            inet_pton(AF_INET, network.c_str(), &(range.sin_addr));
            p->setRange(htonl(range.sin_addr.s_addr)) ;

            uint32_t netbits = ::atoi( bits.c_str() ) ;    
            p->setNetmask(~(~uint32_t(0) >> netbits));
            p->setNetwork(network);
            p->setLocalNet(network, bits);
          }
        }
      }
    }
  }

  std::shared_ptr<SipTransport> SipTransport::findTransport(tport_t* tp) {
    mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.find(tp) ;
    if( it == m_mapTport2SipTransport.end() ) {
      tport_t* tpp = tport_parent(tp);
      DR_LOG(log_debug) << "SipTransport::findTransport - could not find transport: " << hex << tp << " searching for parent " <<  tpp ;
      it = m_mapTport2SipTransport.find(tport_parent(tpp));
    }
    assert( it != m_mapTport2SipTransport.end() ) ;
    return it->second ;
  }

  std::shared_ptr<SipTransport> SipTransport::findAppropriateTransport(const char* remoteHost, const char* proto) {
    string scheme, userpart, hostpart, port ;
    vector< pair<string,string> > vecParam ;
    string host = remoteHost ;
    string requestedProto = (NULL == proto ? "" : proto);

    if( parseSipUri(host, scheme, userpart, hostpart, port, vecParam) ) {
      host = hostpart ;
    }

    for (vector<pair<string, string> >::const_iterator it = vecParam.begin(); it != vecParam.end(); ++it) {
      if (0 == it->first.compare("transport")) {
        requestedProto = it->second;
        break;
      }
    }
    std::transform(requestedProto.begin(), requestedProto.end(), requestedProto.begin(), ::tolower);
    DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: searching for a transport to reach " << requestedProto.c_str() << "/" << remoteHost ;
    string desc ;
    bool wantsIpV6 = (NULL != strstr( remoteHost, "[") && NULL != strstr( remoteHost, "]")) ;

    // create vector of candidates, remove those with wrong transport or protocol
    // then sort from most to least desirable:
    // - transports within the subnet of the remote host
    // - transports that have an external address
    // - ...all others, and finally..
    // - transports bound to localhost
    std::vector< std::shared_ptr<SipTransport> > candidates;
    std::pair< tport_t*, boost::shared_ptr<SipTransport> > myPair;

    for (auto pair : m_mapTport2SipTransport) {
      candidates.push_back(pair.second);
    }

    // filter by transport
    auto it = std::remove_if(candidates.begin(), candidates.end(), [wantsIpV6](const std::shared_ptr<SipTransport>& p) {
      return (wantsIpV6 && !p->isIpV6()) || (!wantsIpV6 && p->isIpV6());
    });
    candidates.erase(it, candidates.end());
    DR_LOG(log_debug) <<  "SipTransport::findAppropriateTransport - after filtering for transport we have " << candidates.size() << " candidates";

    // filter by protocol
    it = std::remove_if(candidates.begin(), candidates.end(), [requestedProto](const std::shared_ptr<SipTransport>& p) {
      if (requestedProto.length() == 0) return false;
      return 0 != strcmp(p->getProtocol(), requestedProto.c_str());
    });
    candidates.erase(it, candidates.end());
    DR_LOG(log_debug) <<  "SipTransport::findAppropriateTransport - after filtering for protocol we have " << candidates.size() << " candidates";


    sort(candidates.begin(), candidates.end(), [host](const std::shared_ptr<SipTransport>& pA, const std::shared_ptr<SipTransport>& pB) {

      if (pA->isInNetwork(host.c_str())) {
        return true;
      }
      if (pB->isInNetwork(host.c_str())) {
        return false;
      }

      uint32_t a = pA->getOctetMatchCount(host);
      uint32_t b = pB->getOctetMatchCount(host);
      if (a > b) return true;
      if (a < b) return false;

      if (pA->hasExternalIp()) return true;
      if (pB->hasExternalIp()) return false;

      if (pA->isLocalhost()) return false;
      if (pB->isLocalhost()) return true;

      return true;
    });

#ifdef DEBUG 
    DR_LOG(log_debug) <<  "SipTransport::findAppropriateTransport - in priority order, here are the candidates for sending to  " << host;
    BOOST_FOREACH(std::shared_ptr<SipTransport>& p, candidates) {
      string desc;
      p->getDescription(desc);
      DR_LOG(log_debug) << "SipTransport::findAppropriateTransport -   " << desc;
    }
#endif

    if (candidates.empty() && m_masterTransport->hasTportAndTpname()) {
      m_masterTransport->getDescription(desc) ;
      DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - returning master transport " << hex << m_masterTransport->getTport() << 
        " as we found no better matches: " << desc ;
      return m_masterTransport ;      
    }
    else if (candidates.empty()) {
      DR_LOG(log_info) << "SipTransport::findAppropriateTransport: - no transports found ";
      return nullptr;
    }

    std::shared_ptr<SipTransport> p = candidates[0];
    p->getDescription(desc);
    DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - returning the best match " << hex << p->getTport() << ": " << desc ;
    return p ;
  }

  void SipTransport::getAllExternalIps( vector<string>& vec ) {
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      std::shared_ptr<SipTransport> p = it->second ;
      if( p->hasExternalIp() ) {
        vec.push_back(p->getExternalIp()) ;
      }
      for( vector<string>::const_iterator itDns = p->getDnsNames().begin(); itDns != p->getDnsNames().end(); ++itDns ) {
        vec.push_back(*itDns) ;
      }
    }
  }
  void SipTransport::getAllExternalContacts( vector< pair<string, string> >& vec ) {
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      std::shared_ptr<SipTransport> p = it->second ;
      string port = p->getPort() != NULL ? p->getPort() : "5060";
      if( p->hasExternalIp() ) {
        vec.push_back( make_pair(p->getExternalIp(), port)) ;
      }
      for( vector<string>::const_iterator itDns = p->getDnsNames().begin(); itDns != p->getDnsNames().end(); ++itDns ) {
        vec.push_back( make_pair(*itDns, port)) ;
      }
    }
  }
  void SipTransport::getAllHostports( vector<string>& vec, bool localIpsOnly) {
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      std::shared_ptr<SipTransport> p = it->second ;
      string desc ;
      p->getHostport(desc, localIpsOnly);
      vec.push_back(desc) ;
    }
  }

  bool SipTransport::isLocalAddress(const char* szHost, tport_t* tp) {
    if( tp ) {
     std::shared_ptr<SipTransport> p = SipTransport::findTransport(tp) ;
     return p->isLocal(szHost); 
    }

    // search all
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      std::shared_ptr<SipTransport> p = it->second ;
      if( p->isLocal(szHost) ) {
        return true ;
      }
      for( vector<string>::const_iterator itDns = p->getDnsNames().begin(); itDns != p->getDnsNames().end(); ++itDns ) {
        if( 0 == (*itDns).compare(szHost) ) {
          return true;
        }
      }
    }

    return false;
  }

  void SipTransport::logTransports() {
    DR_LOG(log_info) << "SipTransport::logTransports - there are : " << dec <<  m_mapTport2SipTransport.size() << " transports";

    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      std::shared_ptr<SipTransport> p = it->second ;
      string desc ;

      p->getDescription(desc, false) ;

      if (0 == strcmp(p->getProtocol(), "udp")) {
        unsigned int mtuSize = 0;
        const tport_t* tp = p->getTport();
        tport_get_params(tp, TPTAG_MTU_REF(mtuSize), TAG_END());
        DR_LOG(log_info) << "SipTransport::logTransports - " << desc << ", mtu size: " << mtuSize;
      }
      else {
        DR_LOG(log_info) << "SipTransport::logTransports - " << desc ;
      }
    }
  }


}