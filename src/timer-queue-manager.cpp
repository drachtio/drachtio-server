#include "timer-queue-manager.hpp"
#include "drachtio.h"
#include "controller.hpp"

namespace drachtio {
  void SipTimerQueueManager::logQueueSizes(void) {
    DR_LOG(log_info) << "general queue size:                                              " << m_queue.size() ;
    DR_LOG(log_info) << "timer A queue size:                                              " << m_queueA.size() ;
    DR_LOG(log_info) << "timer B queue size:                                              " << m_queueB.size() ;
    DR_LOG(log_info) << "timer C queue size:                                              " << m_queueC.size() ;
    DR_LOG(log_info) << "timer D queue size:                                              " << m_queueD.size() ;
    DR_LOG(log_info) << "timer E queue size:                                              " << m_queueE.size() ;
    DR_LOG(log_info) << "timer F queue size:                                              " << m_queueF.size() ;
    DR_LOG(log_info) << "timer G queue size:                                              " << m_queueF.size() ;
    DR_LOG(log_info) << "timer K queue size:                                              " << m_queueK.size() ;
  }
}