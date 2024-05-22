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
#ifndef __SIP_TRANSPORTS_HPP__
#define __SIP_TRANSPORTS_HPP__

#include <unordered_map>

#include <sofia-sip/nta.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>

using namespace std ;

namespace drachtio {

  class SipTransport : public std::enable_shared_from_this<SipTransport> {
  public:
    SipTransport(const string& contact, const string& localNet, const string& externalIp) ;
    SipTransport(const string& contact, const string& localNet) ;
    SipTransport(const string& contact) ;
    SipTransport(const std::shared_ptr<drachtio::SipTransport> other) ;

    ~SipTransport() ;

    void addDnsName(const string& name) { m_dnsNames.push_back(name); }

    void getContactUri(string& contact, bool useExternalIp = true) ;
    void getBindableContactUri(string& contact) ;

    sip_via_t* makeVia(su_home_t * home, const char* szRemoteHost = NULL) ;

    const string& getContact(void) { return m_strContact; }
    bool hasExternalIp(void) const { return !m_strExternalIp.empty() ; }
    const string& getExternalIp(void) const { return m_strExternalIp; }
    const vector<string>& getDnsNames(void) const  { return m_dnsNames; }
    const string& getLocalNet(void) const { return m_strLocalNet; }    
    void setLocalNet(const string& network, const string& bits) { m_strLocalNet = network + "/" + bits; }    
    bool shouldAdvertisePublic(const char* address) const ;
    bool isInNetwork(const char* address) const;
    bool hasTport(void) const { return NULL != m_tp; }
    bool hasTportAndTpname(void) const { return NULL != m_tp && NULL != m_tpName; }
    const tport_t* getTport(void) const { return m_tp; }
    void setTport(tport_t* tp) { 
      assert(!m_tp);
      m_tp = tp ;
    }
    void setTportName(const tp_name_t* tpn) { m_tpName = tpn; }
    const char* getHost(void) const { return m_tpName ? m_tpName->tpn_host : "" ; }
    const char* getPort(void) const { return m_tpName ? m_tpName->tpn_port : ""; }
    const char* getProtocol(void) const { return m_tpName ? m_tpName->tpn_proto : ""; }
    bool isIpV6(void) ;
    bool isLocalhost(void) ;
    bool isLocal(const char* szHost) ;
    bool isSips(void) { return 0 == m_contactScheme.compare("sips"); }

    void setNetwork(const string& network) { m_network = network;}
    void setNetmask(uint32_t netmask) { m_netmask = netmask;}
    void setRange(uint32_t range) { m_range = range;}

    void getDescription(string& s, bool shortVersion = true) ;
    void getHostport(string& s, bool localIpsOnly = false) ;

    uint32_t getOctetMatchCount(const string& address);

    static void addTransports(std::shared_ptr<SipTransport> config, unsigned int mtu);
    static std::shared_ptr<SipTransport> findTransport(tport_t* tp) ;
    static std::shared_ptr<SipTransport> findAppropriateTransport(const char* remoteHost, const char* proto = "udp") ;
    static void logTransports() ;
    static void getAllHostports( vector<string>& vec, bool localIpsOnly = false ) ;
    static void getAllExternalIps( vector<string>& vec ) ;
    static void getAllExternalContacts( vector< pair<string, string> >& vec ) ;
    static bool isLocalAddress(const char* szHost, tport_t* tp = NULL) ;
    
  protected:
    void init() ;

    typedef std::unordered_map<tport_t*, std::shared_ptr<SipTransport> > mapTport2SipTransport ;

    static mapTport2SipTransport m_mapTport2SipTransport ;
    static std::shared_ptr<SipTransport> m_masterTransport ;

    // these are loaded from config
    string  m_strContact ;
    string  m_strExternalIp ;
    string  m_strLocalNet ;

    // these are computed from the above
    string m_network ;
    uint32_t  m_netmask ;
    uint32_t  m_range ;

    string m_contactScheme ;
    string m_contactUserpart ;
    string m_contactHostpart ;
    string m_contactPort ;
    std::vector< pair<string,string> > m_contactParams ;
    std::vector<string> m_dnsNames; 

    // these are given when we actually create a transport with the info above
    tport_t* m_tp;
    const tp_name_t*  m_tpName ;
  }  ;

}

#endif
