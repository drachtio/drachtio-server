#include "timer-queue-manager.hpp"
#include "drachtio.h"
#include "controller.hpp"

namespace drachtio {
  void SipTimerQueueManager::logQueueSizes(void) {
    DR_LOG(log_info) << "queue size:                                                      " << m_queue.size() ;
    DR_LOG(log_info) << "timer B queue size:                                              " << m_queueB.size() ;
    DR_LOG(log_info) << "timer C queue size:                                              " << m_queueC.size() ;
    DR_LOG(log_info) << "timer D queue size:                                              " << m_queueD.size() ;
  }
}