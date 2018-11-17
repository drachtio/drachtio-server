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
#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <functional>
#include <thread>

#include <sofia-sip/su_wait.h>

namespace drachtio {

  class TimerQueue ;

  typedef std::function<void (void*)> TimerFunc ;

  struct queueEntry_t {
    queueEntry_t(TimerQueue* queue, TimerFunc f, void* functionArgs, su_time_t when) ;

    TimerQueue*       m_queue ;
    queueEntry_t*     m_next ;
    queueEntry_t*     m_prev ;
    TimerFunc         m_function ;
    void*             m_functionArgs ;
    su_time_t         m_when ;
  } ;

  typedef queueEntry_t * TimerEventHandle ;
 
  class TimerQueue  {
  public:
    

    TimerQueue(su_root_t* root, const char*szName = NULL) ;
    TimerQueue( const TimerQueue& ) = delete;
    ~TimerQueue() ;

    TimerEventHandle add( TimerFunc f, void* functionArgs, uint32_t milliseconds ) ;
    TimerEventHandle add( TimerFunc f, void* functionArgs, uint32_t milliseconds, su_time_t now ) ;
    void remove( TimerEventHandle handle) ;

    bool isEmpty(void) { return 0 == m_length; }
    int size(void) { return m_length; }
    int positionOf(TimerEventHandle handle) ;

    void doTimer(su_timer_t* timer) ;      

  protected:
    int          numberOfElements(void) ;

    su_root_t*    m_root ;
    std::string   m_name ;
    su_timer_t*   m_timer ;
    queueEntry_t* m_head ;
    queueEntry_t* m_tail ;
    int           m_length ;
    unsigned      m_in_timer:1; /**< Set when executing timers */

   } ;    
}

#endif
