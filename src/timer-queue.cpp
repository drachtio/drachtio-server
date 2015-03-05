#include <sofia-sip/nta.h>

#ifndef TEST
#include "drachtio.h"
#include "controller.hpp"
#endif

#include "timer-queue.hpp"

namespace {
  void timer_function( void *rm, su_timer_t * timer, void* p ) {
    drachtio::TimerQueue* queue = static_cast<drachtio::TimerQueue*>( p ) ;
    queue->doTimer( timer ) ;
  }
}

namespace drachtio {
 queueEntry_t::queueEntry_t(TimerQueue* queue, TimerFunc f, void * functionArgs, su_time_t when) : m_queue(queue), 
  m_functionArgs(functionArgs), m_when(when), m_next(NULL), m_prev(NULL) {

    m_function = f ;
  }

  TimerQueue::TimerQueue(su_root_t* root) : m_root(root), m_head(NULL), m_tail(NULL), m_length(0), m_in_timer(0) {
    m_timer = su_timer_create(su_root_task(m_root), NTA_SIP_T1 / 8 ) ;
  }
  TimerQueue::~TimerQueue() {
    queueEntry_t* ptr = m_head ;
    while( ptr ) {
      queueEntry_t* p = ptr ;
      ptr = ptr->m_next ;
      delete p ;
      m_length-- ;
      m_head = ptr ;
    }
  }

  TimerEventHandle TimerQueue::add( TimerFunc f, void* functionArgs, uint32_t milliseconds ) {
    return add( f, functionArgs, milliseconds, su_now() ) ;
  }

  TimerEventHandle TimerQueue::add( TimerFunc f, void* functionArgs, uint32_t milliseconds, su_time_t now ) {

    su_time_t when = su_time_add(now, milliseconds) ;
    queueEntry_t* entry = new queueEntry_t(this, f, functionArgs, when) ;
    TimerEventHandle handle = entry ;
    assert(handle) ;
    int queueLength ;

    if( entry ) {
#ifndef TEST
      DR_LOG(log_debug) << "Adding entry to go off in " << std::dec << milliseconds << "ms" ;
#endif
      //std::cout << "Adding entry to go off in " << milliseconds << "ms" << std::endl;

      if( NULL == m_head ) {
        assert( NULL == m_tail ) ;
        m_head = m_tail = entry; 
#ifndef TEST
        DR_LOG(log_debug) << "Adding entry to the head (queue was empty)" ;
#endif
        //std::cout << "Adding entry to the head of the queue (it was empty)" << std::endl ;
      }
      else {
        queueEntry_t* ptr = m_head ;
        int idx = 0 ;
        do {
          if( su_time_cmp( when, ptr->m_when ) < 0) {
#ifndef TEST
            DR_LOG(log_debug) << "Adding entry at position " << std::dec << idx << " of the queue" ;
#endif
            //std::cout << "Adding entry at position " << idx << " of the queue" << std::endl ;
            entry->m_prev = ptr->m_prev ;
            entry->m_next = ptr ;
            ptr->m_prev = entry ;
            if( ptr == m_head ) m_head = entry ;
            break ;
          }
          idx++ ;
        } while( NULL != (ptr = ptr->m_next) ) ;
        if( NULL == ptr ) {
#ifndef TEST
          DR_LOG(log_debug) << "Adding entry to the tail of the queue" ;
#endif
          //std::cout << "Adding entry to the tail of the queue" << std::endl ;
          entry->m_prev = m_tail ;
          m_tail->m_next = entry ;
          m_tail = entry ;
        }
      }
      queueLength = ++m_length ;
    }
    else {
      //DR_LOG(log_error) << "Error allocating queue entry" ;
      //std::cerr << "Error allocating queue entry" << std::endl ;
      return NULL ;
    }

    //only need to set the timer if we added to the front
    if( m_head == entry ) {
      //DR_LOG(log_debug) << "timer add: Setting timer for " << milliseconds  << "ms" ;
      //std::cout << "timer add: Setting timer for " << milliseconds  << "ms" << std::endl;
      int rc = su_timer_set_at(m_timer, timer_function, this, when);
      assert( 0 == rc ) ;
    }

    //DR_LOG(log_debug) << "timer add: queue length is now " << queueLength ;
    //std::cout << "timer add: queue length is now " << queueLength << std::endl;

    //self check
    assert( 0 != m_length || (NULL == m_head && NULL == m_tail) ) ;
    assert( 1 != m_length || (m_head == m_tail)) ;
    assert( m_length < 2 || (m_head != m_tail)) ;

    return handle ;

  }
  void TimerQueue::remove( TimerEventHandle entry) {
    int queueLength ;
    {
      if( m_head == entry ) {
        m_head = entry->m_next ;
        if( m_head ) m_head->m_prev = NULL ;
        else m_tail = NULL ;
      }
      else if( m_tail == entry ) {
        m_tail = entry->m_prev ;
        if( m_tail ) m_tail->m_next = NULL ;
        else m_head = NULL ;
      }
      else {
        entry->m_prev->m_next = entry->m_next ;
        entry->m_next->m_prev = entry->m_prev ;
      }
      queueLength = --m_length ;
      assert( m_length >= 0 ) ;

      if( NULL == m_head ) {
#ifndef TEST
        DR_LOG(log_debug) << "timer not set (queue is empty after removal)" ;
#endif
        //std::cout << "timer not set (queue is empty after removal)"  << std::endl;
        su_timer_reset( m_timer ) ;
      }
      else if( m_head == entry->m_next ) {
#ifndef TEST
        DR_LOG(log_debug) << "Setting timer for " << std::dec << su_duration( m_head->m_when, su_now() )  << "ms after removal" ;
#endif
        //std::cout << "Setting timer for " << su_duration( m_head->m_when, su_now() )  << "ms after removal of head entry"  << std::endl;
        int rc = su_timer_set_at(m_timer, timer_function, this, m_head->m_when);
      }      
    }

    //DR_LOG(log_debug) << "timer remove: queue length is now " << queueLength ;
    //std::cout << "timer remove: queue length is now " << queueLength << std::endl;

    delete entry ;

    //self check
    assert( 0 != m_length || (NULL == m_head && NULL == m_tail) ) ;
    assert( 1 != m_length || (m_head == m_tail)) ;
    assert( m_length < 2 || (m_head != m_tail)) ;
  }

