/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   thread_id.cpp
 * \author Andrey Semashev
 * \date   08.1.2012
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <boost/log/detail/prologue.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <iostream>
#include <boost/integer.hpp>
#include <boost/io/ios_state.hpp>
#include <boost/log/detail/thread_id.hpp>

#if defined(BOOST_WINDOWS)

#define WIN32_LEAN_AND_MEAN

#include "windows_version.hpp"
#include <windows.h>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

enum { tid_size = sizeof(GetCurrentThreadId()) };

namespace this_thread {

    //! The function returns current process identifier
    BOOST_LOG_EXPORT thread::id get_id()
    {
        return thread::id(GetCurrentThreadId());
    }

} // namespace this_process

} // namespace aux

} // namespace log

} // namespace boost

#else // defined(BOOST_WINDOWS)

#include <pthread.h>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

namespace this_thread {

    //! The function returns current thread identifier
    BOOST_LOG_EXPORT thread::id get_id()
    {
        // According to POSIX, pthread_t may not be an integer type:
        // http://pubs.opengroup.org/onlinepubs/009695399/basedefs/sys/types.h.html
        // For now we use the hackish cast to get some opaque number that hopefully correlates with system thread identification.
        union
        {
            thread::id::native_type as_uint;
            pthread_t as_pthread;
        }
        caster = {};
        caster.as_pthread = pthread_self();
        return thread::id(caster.as_uint);
    }

} // namespace this_thread

enum { tid_size = sizeof(pthread_t) > sizeof(uintmax_t) ? sizeof(uintmax_t) : sizeof(pthread_t) };

} // namespace aux

} // namespace log

} // namespace boost

#endif // defined(BOOST_WINDOWS)


namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

template< typename CharT, typename TraitsT >
std::basic_ostream< CharT, TraitsT >&
operator<< (std::basic_ostream< CharT, TraitsT >& strm, thread::id const& tid)
{
    if (strm.good())
    {
        // NOTE: MSVC 10 STL seem to have a buggy showbase + setwidth implementation, which results in multiple leading zeros _before_ 'x'.
        //       So we print the "0x" prefix ourselves.
        strm << "0x";
        io::ios_flags_saver flags_saver(strm, std::ios_base::hex);
        io::ios_width_saver width_saver(strm, static_cast< std::streamsize >(tid_size * 2));
        io::basic_ios_fill_saver< CharT, TraitsT > fill_saver(strm, static_cast< CharT >('0'));
        strm << static_cast< uint_t< tid_size * 8 >::least >(tid.native_id());
    }

    return strm;
}

#if defined(BOOST_LOG_USE_CHAR)
template BOOST_LOG_EXPORT
std::basic_ostream< char, std::char_traits< char > >&
operator<< (std::basic_ostream< char, std::char_traits< char > >& strm, thread::id const& tid);
#endif // defined(BOOST_LOG_USE_CHAR)

#if defined(BOOST_LOG_USE_WCHAR_T)
template BOOST_LOG_EXPORT
std::basic_ostream< wchar_t, std::char_traits< wchar_t > >&
operator<< (std::basic_ostream< wchar_t, std::char_traits< wchar_t > >& strm, thread::id const& tid);
#endif // defined(BOOST_LOG_USE_WCHAR_T)

} // namespace aux

} // namespace log

} // namespace boost

#endif // !defined(BOOST_LOG_NO_THREADS)
