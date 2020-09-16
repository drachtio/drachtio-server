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
    m_strContact(contact), m_strExternalIp(externalIp), m_strLocalNet(localNet), m_tp(NULL), m_tpName(NULL), m_viaPrivate(nullptr), m_viaPublic(nullptr) {
    init() ;
  }
  SipTransport::SipTransport(const string& contact, const string& localNet) :
    m_strContact(contact), m_strLocalNet(localNet), m_tp(NULL), m_tpName(NULL) {
    init() ;
  }
  SipTransport::SipTransport(const string& contact) : m_strContact(contact), m_strLocalNet("0.0.0.0/0"), m_tp(NULL), m_tpName(NULL), m_viaPrivate(nullptr), m_viaPublic(nullptr)  {
    init() ;
  }
  SipTransport::SipTransport(const std::shared_ptr<drachtio::SipTransport>& other) :
    m_strContact(other->m_strContact), m_strLocalNet(other->m_strLocalNet), m_strExternalIp(other->m_strExternalIp), 
    m_dnsNames(other->m_dnsNames), m_tp(other->m_tp), m_tpName(other->m_tpName) {
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

    assert (!m_strLocalNet.empty() || isIpV6());
    m_network_v4 = boost::asio::ip::make_network_v4(m_strLocalNet);
  }

  void SipTransport::setTport(tport_t* tp) { 
    assert(!m_tp);
    assert(!m_viaPrivate);
    assert(!m_viaPublic);

    m_tp = tp ;

    if (m_strExternalIp.empty()) {
      m_viaPrivate = (sip_via_t*) tport_magic(tp);
    }
    else {
      m_viaPublic = (sip_via_t*) tport_magic(tp);

      string host = getHost();
      string proto = getProtocol();
      if (0 == proto.length()) proto = "UDP";
      boost::to_upper(proto);
      string transport = string("SIP/2.0/") + proto;
      m_viaPrivate = sip_via_create(theOneAndOnlyController->getHome(), getHost(), getPort(), transport.c_str(), NULL);
    }
  }

  int SipTransport::routingAbilityScore(const char* szAddress) {
    if (isIpV6() && NULL != strstr( szAddress, "[") && NULL != strstr( szAddress, "]")) return 64;
    int len = m_network_v4.prefix_length();
    if (0 == len) return hasExternalIp() ? 1 : 0;

    try {
      const auto address = boost::asio::ip::make_address_v4(szAddress);
      const auto hosts = m_network_v4.hosts();

      return hosts.find(address) != hosts.end() ? len : (-len);
    } catch (std::exception& e) {
        DR_LOG(log_error) << "routingAbilityScore - error checking routing for " << szAddress << ": " << e.what();
        return 0;
    }	
  }

  bool SipTransport::shouldAdvertisePublic( const char* address ) const {
    if( !hasExternalIp() ) return false ;

    return !isInNetwork(address) ;
  }

  bool SipTransport::isInNetwork(const char* address) const {
    boost::system::error_code ec;
    auto hosts = m_network_v4.hosts();
    auto addr = boost::asio::ip::make_address_v4(address, ec);
    
    if (isIpV6()) return false;
    if (0 == m_network_v4.prefix_length()) return false;
    if (ec) return false; // probably not a dot decimal address
    return hosts.find(addr) != hosts.end();
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
    s += m_network_v4.canonical().to_string() ;
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

  bool SipTransport::getHostport(string& s, bool external) {
    assert(hasTport()) ;
    if (external && !hasExternalIp()) return false;
    s = "" ;
    s += getProtocol() ;
    s += "/" ;
    s += external ? getExternalIp() : getHost() ;
    s += ":" ;
    s += getPort() ;
    return true;
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
  
  bool SipTransport::getContactAndVia(const char* szAddress, std::string& contact, sip_via_t** pvia) const {
    // first determine whether to use the private or public ip (if we have one), based on where we are sending
    bool useExternalIp = false;
    if (!m_strExternalIp.empty()) {
      if (!szAddress) useExternalIp = true;
      else if (0 == strncmp("sip:", szAddress, 4) || 0 == strncmp("sips:", szAddress, 5) || 0 == strncmp("tel:", szAddress, 4)) {
        std::string scheme, userpart, hostpart, port ;
        std::vector< pair<string,string> > vecParam ;
        std::string host = szAddress ;

        if( parseSipUri(host, scheme, userpart, hostpart, port, vecParam) ) {
          useExternalIp = !this->isInNetwork(hostpart.c_str());
          DR_LOG(log_debug) << "SipTransport::getContactAndVia " << (useExternalIp ? "using" : "not using") << 
            " public address based on ability to reach destination address " << szAddress << " from local address " << getHost();
        }
        else useExternalIp = true;
      }
      else {
        useExternalIp = !this->isInNetwork(szAddress);
      }
    }

    // set the via accordingly
    if (pvia != nullptr) {
      *pvia = useExternalIp ? m_viaPublic : m_viaPrivate;
      assert(*pvia);
    }

    // set the Contact accordingly
    contact = m_contactScheme ;
    contact.append(":");
    if( !m_contactUserpart.empty() ) {
      contact.append(m_contactUserpart) ;
      contact.append("@") ;
    }
    if( m_tp ) {
      contact.append(useExternalIp ? m_strExternalIp : getHost()) ;
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
      contact.append(useExternalIp ? m_strExternalIp : m_contactHostpart) ;
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

    DR_LOG(log_debug) << "SipTransport::getContactAndVia - created Contact header: " << contact;
    return useExternalIp;
  }

/*

  bool SipTransport::getContactUri(string& contact, const char* szAddress) {
    bool useExternalIp = false;
  
    if (szAddress) DR_LOG(log_debug) << "SipTransport::getContactUri searching for contact to use to reach dest " << szAddress;
    if (!m_strExternalIp.empty()) {
      if (!szAddress) useExternalIp = true;
      else {
        string scheme, userpart, hostpart, port ;
        vector< pair<string,string> > vecParam ;
        string host = szAddress ;

        if( parseSipUri(host, scheme, userpart, hostpart, port, vecParam) ) {
          useExternalIp = !this->isInNetwork(hostpart.c_str());
          DR_LOG(log_debug) << "SipTransport::getContactUri " << (useExternalIp ? "using" : "not using") << 
            " public address based on ability to reach destination address " << szAddress << " from local address " << getHost();
        }
        else {
          useExternalIp = !this->isInNetwork(host.c_str());
        }
      }
    }
    
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
    return useExternalIp;
  }

  sip_via_t* SipTransport::makeVia(su_home_t * h, const char* szRemoteUri) {
    bool isInSubnet = false;
    if (szRemoteUri) {
      string scheme, userpart, hostpart, port ;
      vector< pair<string,string> > vecParam ;
      string host = szRemoteUri ;

      if( parseSipUri(host, scheme, userpart, hostpart, port, vecParam) ) {
        isInSubnet = this->isInNetwork(hostpart.c_str());
      }
    }

    string host = this->getHost();
    if (this->hasExternalIp() && !isInSubnet) {
      host = this->getExternalIp();
    }

    string proto = this->getProtocol();
    if (0 == proto.length()) proto = "UDP";
    boost::to_upper(proto);

    string transport = string("SIP/2.0/") + proto;
    DR_LOG(log_debug) << "SipTransport::makeVia - host " << host << ", port " << this->getPort() << ", transport " << transport ;

    return sip_via_create(h, host.c_str(), this->getPort(), transport.c_str(), NULL);
  }
*/

  bool SipTransport::isIpV6(void) const {
    return hasTport() && NULL != strstr( getHost(), "[") && NULL != strstr( getHost(), "]") ;
  }

  bool SipTransport::isLocalhost(void) const {
    return hasTport() && (0 == strcmp(getHost(), "127.0.0.1") || 0 == strcmp(getHost(), "[::1]"));
  }

  bool SipTransport::isLocal(const char* szHost) const {
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
        DR_LOG(log_info) << "SipTransport::addTransports - creating transport: " << std::hex << tp << ": " << desc << " socket(" << std::dec << tport_socket(tp) << ")";

        std::shared_ptr<SipTransport> p = std::make_shared<SipTransport>( config ) ;
        string host = p->hasExternalIp() ? p->getExternalIp() : p->getHost();
        p->setTport(tp); 
        p->setTportName(tpn) ;
        m_mapTport2SipTransport.insert(mapTport2SipTransport::value_type(tp, p)) ;

        if (tpn->tpn_host && 0 == strcmp(tpn->tpn_host, "127.0.0.1")) {
          p->setLocalNet("127.0.0.1/32");
        }
        else if(/*!p->hasExternalIp() && */tpn->tpn_host && 0 == strncmp(tpn->tpn_host, "192.168.", 8)) {
          std::string s = tpn->tpn_host;
          s += "/16";
          p->setLocalNet(s.c_str());
        }
        else if(/*!p->hasExternalIp() && */tpn->tpn_host && 0 == strncmp(tpn->tpn_host, "172.", 4) && strlen(tpn->tpn_host) > 7) {
          char octet[3];
          strncpy(octet, tpn->tpn_host + 4, 2);
          int v = ::atoi(octet);
          if (v >= 16 && v <= 31) {
            std::string s = tpn->tpn_host;
            s += "/12";
            p->setLocalNet(s.c_str());
          }
        }
        else if(/*!p->hasExternalIp() && */tpn->tpn_host && 0 == strncmp(tpn->tpn_host, "10.", 3)) {
          std::string s = tpn->tpn_host;
          s += "/8";
          p->setLocalNet(s.c_str());
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

  std::shared_ptr<SipTransport> SipTransport::findAppropriateTransport(const char* remoteHost, const char* proto, const char* requestedHost) {
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


    // sort highest to lowest based on the perceived ability to route to the target subnet
    sort(candidates.begin(), candidates.end(), [host, requestedHost](const std::shared_ptr<SipTransport>& pA, const std::shared_ptr<SipTransport>& pB) {
      std::string desc_A, desc_B; 
      int score_A = pA->routingAbilityScore(host.c_str());
      int score_B = pB->routingAbilityScore(host.c_str());

      pA->getHostport(desc_A, false);
      pB->getHostport(desc_B, false);

      // give precedence to a specific requested interface (tie-breaker only)
      if (requestedHost && std::string::npos != desc_A.find(requestedHost)) score_A = 33; //score_A++;
      else if (requestedHost && std::string::npos != desc_B.find(requestedHost)) score_B = 33; //score_B++;

      DR_LOG(log_debug) <<  "SipTransport::findAppropriateTransport - scores for routing to " << 
        host << " " << " with request " << (requestedHost ? requestedHost : "(none)") << " - " << 
        desc_A.c_str() << " = " << score_A << "; " << 
        desc_B.c_str() << " = " << score_B;
      
      return score_A > score_B;
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
  void SipTransport::getAllHostports( vector<string>& vec ) {
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      std::shared_ptr<SipTransport> p = it->second ;
      string desc ;
      p->getHostport(desc, false);
      vec.push_back(desc) ;

      // push external ip as well, if one exists
      if (p->getHostport(desc, true)) {
        vec.push_back(desc);
      }
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