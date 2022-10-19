#include "invite-in-progress.hpp"
#include "controller.hpp"

#define MAX_PROCEEDING_DURATION (180000)

#include <mutex>
namespace {
    void max_proceeding_timer_handler( su_root_magic_t* magic, su_timer_t* timer, su_timer_arg_t* args) {
      std::weak_ptr<drachtio::IIP> *p = reinterpret_cast< std::weak_ptr<drachtio::IIP> *>( args ) ;
      std::shared_ptr<drachtio::IIP> pIIP = p->lock() ;
      if( pIIP ) pIIP->doMaxProceedingTimerHandling() ;
      else assert(0) ;
    }

  std::mutex	iip_mutex;
}
namespace drachtio {

  IIP::IIP(nta_leg_t* leg, nta_incoming_t* irq, const std::string& transactionId, std::shared_ptr<SipDialog> dlg) : 
    m_leg(leg), m_irq(irq), m_orq(nullptr), m_strTransactionId(transactionId), m_dlg(dlg),
    m_role(uas_role),m_rel(nullptr), m_bCanceled(false), m_tmCreated(sip_now()), m_ppSelf(nullptr), m_timerMaxProceeding(nullptr) {
      DR_LOG(log_debug) << "adding IIP for incoming call " << *this;
    }

  IIP::IIP(nta_leg_t* leg, nta_outgoing_t* orq, const string& transactionId, std::shared_ptr<SipDialog> dlg) : 
    m_leg(leg), m_irq(nullptr), m_orq(orq), m_strTransactionId(transactionId), m_dlg(dlg),
    m_role(uac_role),m_rel(nullptr), m_bCanceled(false), m_tmCreated(sip_now()), m_ppSelf(nullptr), m_timerMaxProceeding(nullptr) {
      DR_LOG(log_debug) << "adding IIP for outgoing call " << *this;
    }

  IIP::~IIP() {
    cancelMaxProceedingTimer() ;
		if( m_ppSelf) delete m_ppSelf ;
  }
  
  void IIP::doMaxProceedingTimerHandling(void) {
    theOneAndOnlyController->getDialogController()->notifyMaxProceedingReachedIIP( shared_from_this() ) ;
		delete m_ppSelf ; 
    m_ppSelf = nullptr ; 
    m_timerMaxProceeding = nullptr;
    DR_LOG(log_debug) << "IIP::doMaxProceedingTimerHandling " << *this;
  }

  void IIP::cancelMaxProceedingTimer() {
		if (m_timerMaxProceeding) {
      DR_LOG(log_debug) << "IIP::doMaxProceedingTimerHandling " << *this;
      su_timer_destroy( m_timerMaxProceeding ) ;
    }
		m_timerMaxProceeding = nullptr ;
	}

  void IIP::startMaxProceedingTimer() {
    assert(!m_timerMaxProceeding) ;
    assert(!m_ppSelf) ;
    DR_LOG(log_debug) << "IIP::startMaxProceedingTimer " << *this;
    m_ppSelf = new std::weak_ptr<IIP>( shared_from_this() ) ;
    m_timerMaxProceeding = su_timer_create( su_root_task(theOneAndOnlyController->getRoot()), MAX_PROCEEDING_DURATION ) ;
	}

  std::ostream& operator<<(std::ostream& os, const IIP& iip) {
    sip_time_t alive = sip_now() - iip.m_tmCreated;
    os << "tid:" << iip.getTransactionId() << std::dec << 
      " alive:" << alive << "s" << std::hex << 
      " leg:" << iip.leg() << 
      " irq:" << iip.irq() <<
      " orq:" << iip.orq() <<
      " rel:" << iip.rel() ;
    return os;
  }

  void IIP_Insert(InvitesInProgress_t& iips, nta_leg_t* leg, nta_incoming_t* irq, const std::string& transactionId, std::shared_ptr<SipDialog>& dlg) {
    std::shared_ptr<IIP> iip = std::make_shared<IIP>(leg, irq, transactionId, dlg);
    DR_LOG(log_debug) << "IIP_Insert incoming - ref count: " << iip.use_count() << " inserting " << *iip;
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto& idx = iips.get<PtrTag>();
    auto res = idx.insert(iip);
    if (!res.second) {
	    DR_LOG(log_error) << "IIP_Insert failed to insert incoming IIP " << *iip;
		}
    iip->startMaxProceedingTimer();
  }
  void IIP_Insert(InvitesInProgress_t& iips, nta_leg_t* leg, nta_outgoing_t* orq, const std::string& transactionId, std::shared_ptr<SipDialog>& dlg) {
    std::shared_ptr<IIP> iip = std::make_shared<IIP>(leg, orq, transactionId, dlg);
    DR_LOG(log_debug) << "IIP_Insert outgoing - ref count: " << iip.use_count() << " inserting " << *iip;
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto& idx = iips.get<PtrTag>();
    auto res = idx.insert(iip);
    if (!res.second) {
	    DR_LOG(log_error) << "IIP_Insert failed to insert outgoing IIP " << *iip;
		}
    iip->startMaxProceedingTimer();
  }

