/*
Copyright (c) 2020, David C Horton

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
#ifndef __SIP_IIP_HPP__
#define __SIP_IIP_HPP__

#include <iostream>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/identity.hpp>

#include <sofia-sip/nta.h>

#include "drachtio.h"
#include "sip-dialog.hpp"

using namespace ::boost::multi_index;

namespace drachtio {

	class DrachtioController ;

  struct PtrTag{};
  struct TimeTag{};
  struct IrqTag{};
  struct OrqTag{};
  struct LegTag{};
  struct RelTag{};
  struct TransactionIdTag{};

	/* invites in progress */
	class IIP  : public std::enable_shared_from_this<IIP> {
  public:
		IIP(nta_leg_t* leg, nta_incoming_t* irq, const std::string& transactionId, std::shared_ptr<SipDialog> dlg);
		IIP(nta_leg_t* leg, nta_outgoing_t* orq, const string& transactionId, std::shared_ptr<SipDialog> dlg);

		~IIP();
    bool operator <(const IIP& a) const { return m_tmCreated < a.m_tmCreated; }

		const nta_leg_t* leg(void) const { return m_leg; }
		const nta_incoming_t* irq(void) const { return m_irq; }
		const nta_outgoing_t* orq(void) const { return m_orq; }
		const nta_reliable_t* rel(void) const { return m_rel; }
		const string& getTransactionId(void) const { return m_strTransactionId; }

    const agent_role role() const { return m_role; }

		void setReliable(nta_reliable_t* rel) { m_rel = rel; }
		void destroyReliable(void) {
			if( m_rel ) {
				nta_reliable_destroy( m_rel ) ;
				m_rel = NULL ;
			}
		}
		std::shared_ptr<SipDialog> dlg(void) { return m_dlg; }

		void setCanceled(void) { m_bCanceled = true; }
		bool isCanceled(void) { return m_bCanceled; }

    void startMaxProceedingTimer(void);
    void cancelMaxProceedingTimer(void);
    void doMaxProceedingTimerHandling(void);
  
    friend std::ostream& operator<<(std::ostream& os, const IIP& iip);

  private:

		nta_incoming_t*	m_irq ;
		nta_outgoing_t*	m_orq ;
		nta_leg_t* 		m_leg ;
		string 			  m_strTransactionId ;
		nta_reliable_t*	m_rel ;
		std::shared_ptr<SipDialog> 	m_dlg ;
		agent_role		m_role ;
		bool 					m_bCanceled;
    sip_time_t    m_tmCreated;
    su_timer_t*   m_timerMaxProceeding;
    std::weak_ptr<IIP>* m_ppSelf ;
	} ;


  typedef multi_index_container<
    std::shared_ptr<IIP>,
    indexed_by<
      hashed_unique<
        boost::multi_index::tag<PtrTag>,
        boost::multi_index::identity< std::shared_ptr<IIP> >
      >,
      ordered_non_unique<
        boost::multi_index::tag<TimeTag>,
        boost::multi_index::identity<IIP> 
      >,
      hashed_non_unique<
        boost::multi_index::tag<IrqTag>,
        boost::multi_index::const_mem_fun<IIP, const nta_incoming_t*, &IIP::irq>
      >,
      hashed_non_unique<
        boost::multi_index::tag<OrqTag>,
        boost::multi_index::const_mem_fun<IIP, const nta_outgoing_t*, &IIP::orq>
      >,
      hashed_non_unique<
        boost::multi_index::tag<LegTag>,
        boost::multi_index::const_mem_fun<IIP, const nta_leg_t*, &IIP::leg>
      >,
      hashed_non_unique<
        boost::multi_index::tag<RelTag>,
        boost::multi_index::const_mem_fun<IIP, const nta_reliable_t*, &IIP::rel>
      >,
      hashed_unique<
        boost::multi_index::tag<TransactionIdTag>,
        boost::multi_index::const_mem_fun<IIP, const std::string&, &IIP::getTransactionId>
      >
    >
  > InvitesInProgress_t;

  void IIP_Insert(InvitesInProgress_t& iips, nta_leg_t* leg, nta_incoming_t* irq, const std::string& transactionId, std::shared_ptr<SipDialog>& dlg);
  void IIP_Insert(InvitesInProgress_t& iips, nta_leg_t* leg, nta_outgoing_t* irq, const std::string& transactionId, std::shared_ptr<SipDialog>& dlg);
  
  bool IIP_FindByIrq(const InvitesInProgress_t& iips, nta_incoming_t* irq, std::shared_ptr<IIP>& iip);
  bool IIP_FindByOrq(const InvitesInProgress_t& iips, nta_outgoing_t* orq, std::shared_ptr<IIP>& iip) ;
  bool IIP_FindByLeg(const InvitesInProgress_t& iips, nta_leg_t* leg, std::shared_ptr<IIP>& iip);
  bool IIP_FindByReliable(const InvitesInProgress_t& iips, nta_reliable_t* rel, std::shared_ptr<IIP>& iip) ;
  bool IIP_FindByTransactionId(const InvitesInProgress_t& iips, const std::string& transactionId, std::shared_ptr<IIP>& iip) ;
  void IIP_Clear(InvitesInProgress_t& iips, std::shared_ptr<IIP>& iip);
  void IIP_Clear(InvitesInProgress_t& iips, nta_leg_t* leg);
  size_t IIP_Size(const InvitesInProgress_t& iips);
  void IIP_SetReliable(InvitesInProgress_t& iips, std::shared_ptr<IIP>& iip, nta_reliable_t* rel);
  void IIP_Log(const InvitesInProgress_t& iips, bool full = false);
}

#endif
