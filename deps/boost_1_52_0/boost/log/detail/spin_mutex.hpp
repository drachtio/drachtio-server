/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   spin_mutex.hpp
 * \author Andrey Semashev
 * \date   01.08.2010
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DETAIL_SPIN_MUTEX_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_SPIN_MUTEX_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>

#ifndef BOOST_LOG_NO_THREADS

#if defined(BOOST_THREAD_POSIX) // This one can be defined by users, so it should go first
#define BOOST_LOG_SPIN_MUTEX_USE_PTHREAD
#elif defined(BOOST_WINDOWS)
#define BOOST_LOG_SPIN_MUTEX_USE_WINAPI
#elif defined(BOOST_HAS_PTHREADS)
#define BOOST_LOG_SPIN_MUTEX_USE_PTHREAD
#endif

#if defined(BOOST_LOG_SPIN_MUTEX_USE_WINAPI)

#include <boost/detail/interlocked.hpp>

#if defined(BOOST_USE_WINDOWS_H)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif

#include <windows.h>

#else // defined(BOOST_USE_WINDOWS_H)

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

extern "C" {

__declspec(dllimport) int __stdcall SwitchToThread();

} // extern "C"

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_USE_WINDOWS_H

#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
#    if defined(_M_IX86)
#        define BOOST_LOG_PAUSE_OP __asm { pause }
#    elif defined(_M_AMD64)
extern "C" void _mm_pause(void);
#pragma intrinsic(_mm_pause)
#        define BOOST_LOG_PAUSE_OP _mm_pause()
#    endif
#    if defined(__INTEL_COMPILER)
#        define BOOST_LOG_WRITE_MEMORY_BARRIER __asm { nop }
#    elif _MSC_VER >= 1400
extern "C" void _WriteBarrier(void);
#pragma intrinsic(_WriteBarrier)
#        define BOOST_LOG_WRITE_MEMORY_BARRIER _WriteBarrier()
#    elif _MSC_VER >= 1310
extern "C" void _ReadWriteBarrier(void);
#pragma intrinsic(_ReadWriteBarrier)
#        define BOOST_LOG_WRITE_MEMORY_BARRIER _ReadWriteBarrier()
#    endif
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#    define BOOST_LOG_PAUSE_OP __asm__ __volatile__("pause;")
#    define BOOST_LOG_WRITE_MEMORY_BARRIER __asm__ __volatile__("" : : : "memory")
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

//! A simple spinning mutex
class spin_mutex
{
private:
    enum state
    {
        initial_pause = 2,
        max_pause = 16
    };

    long m_State;

public:
    spin_mutex() : m_State(0) {}

    bool try_lock()
    {
        return (BOOST_INTERLOCKED_COMPARE_EXCHANGE(&m_State, 1L, 0L) == 0L);
    }

    void lock()
    {
#if defined(BOOST_LOG_PAUSE_OP)
        register unsigned int pause_count = initial_pause;
#endif
        while (!try_lock())
        {
#if defined(BOOST_LOG_PAUSE_OP)
            if (pause_count < max_pause)
            {
                for (register unsigned int i = 0; i < pause_count; ++i)
                {
                    BOOST_LOG_PAUSE_OP;
                }
                pause_count += pause_count;
            }
            else
            {
                // Restart spinning after waking up this thread
                pause_count = initial_pause;
                SwitchToThread();
            }
#else
            SwitchToThread();
#endif
        }
    }

    void unlock()
    {
#if defined(BOOST_LOG_WRITE_MEMORY_BARRIER)
        m_State = 0L;
        BOOST_LOG_WRITE_MEMORY_BARRIER;
#else
        BOOST_INTERLOCKED_EXCHANGE(&m_State, 0L);
#endif
    }

private:
    //  Non-copyable
    spin_mutex(spin_mutex const&);
    spin_mutex& operator= (spin_mutex const&);
};

#undef BOOST_LOG_PAUSE_OP
#undef BOOST_LOG_WRITE_MEMORY_BARRIER

} // namespace aux

} // namespace log

} // namespace boost

#elif defined(BOOST_LOG_SPIN_MUTEX_USE_PTHREAD)

#include <pthread.h>
#include <boost/assert.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

#if defined(_POSIX_SPIN_LOCKS) && _POSIX_SPIN_LOCKS > 0

//! A simple spinning mutex
class spin_mutex
{
private:
    pthread_spinlock_t m_State;

public:
    spin_mutex()
    {
        BOOST_VERIFY(pthread_spin_init(&m_State, PTHREAD_PROCESS_PRIVATE) == 0);
    }
    ~spin_mutex()
    {
        pthread_spin_destroy(&m_State);
    }
    bool try_lock()
    {
        return (pthread_spin_trylock(&m_State) == 0);
    }
    void lock()
    {
        BOOST_VERIFY(pthread_spin_lock(&m_State) == 0);
    }
    void unlock()
    {
        pthread_spin_unlock(&m_State);
    }

private:
    //  Non-copyable
    spin_mutex(spin_mutex const&);
    spin_mutex& operator= (spin_mutex const&);
};

#else // defined(_POSIX_SPIN_LOCKS)

//! Backup implementation in case if pthreads don't support spin locks
class spin_mutex
{
private:
    pthread_mutex_t m_State;

public:
    spin_mutex()
    {
        BOOST_VERIFY(pthread_mutex_init(&m_State, NULL) == 0);
    }
    ~spin_mutex()
    {
        pthread_mutex_destroy(&m_State);
    }
    bool try_lock()
    {
        return (pthread_mutex_trylock(&m_State) == 0);
    }
    void lock()
    {
        BOOST_VERIFY(pthread_mutex_lock(&m_State) == 0);
    }
    void unlock()
    {
        pthread_mutex_unlock(&m_State);
    }

private:
    //  Non-copyable
    spin_mutex(spin_mutex const&);
    spin_mutex& operator= (spin_mutex const&);
};

#endif // defined(_POSIX_SPIN_LOCKS)

} // namespace aux

} // namespace log

} // namespace boost

#endif

#endif // BOOST_LOG_NO_THREADS

#endif // BOOST_LOG_DETAIL_SPIN_MUTEX_HPP_INCLUDED_
