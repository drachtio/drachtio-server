#include "timer-queue-manager.hpp"
#include "drachtio.h"
#include "controller.hpp"

namespace drachtio {
  void SipTimerQueueManager::logQueueSizes(void) {
    DR_LOG(log_debug) << "queue size:                                                      " << m_queue.size() ;
    DR_LOG(log_debug) << "timer B queue size:                                              " << m_queueB.size() ;
    DR_LOG(log_debug) << "timer C queue size:                                              " << m_queueC.size() ;
    DR_LOG(log_debug) << "timer D queue size:                                              " << m_queueD.size() ;
  }
}