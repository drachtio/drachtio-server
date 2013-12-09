/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   detail/event.hpp
 * \author Andrey Semashev
 * \date   24.07.2011
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DETAIL_EVENT_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_EVENT_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>

#if defined(BOOST_THREAD_PLATFORM_PTHREAD)
#   if defined(_POSIX_SEMAPHORES) && (_POSIX_SEMAPHORES + 0) > 0
#       if defined(__GNUC__) && defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4)
#           include <semaphore.h>
#           include <boost/cstdint.hpp>
#           define BOOST_LOG_EVENT_USE_POSIX_SEMAPHORE
#       endif
#   endif
#elif defined(BOOST_THREAD_PLATFORM_WIN32)
#   include <boost/cstdint.hpp>
#   define BOOST_LOG_EVENT_USE_WINAPI
#endif

#if !defined(BOOST_LOG_EVENT_USE_POSIX_SEMAPHORE) && !defined(BOOST_LOG_EVENT_USE_WINAPI)
#   include <boost/thread/mutex.hpp>
#   include <boost/thread/condition_variable.hpp>
#   define BOOST_LOG_EVENT_USE_BOOST_CONDITION
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

#if defined(BOOST_LOG_EVENT_USE_POSIX_SEMAPHORE)

class sem_based_event
{
private:
    boost::uint32_t m_state;
    sem_t m_semaphore;

public:
    //! Default constructor
    BOOST_LOG_EXPORT sem_based_event();
    //! Destructor
    BOOST_LOG_EXPORT ~sem_based_event();

    //! Waits for the object to become signalled
    BOOST_LOG_EXPORT void wait();
    //! Sets the object to a signalled state
    BOOST_LOG_EXPORT void set_signalled();

private:
    //  Copying prohibited
    sem_based_event(sem_based_event const&);
    sem_based_event& operator= (sem_based_event const&);
};

typedef sem_based_event event;

#elif defined(BOOST_LOG_EVENT_USE_WINAPI)

class winapi_based_event
{
private:
    boost::uint32_t m_state;
    void* m_event;

public:
    //! Default constructor
    BOOST_LOG_EXPORT winapi_based_event();
    //! Destructor
    BOOST_LOG_EXPORT ~winapi_based_event();

    //! Waits for the object to become signalled
    BOOST_LOG_EXPORT void wait();
    //! Sets the object to a signalled state
    BOOST_LOG_EXPORT void set_signalled();

private:
    //  Copying prohibited
    winapi_based_event(winapi_based_event const&);
    winapi_based_event& operator= (winapi_based_event const&);
};

typedef winapi_based_event event;

#else

class generic_event
{
private:
    boost::mutex m_mutex;
    boost::condition_variable m_cond;
    bool m_state;

public:
    //! Default constructor
    BOOST_LOG_EXPORT generic_event();
    //! Destructor
    BOOST_LOG_EXPORT ~generic_event();

    //! Waits for the object to become signalled
    BOOST_LOG_EXPORT void wait();
    //! Sets the object to a signalled state
    BOOST_LOG_EXPORT void set_signalled();

private:
    //  Copying prohibited
    generic_event(generic_event const&);
    generic_event& operator= (generic_event const&);
};

typedef generic_event event;

#endif

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DETAIL_EVENT_HPP_INCLUDED_
