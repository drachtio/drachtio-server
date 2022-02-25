#include "timer-queue-manager.hpp"
#include "drachtio.h"
#include "controller.hpp"

namespace drachtio {
  void SipTimerQueueManager::logQueueSizes(void) {
    DR_LOG(log_debug) << "general queue size:                                              " << m_queue.size() ;
    DR_LOG(log_debug) << "timer A queue size:                                              " << m_queueA.size() ;
    DR_LOG(log_debug) << "timer B queue size:                                              " << m_queueB.size() ;
    DR_LOG(log_debug) << "timer C queue size:                                              " << m_queueC.size() ;
    DR_LOG(log_debug) << "timer D queue size:                                              " << m_queueD.size() ;
    DR_LOG(log_debug) << "timer E queue size:                                              " << m_queueE.size() ;
    DR_LOG(log_debug) << "timer F queue size:                                              " << m_queueF.size() ;
    DR_LOG(log_debug) << "timer G queue size:                                              " << m_queueF.size() ;
    DR_LOG(log_debug) << "timer K queue size:                                              " << m_queueK.size() ;
  }
}