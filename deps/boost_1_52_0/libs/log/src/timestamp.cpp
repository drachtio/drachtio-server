/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   timestamp.cpp
 * \author Andrey Semashev
 * \date   31.07.2011
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <boost/log/detail/timestamp.hpp>

#if defined(BOOST_WINDOWS) && !defined(__CYGWIN__)
#include "windows_version.hpp"
#include <windows.h>
#else
#include <unistd.h> // for config macros
#if defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#include <mach/mach_time.h>
#include <mach/kern_return.h>
#include <boost/log/utility/once_block.hpp>
#endif
#include <time.h>
#include <errno.h>
#include <boost/throw_exception.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

#if defined(BOOST_WINDOWS) && !defined(__CYGWIN__)

#if _WIN32_WINNT >= 0x0600

// Directly use API from Vista and later
BOOST_LOG_EXPORT get_tick_count_t get_tick_count = &GetTickCount64;

#else // _WIN32_WINNT >= 0x0600

BOOST_LOG_ANONYMOUS_NAMESPACE {

#ifdef _MSC_VER
__declspec(align(16))
#elif defined(__GNUC__)
__attribute__((aligned(16)))
#endif
uint64_t g_ticks = 0;

union ticks_caster
{
    uint64_t as_uint64;
    struct
    {
        uint32_t ticks;
        uint32_t counter;
    }
    as_components;
};

#if defined(_MSC_VER) && !defined(_M_CEE_PURE)

#   if defined(_M_IX86)
//! Atomically loads and stores the 64-bit value
BOOST_LOG_FORCEINLINE void move64(const uint64_t* from, uint64_t* to)
{
    __asm
    {
        mov eax, from
        mov edx, to
        fild qword ptr [eax]
        fistp qword ptr [edx]
    };
}
#   else // == if defined(_M_AMD64)
//! Atomically loads and stores the 64-bit value
BOOST_LOG_FORCEINLINE void move64(const uint64_t* from, uint64_t* to)
{
    *to = *from;
}
#   endif

#elif defined(__GNUC__)

#   if defined(__i386__)
//! Atomically loads and stores the 64-bit value
BOOST_LOG_FORCEINLINE void move64(const uint64_t* from, uint64_t* to)
{
    __asm__ __volatile__
    (
        "fildq %1\n\t"
        "fistpq %0"
            : "=m" (*to)
            : "m" (*from)
            : "memory"
    );
}
#   else // == if defined(__x86_64__)
//! Atomically loads and stores the 64-bit value
BOOST_LOG_FORCEINLINE void move64(const uint64_t* from, uint64_t* to)
{
    *to = *from;
}
#   endif

#else

#error Boost.Log: Atomic operations are not defined for your compiler, sorry

#endif

//! Artifical implementation of GetTickCount64
uint64_t __stdcall get_tick_count64()
{
    ticks_caster state;
    move64(&g_ticks, &state.as_uint64);

    uint32_t new_ticks = GetTickCount();

    state.as_components.counter += new_ticks < state.as_components.ticks;
    state.as_components.ticks = new_ticks;

    move64(&state.as_uint64, &g_ticks);
    return state.as_uint64;
}

uint64_t __stdcall get_tick_count_init()
{
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32)
    {
        get_tick_count_t p = (get_tick_count_t)GetProcAddress(hKernel32, "GetTickCount64");
        if (p)
        {
            // Use native API
            get_tick_count = p;
            return p();
        }
    }

    // No native API available
    get_tick_count = &get_tick_count64;
    return get_tick_count64();
}

} // namespace

BOOST_LOG_EXPORT get_tick_count_t get_tick_count = &get_tick_count_init;

#endif // _WIN32_WINNT >= 0x0600

#elif defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0

BOOST_LOG_EXPORT int64_t duration::milliseconds() const
{
    // Timestamps are always in nanoseconds
    return m_ticks / 1000000LL;
}

BOOST_LOG_ANONYMOUS_NAMESPACE {

/*!
 * \c get_timestamp implementation based on POSIX realtime clock.
 * Note that this implementation is only used as a last resort since
 * this timer can be manually set and may jump due to DST change.
 */
timestamp get_timestamp_realtime_clock()
{
    timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
        BOOST_THROW_EXCEPTION(boost::system::system_error(
            errno, boost::system::system_category(), "Failed to acquire current time"));
    }

    return timestamp(static_cast< uint64_t >(ts.tv_sec) * 1000000000ULL + ts.tv_nsec);
}

#   if defined(_POSIX_MONOTONIC_CLOCK)

//! \c get_timestamp implementation based on POSIX monotonic clock
timestamp get_timestamp_monotonic_clock()
{
    timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        int err = errno;
        if (err == EINVAL)
        {
            // The current platform does not support monotonic timer.
            // Fall back to realtime clock, which is not exactly what we need
            // but is better than nothing.
            get_timestamp = &get_timestamp_realtime_clock;
            return get_timestamp_realtime_clock();
        }
        BOOST_THROW_EXCEPTION(boost::system::system_error(
            err, boost::system::system_category(), "Failed to acquire current time"));
    }

    return timestamp(static_cast< uint64_t >(ts.tv_sec) * 1000000000ULL + ts.tv_nsec);
}

#       define BOOST_LOG_DEFAULT_GET_TIMESTAMP get_timestamp_monotonic_clock

#   else // if defined(_POSIX_MONOTONIC_CLOCK)
#       define BOOST_LOG_DEFAULT_GET_TIMESTAMP get_timestamp_realtime_clock
#   endif // if defined(_POSIX_MONOTONIC_CLOCK)

} // namespace

// Use POSIX API
BOOST_LOG_EXPORT get_timestamp_t get_timestamp = &BOOST_LOG_DEFAULT_GET_TIMESTAMP;

#   undef BOOST_LOG_DEFAULT_GET_TIMESTAMP

#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)

BOOST_LOG_EXPORT int64_t duration::milliseconds() const
{
    static mach_timebase_info_data_t timebase_info = {};
    BOOST_LOG_ONCE_BLOCK()
    {
        kern_return_t err = mach_timebase_info(&timebase_info);
        if (err != KERN_SUCCESS)
        {
            BOOST_THROW_EXCEPTION(boost::system::system_error(
                err, boost::system::system_category(), "Failed to initialize timebase info"));
        }
    }

    // Often the timebase rational equals 1, we can optimize for this case
    if (timebase_info.numer == timebase_info.denom)
    {
        // Timestamps are in nanoseconds
        return m_ticks / 1000000LL;
    }
    else
    {
        return (m_ticks * timebase_info.numer) / (1000000LL * timebase_info.denom);
    }
}

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! \c get_timestamp implementation based on MacOS X absolute time
timestamp get_timestamp_mach()
{
    return timestamp(mach_absolute_time());
}

} // namespace

// Use MacOS X API
BOOST_LOG_EXPORT get_timestamp_t get_timestamp = &get_timestamp_mach;

#else

#   error Boost.Log: Timestamp generation is not supported for your platform

#endif

} // namespace aux

} // namespace log

} // namespace boost