  void TimerQueue::doTimer(su_timer_t* timer) {
#ifndef TEST
    DR_LOG(log_debug) << "doTimer: running timer function" ;
#endif
    //std::cout << "doTimer: running timer function with " << m_length << " timers queued " << std::endl;

    if( m_in_timer ) return ;
    m_in_timer = 1 ;

    su_time_t now = su_now() ;
    assert( NULL != m_head ) ;

    queueEntry_t* ptr = m_head ;
    while( ptr && su_time_cmp( ptr->m_when, now ) < 0 ) {
      //std::cout << "expiring a timer" << std::endl ;
      m_length-- ;
      m_head = ptr->m_next ;
      if( m_head ) m_head->m_prev = NULL ;
      else m_tail = NULL ;

      ptr->m_function( ptr->m_functionArgs ) ;
      queueEntry_t* p = ptr ;
      ptr = ptr->m_next ;
      delete p ;
    }

    if( NULL == m_head ) {
      //DR_LOG(log_debug) << "timer not set (queue is empty after processing expired timers)" ;
      //std::cout << "doTimer: timer not set (queue is empty after processing expired timers)" << std::endl;
      assert( 0 == m_length ) ;
    }
    else {
      //std::cout << "doTimer: Setting timer for " << su_duration( m_head->m_when, su_now() )  << "ms after processing expired timers" << std::endl;
      //DR_LOG(log_debug) << "Setting timer for " << su_duration( m_head->m_when, su_now() )  << "ms after processing expired timers" ;
      int rc = su_timer_set_at(m_timer, timer_function, this, m_head->m_when);      
    }
    m_in_timer = 0 ;

    //self check
    assert( 0 != m_length || (NULL == m_head && NULL == m_tail) ) ;
    assert( 1 != m_length || (m_head == m_tail)) ;
    assert( m_length < 2 || (m_head != m_tail)) ;
  }    
  int TimerQueue::positionOf(TimerEventHandle handle) {
    int pos = 0 ;
    queueEntry_t* ptr = m_head ;
    while( ptr ) {
      if( ptr == handle ) return pos ;
      pos++ ;
      ptr = ptr->m_next ;
    }
    return -1 ;
  }
}