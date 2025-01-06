#include <cassert>

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
  m_functionArgs(functionArgs), m_when(when), m_next(NULL), m_prev(NULL), m_function(f) {
  }

  TimerQueue::TimerQueue(su_root_t* root, const char* szName) : m_root(root), m_head(NULL), m_tail(NULL), 
    m_length(0), m_in_timer(0) {
    m_name.assign( szName ? szName : "timer") ;
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
    return TimerQueue::add( f, functionArgs, milliseconds, su_now() ) ;
  }

  TimerEventHandle TimerQueue::add( TimerFunc f, void* functionArgs, uint32_t milliseconds, su_time_t now ) {
    //self check
    /*
    assert( m_length == numberOfElements()) ;
    assert( 0 != m_length || (NULL == m_head && NULL == m_tail) ) ;
    assert( 1 != m_length || (m_head == m_tail)) ;
    assert( m_length < 2 || (m_head != m_tail)) ;
    assert( !(NULL == m_head && NULL != m_tail)) ;
    assert( !(NULL == m_tail && NULL != m_head)) ;
    */
  
    su_time_t when = su_time_add(now, milliseconds) ;
    queueEntry_t* entry = new queueEntry_t(this, f, functionArgs, when) ;
    TimerEventHandle handle = entry ;
    assert(handle) ;

    if( entry ) {
#ifndef TEST
      DR_LOG(log_debug) << m_name << ": Adding entry to go off in " << std::dec << milliseconds << "ms" ;
#endif
      //std::cout << "Adding entry to go off in " << milliseconds << "ms" << std::endl;

      if( NULL == m_head ) {
        assert( NULL == m_tail ) ;
        m_head = m_tail = entry; 
#ifndef TEST
        DR_LOG(log_debug) << m_name << ": Adding entry to the head (queue was empty), length: " << dec << m_length+1 ;
#endif
        //std::cout << "Adding entry to the head of the queue (it was empty)" << std::endl ;
      }
      else if( NULL != m_tail && su_time_cmp( when, m_tail->m_when ) > 0) {
        //one class of timer queues will always be appending entries, so check the tail
        //before starting to iterate through
#ifndef TEST
          DR_LOG(log_debug) << m_name << ": Adding entry to the tail of the queue: length " << dec << m_length+1;
#endif
          //std::cout << "Adding entry to the tail of the queue" << std::endl ;
          entry->m_prev = m_tail ;
          m_tail->m_next = entry ;
          m_tail = entry ;
      }
      else { 
        //iterate
        queueEntry_t* ptr = m_head ;
        int idx = 0 ;
        do {
          if( su_time_cmp( when, ptr->m_when ) < 0) {
#ifndef TEST
            DR_LOG(log_debug) << m_name << ": Adding entry at position " << std::dec << idx << " of the queue, length: " << dec << m_length+1 ;
#endif
            //std::cout << "Adding entry at position " << idx << " of the queue" << std::endl ;
            entry->m_next = ptr ;
            if( 0 == idx ) {
              m_head = entry ;
            }   
            else {
              entry->m_prev = ptr->m_prev ; 
              ptr->m_prev->m_next = entry ;

            }         
            ptr->m_prev = entry ;
            break ;
          }
          idx++ ;
        } while( NULL != (ptr = ptr->m_next) ) ;
        assert( NULL != ptr ) ;
      }
    }
    else {
      //DR_LOG(log_error) << "Error allocating queue entry" ;
      //std::cerr << "Error allocating queue entry" << std::endl ;
      return NULL ;
    }

    //only need to set the timer if we added to the front
    if( m_head == entry ) {
      int rc = su_timer_set_at(m_timer, timer_function, this, when);
      assert( 0 == rc ) ;
    }


     //self check
     /*
    assert( m_length == numberOfElements()) ;
    assert( 0 != m_length || (NULL == m_head && NULL == m_tail) ) ;
    assert( 1 != m_length || (m_head == m_tail)) ;
    assert( m_length < 2 || (m_head != m_tail)) ;
    assert( !(NULL == m_head && NULL != m_tail)) ;
    assert( !(NULL == m_tail && NULL != m_head)) ;
    */

    return handle ;

  }
  void TimerQueue::remove( TimerEventHandle entry) {

#ifndef TEST
        DR_LOG(log_debug) << m_name << ": removing entry, prior to removal length: " << dec << m_length;
#endif

    {
      if( m_head == entry ) {
        m_head = entry->m_next ;
        if( m_head ) m_head->m_prev = NULL ;
        else {
          assert( 1 == m_length ) ;
          m_tail = NULL ;
        }
      }
      else if( m_tail == entry ) {
        assert( m_head && entry->m_prev ) ;
        m_tail = entry->m_prev ;
        m_tail->m_next = NULL ;
      }
      else {
        assert( entry->m_prev ) ;
        assert( entry->m_next ) ;
        entry->m_prev->m_next = entry->m_next ;
        entry->m_next->m_prev = entry->m_prev ;
      }
      m_length-- ;
      assert( m_length >= 0 ) ;

      if( NULL == m_head ) {
#ifndef TEST
        DR_LOG(log_debug) << m_name << ": removed entry, timer not set (queue is empty after removal), length: " << dec << m_length;
#endif
        //std::cout << "timer not set (queue is empty after removal)"  << std::endl;
        su_timer_reset( m_timer ) ;
      }
      else if( m_head == entry->m_next ) {
#ifndef TEST
        DR_LOG(log_debug) << m_name << ": removed entry, setting timer for " << std::dec << su_duration( m_head->m_when, su_now() )  << 
          "ms after removal, length: " << dec << m_length;
#endif
        //std::cout << "Setting timer for " << su_duration( m_head->m_when, su_now() )  << "ms after removal of head entry"  << std::endl;
        su_timer_set_at(m_timer, timer_function, this, m_head->m_when);
      }      
    }

    delete entry ;
  }

  void TimerQueue::doTimer(su_timer_t* timer) {

#ifndef TEST
    DR_LOG(log_debug) << m_name << ": running timer function" ;
#endif
    //std::cout << "doTimer: running timer function with " << m_length << " timers queued " << std::endl;

    if( m_in_timer ) return ;
    m_in_timer = 1 ;

    queueEntry_t* expired = NULL ;
    queueEntry_t* tailExpired = NULL ;

    su_time_t now = su_now() ;
    assert( NULL != m_head ) ;

    queueEntry_t* ptr = m_head ;
    while( ptr && su_time_cmp( ptr->m_when, now ) < 0 ) {
      //std::cout << "expiring a timer" << std::endl ;
      m_length-- ;
      m_head = ptr->m_next ;
      if( m_head ) m_head->m_prev = NULL ;
      else m_tail = NULL ;

      //detach and assemble them into a new queue temporarily
      if( !expired ) {
        expired = tailExpired = ptr ;
        ptr->m_prev = ptr->m_next = NULL ;
      }
      else {
        tailExpired->m_next = ptr ;
        ptr->m_prev = tailExpired ;
        tailExpired = ptr ;
      }
      ptr = ptr->m_next ;
    }

    if( NULL == m_head ) {
#ifndef TEST
      DR_LOG(log_debug) << m_name << ": timer not set (queue is empty after processing expired timers), length: " << dec << m_length ;
#endif
      assert( 0 == m_length ) ;
    }
    else {
#ifndef TEST
      DR_LOG(log_debug) << m_name << ": Setting timer for " << su_duration( m_head->m_when, su_now() )  << 
        "ms after processing expired timers, length: "  << dec << m_length ;
#endif
      int rc = su_timer_set_at(m_timer, timer_function, this, m_head->m_when);      
    }
    m_in_timer = 0 ;

    while( NULL != expired ) {
      expired->m_function( expired->m_functionArgs ) ;
      queueEntry_t* p = expired ;
      expired = expired->m_next ;
      delete p ;
    }    
  }

  int TimerQueue::positionOf(TimerEventHandle handle) {
    int pos = 0 ;
    queueEntry_t* ptr = m_head ;
    while( ptr ) {
      if( ptr == handle ) {
        return pos ;
      }
      pos++ ;
      ptr = ptr->m_next ;
    }
    return -1 ;
  }
  int TimerQueue::numberOfElements() {
    int len = 0 ;
    queueEntry_t* ptr = m_head ;
    while( ptr ) {
      len++ ;
      ptr = ptr->m_next ;
    }
    return len ;
  }

  // LockingTimerQueue
   TimerEventHandle LockingTimerQueue::add( TimerFunc f, void* functionArgs, uint32_t milliseconds ) {
    return add(f, functionArgs, milliseconds,  su_now());
  }
  TimerEventHandle LockingTimerQueue::add( TimerFunc f, void* functionArgs, uint32_t milliseconds, su_time_t now ) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return TimerQueue::add(f, functionArgs, milliseconds, now);
  }
  void LockingTimerQueue::remove( TimerEventHandle handle) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return TimerQueue::remove(handle);
  }
  bool LockingTimerQueue::isEmpty(void) { 
    std::lock_guard<std::mutex> guard(m_mutex);
    return TimerQueue::isEmpty();
  }
  int LockingTimerQueue::size(void) { 
    std::lock_guard<std::mutex> guard(m_mutex);
    return TimerQueue::size();
  }
  int LockingTimerQueue::positionOf(TimerEventHandle handle) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return TimerQueue::positionOf(handle);
  }
  void LockingTimerQueue::doTimer(su_timer_t* timer) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return TimerQueue::doTimer(timer);
  }    

}
