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
#ifndef __TIMER_QUEUE_MANAGER_H__
#define __TIMER_QUEUE_MANAGER_H__

#include "timer-queue.hpp"

namespace drachtio {

  class TimerQueueManager {
  public:
    virtual TimerEventHandle addTimer( const char* szTimerClass, TimerFunc f, void* functionArgs, uint32_t milliseconds ) = 0 ;
    virtual void removeTimer( TimerEventHandle handle, const char* szTimer ) = 0 ;
    virtual void logQueueSizes(void) {}
  } ;

  class SipTimerQueueManager : public TimerQueueManager {
  public:
    SipTimerQueueManager(su_root_t* root) : 
        m_queue(root, "general-sip"), m_queueA(root,"timerA"), m_queueB(root,"timerB"),
        m_queueC(root, "timerC"), m_queueD(root, "timerD"), m_queueE(root, "timerE"), 
        m_queueF(root, "timerF"), m_queueK(root, "timerK") {}
    ~SipTimerQueueManager() {}

    TimerEventHandle addTimer( const char* szTimerClass, TimerFunc f, void* functionArgs, uint32_t milliseconds ) {
        TimerEventHandle handle ;
        if( 0 == strcmp("timerA", szTimerClass) ) handle = m_queueA.add( f, functionArgs, milliseconds );
        else if( 0 == strcmp("timerB", szTimerClass) ) handle = m_queueB.add( f, functionArgs, milliseconds );
        else if( 0 == strcmp("timerC", szTimerClass) ) handle = m_queueC.add( f, functionArgs, milliseconds );
        else if( 0 == strcmp("timerD", szTimerClass) ) handle = m_queueD.add( f, functionArgs, milliseconds );
        else if( 0 == strcmp("timerE", szTimerClass) ) handle = m_queueE.add( f, functionArgs, milliseconds );
        else if( 0 == strcmp("timerF", szTimerClass) ) handle = m_queueF.add( f, functionArgs, milliseconds );
        else if( 0 == strcmp("timerK", szTimerClass) ) handle = m_queueK.add( f, functionArgs, milliseconds );
        else handle = m_queue.add( f, functionArgs, milliseconds );
        return handle ;
    }
    void removeTimer( TimerEventHandle handle, const char* szTimerClass ) {
        if( 0 == strcmp("timerA", szTimerClass) ) m_queueA.remove( handle ) ;
        else if( 0 == strcmp("timerB", szTimerClass) ) m_queueB.remove( handle ) ;
        else if( 0 == strcmp("timerC", szTimerClass) ) m_queueC.remove( handle ) ;
        else if( 0 == strcmp("timerD", szTimerClass) ) m_queueD.remove( handle ) ;
        else if( 0 == strcmp("timerE", szTimerClass) ) m_queueE.remove( handle ) ;
        else if( 0 == strcmp("timerF", szTimerClass) ) m_queueF.remove( handle ) ;
        else if( 0 == strcmp("timerK", szTimerClass) ) m_queueK.remove( handle ) ;
        else m_queue.remove( handle ) ;
    }
    void logQueueSizes(void) ;

  protected:
    TimerQueue      m_queue ;
    TimerQueue      m_queueA ;
    TimerQueue      m_queueB ;
    TimerQueue      m_queueC ;
    TimerQueue      m_queueD ;    
    TimerQueue      m_queueE ;    
    TimerQueue      m_queueF ;    
    TimerQueue      m_queueK ;    
  } ;

}

#endif