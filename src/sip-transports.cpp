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

#include <boost/regex.hpp>
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
    m_strContact(contact), m_strExternalIp(externalIp), m_strLocalNet(localNet), m_tp(NULL) {
    init() ;
  }
  SipTransport::SipTransport(const string& contact, const string& localNet) :
    m_strContact(contact), m_strLocalNet(localNet), m_tp(NULL) {
    init() ;
  }
  SipTransport::SipTransport(const string& contact) : m_strContact(contact), m_tp(NULL) {
    init() ;
  }
  SipTransport::SipTransport(const boost::shared_ptr<drachtio::SipTransport> other) :
    m_strContact(other->m_strContact), m_strLocalNet(other->m_strLocalNet), m_strExternalIp(other->m_strExternalIp), m_tp(NULL) {
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
      boost::regex e("^([0-9\\.]*)$", boost::regex::extended) ;
      boost::smatch mr; 
      if (!boost::regex_search(m_strExternalIp, mr, e)) {
        cerr << "SipTransport::init - invalid format for externalIp, must be ipv4 dot decimal address: " << m_strExternalIp << endl ;
        throw std::runtime_error("SipTransport::init - invalid format for externalIp, must be ipv4 dot decimal address");
      }
    }

    if( !m_strLocalNet.empty() ) {
      boost::regex e("^([0-9\\.]*)\\/(.*)$", boost::regex::extended) ;
      boost::smatch mr; 
      if (!boost::regex_search(m_strLocalNet, mr, e)) {
        cerr << "SipTransport::init - invalid format for localNet, must be CIDR format: " << m_strLocalNet << endl ;
        throw std::runtime_error("SipTransport::init - invalid format for localNet, must be CIDR format");
      }

      string network = mr[1] ; 
      struct sockaddr_in range;
      inet_pton(AF_INET, network.c_str(), &(range.sin_addr));
      m_range = htonl(range.sin_addr.s_addr) ;

      string tmp = mr[2] ;
      uint32_t netbits = ::atoi( tmp.c_str() ) ;    
      m_netmask = ~(~uint32_t(0) >> netbits);    
    }
  }

  bool SipTransport::shouldAdvertisePublic( const char* address ) const {
    if( !hasExternalIp() ) return false ;

    return !isInNetwork(address) ;
  }

  bool SipTransport::isInNetwork(const char* address) const {
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
  }

  void SipTransport::getHostport(string& s) {
    assert(hasTport()) ;
    s = "" ;
    s += getProtocol() ;
    s += "/" ;
    s += hasExternalIp() ? getExternalIp() : getHost() ;
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

  /** static methods */

  SipTransport::mapTport2SipTransport SipTransport::m_mapTport2SipTransport  ;
  boost::shared_ptr<SipTransport> SipTransport::m_masterTransport ;

  /**
   * iterate all tport_t* that have been created, and any that are "unassigned" associate
   * them with the provided SipTransport 
   * 
   * @param config - SipTransport that was used to create the currently unassigned tport_t* 
   */
  void SipTransport::addTransports(boost::shared_ptr<SipTransport> config) {


    tport_t* tp = nta_agent_tports(theOneAndOnlyController->getAgent());
    m_masterTransport = boost::make_shared<SipTransport>( config ) ;
    m_masterTransport->setTport(tp) ;

    while (NULL != (tp = tport_next(tp) )) {
      const tp_name_t* tpn = tport_name(tp) ;
      string desc ;
      getTransportDescription( tp, desc ); 

      mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.find(tp) ;
      if (it == m_mapTport2SipTransport.end()) {
        DR_LOG(log_info) << "SipTransport::addTransports - creating transport: " << hex << tp << ": " << desc ;

        boost::shared_ptr<SipTransport> p = boost::make_shared<SipTransport>( config ) ;
        p->setTport(tp); 
        p->setTportName(tpn) ;
        m_mapTport2SipTransport.insert(mapTport2SipTransport::value_type(tp, p)) ;
      }
    }
  }

  boost::shared_ptr<SipTransport> SipTransport::findTransport(tport_t* tp) {
    mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.find(tp) ;
    if( it == m_mapTport2SipTransport.end() ) {
      tport_t* tpp = tport_parent(tp);
      DR_LOG(log_debug) << "SipTransport::findTransport - could not find transport: " << hex << tp << " searching for parent " <<  tpp ;
      it = m_mapTport2SipTransport.find(tport_parent(tpp));
    }
    assert( it != m_mapTport2SipTransport.end() ) ;
    return it->second ;
  }

  boost::shared_ptr<SipTransport> SipTransport::findAppropriateTransport(const char* remoteHost, const char* proto) {
    string scheme, userpart, hostpart, port ;
    vector< pair<string,string> > vecParam ;
    string host = remoteHost ;

    DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: " << proto << "/" << remoteHost ;

    if( parseSipUri(host, scheme, userpart, hostpart, port, vecParam) ) {
      host = hostpart ;
      DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: host parsed as " << host;
    }

    string desc ;
    bool wantsIpV6 = (NULL != strstr( remoteHost, "[") && NULL != strstr( remoteHost, "]")) ;
    boost::shared_ptr<SipTransport> pBestMatchSoFar ;
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      boost::shared_ptr<SipTransport> p = it->second ;
      p->getDescription(desc) ;

      if( 0 != strcmp( p->getProtocol(), proto) ) {
        DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - skipping transport " << hex << p->getTport() << 
          " because protocol does not match: " << desc ;
        continue ;
      }
      if( wantsIpV6 && !p->isIpV6() ) {
        DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - skipping transport " << hex << p->getTport() << 
          " because transport is not ipv6: " << desc ;
        continue ;        
      }
      if( !wantsIpV6 && p->isIpV6() ) {
        DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - skipping transport " << hex << p->getTport() << 
          " because transport is not ipv4: " << desc ;
        continue ;        
      }

      // check if remote host is on the local network of the transport, as this will be the optimal match
      if( p->hasExternalIp() && p->isInNetwork(remoteHost) ) {
        DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - selecting transport " << hex << p->getTport() << 
          " because it is in the local network: " << desc ;
        return p ;
      }

      if( 0 == strcmp(remoteHost, p->getHost())) {
        DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - selecting transport " << hex << p->getTport() << 
          " because it matches exactly (probably localhost): " << desc ;
        return p ;
      }

      if( p->isLocalhost() ) {
        //only select if we don't have something better
        if( !pBestMatchSoFar ) {
          DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - setting transport " << hex << p->getTport() << 
            " as best match because although its localhost we have no current best match: " << desc ;
          pBestMatchSoFar = p ;
        }
      }
      else {
        DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - setting transport " << hex << p->getTport() << 
          " as best match: " << desc ;
        pBestMatchSoFar = p ;        
      }
    }

    if (pBestMatchSoFar) {
      pBestMatchSoFar->getDescription(desc) ;
      DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - returning transport " << hex << pBestMatchSoFar->getTport() << 
        " as final best match: " << desc ;
      return pBestMatchSoFar ;
    }

    m_masterTransport->getDescription(desc) ;
    DR_LOG(log_debug) << "SipTransport::findAppropriateTransport: - returning master transport " << hex << m_masterTransport->getTport() << 
      " as we found no better matches: " << desc ;
    return m_masterTransport ;
  }

  void SipTransport::getAllExternalIps( vector<string>& vec ) {
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      boost::shared_ptr<SipTransport> p = it->second ;
      if( p->hasExternalIp() ) {
        vec.push_back(p->getExternalIp()) ;
      }
    }
  }
  void SipTransport::getAllHostports( vector<string>& vec ) {
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      boost::shared_ptr<SipTransport> p = it->second ;
      string desc ;
      p->getHostport(desc);
      vec.push_back(desc) ;
    }
  }

  bool SipTransport::isLocalAddress(const char* szHost, tport_t* tp) {
    if( tp ) {
     boost::shared_ptr<SipTransport> p = SipTransport::findTransport(tp) ;
     return p->isLocalAddress(szHost); 
    }

    // search all
    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      boost::shared_ptr<SipTransport> p = it->second ;
      if( p->isLocalAddress(szHost) ) {
        return true ;
      }
    }

    return false;
  }

  void SipTransport::logTransports() {
    DR_LOG(log_info) << "SipTransport::logTransports - there are : " << dec <<  m_mapTport2SipTransport.size() << " transports";

    for (mapTport2SipTransport::const_iterator it = m_mapTport2SipTransport.begin(); m_mapTport2SipTransport.end() != it; ++it ) {
      boost::shared_ptr<SipTransport> p = it->second ;
      string desc ;
      p->getDescription(desc, false) ;
        DR_LOG(log_info) << "SipTransport::logTransports - " << desc ;
    }
  }


}