  bool IIP_FindByIrq(const InvitesInProgress_t& iips, nta_incoming_t* irq, std::shared_ptr<IIP>& iip) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto &idx = iips.get<IrqTag>();
    auto it = idx.find(irq);
    if (it == idx.end()) return false;
    iip = *it;
    return true;
  }

  bool IIP_FindByOrq(const InvitesInProgress_t& iips, nta_outgoing_t* orq, std::shared_ptr<IIP>& iip) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto &idx = iips.get<OrqTag>();
    auto it = idx.find(orq);
    if (it == idx.end()) return false;
    iip = *it;
    return true;
  }

  bool IIP_FindByLeg(const InvitesInProgress_t& iips, nta_leg_t* leg, std::shared_ptr<IIP>& iip) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto &idx = iips.get<LegTag>();
    auto it = idx.find(leg);
    if (it == idx.end()) return false;
    iip = *it;
    return true;
  }

  bool IIP_FindByReliable(const InvitesInProgress_t& iips, nta_reliable_t* rel, std::shared_ptr<IIP>& iip) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto &idx = iips.get<RelTag>();
    auto it = idx.find(rel);
    if (it == idx.end()) return false;
    iip = *it;
    return true;
  }

  bool IIP_FindByTransactionId(const InvitesInProgress_t& iips, const std::string& transactionId, std::shared_ptr<IIP>& iip) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto &idx = iips.get<TransactionIdTag>();
    auto it = idx.find(transactionId);
    if (it == idx.end()) return false;
    iip = *it;
    return true;
  }

  void IIP_Clear(InvitesInProgress_t& iips, nta_leg_t* leg) {
    std::shared_ptr<IIP> iip;
    if (IIP_FindByLeg(iips, leg, iip)) {
      IIP_Clear(iips, iip);
    }
  }

  void IIP_Clear(InvitesInProgress_t& iips, std::shared_ptr<IIP>& iip) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;

    nta_incoming_t* irq = const_cast<nta_incoming_t*>(iip->irq());
    nta_outgoing_t* orq = const_cast<nta_outgoing_t*>(iip->orq());
    nta_reliable_t* rel = const_cast<nta_reliable_t*>(iip->rel());
    if (irq) nta_incoming_destroy(irq) ;

        // DH: tmp commented this out as it appears to cause a crash
        // https://github.com/davehorton/drachtio-server/issues/76#event-2662761148
        // this needs investigation, because it also causes a memory leak
        //if( orq ) nta_outgoing_destroy( orq ) ;
        //
        // later note: the orq of the uac INVITE (as well as orq of uac ACK) is destroyed in SipDialog destructor

    if (rel) nta_reliable_destroy(rel) ;
   
    auto &idx = iips.get<LegTag>();
    idx.erase(iip->leg());
  }

  size_t IIP_Size(const InvitesInProgress_t& iips) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    auto &idx = iips.get<TransactionIdTag>();
    return idx.size();
  }

  /* since we are changing a key of the multiindex we need to use modify on the index */
  void IIP_SetReliable(InvitesInProgress_t& iips, std::shared_ptr<IIP>& iip, nta_reliable_t* rel) {
    std::lock_guard<std::mutex> lock(iip_mutex) ;
    const std::string& transactionId = iip->getTransactionId();
    auto &idx = iips.get<TransactionIdTag>();
    auto it = idx.find(transactionId);
    idx.modify(it, [rel](std::shared_ptr<IIP>& iip) {
      iip->setReliable(rel);
    });
  }
 
  void IIP_Log(const InvitesInProgress_t& iips, bool full) {
    size_t count = IIP_Size(iips);
    DR_LOG(log_debug) << "IIP size:                                                " << count;
    if (full && count) {
      std::lock_guard<std::mutex> lock(iip_mutex) ;
      auto &idx = iips.get<TimeTag>();
      for (auto it = idx.begin(); it != idx.end(); ++it) {
        std::shared_ptr<IIP> p = *it;
        DR_LOG(log_info) << *p;
      }
    }
  }

}