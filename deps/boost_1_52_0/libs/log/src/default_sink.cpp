/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   default_sink.cpp
 * \author Andrey Semashev
 * \date   19.04.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <cstdio>
#include <boost/optional/optional.hpp>
#include <boost/log/detail/prologue.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/thread/locks.hpp>
#include <boost/log/detail/thread_id.hpp>
#endif
#include <boost/log/detail/default_attribute_names.hpp>
#include <boost/date_time/microsec_time_clock.hpp>
#include <boost/date_time/time_resolution_traits.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include "default_sink.hpp"

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sinks {

namespace aux {

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! A special time point type that contains decomposed date and time to avoid excessive calculations
struct decomposed_time_point
{
    struct date_type :
        public gregorian::greg_year_month_day
    {
        date_type(year_type y, month_type m, day_type d) :
            gregorian::greg_year_month_day(y, m, d)
        {
        }
    };

    struct time_duration_type :
        public date_time::micro_res
    {
        typedef date_time::micro_res rep_type;

        hour_type hours;
        min_type minutes;
        sec_type seconds;
        fractional_seconds_type useconds;

        time_duration_type(hour_type h, min_type m, sec_type s, fractional_seconds_type u) :
            hours(h),
            minutes(m),
            seconds(s),
            useconds(u)
        {
        }
    };

    date_type date;
    time_duration_type time;

    decomposed_time_point(date_type const& d, time_duration_type const& t) :
        date(d),
        time(t)
    {
    }
};

inline const char* severity_level_to_string(boost::log::trivial::severity_level lvl)
{
    switch (lvl)
    {
    case boost::log::trivial::trace:
        return "[trace]  ";
    case boost::log::trivial::debug:
        return "[debug]  ";
    case boost::log::trivial::info:
        return "[info]   ";
    case boost::log::trivial::warning:
        return "[warning]";
    case boost::log::trivial::error:
        return "[error]  ";
    case boost::log::trivial::fatal:
        return "[fatal]  ";
    default:
        return "[-]      ";
    }
}

#ifdef BOOST_LOG_USE_CHAR

inline void print_message(boost::log::trivial::severity_level lvl, std::string const& msg)
{
    const decomposed_time_point now = date_time::microsec_clock< decomposed_time_point >::local_time();

    std::printf("[%04u-%02u-%02u %02u:%02u:%02u.%06u] "
#if !defined(BOOST_LOG_NO_THREADS)
                "[0x%08x] "
#endif
                "%s %s\n",
        static_cast< unsigned int >(now.date.year),
        static_cast< unsigned int >(now.date.month),
        static_cast< unsigned int >(now.date.day),
        static_cast< unsigned int >(now.time.hours),
        static_cast< unsigned int >(now.time.minutes),
        static_cast< unsigned int >(now.time.seconds),
        static_cast< unsigned int >(now.time.useconds),
#if !defined(BOOST_LOG_NO_THREADS)
        static_cast< unsigned int >(boost::log::aux::this_thread::get_id().native_id()),
#endif
        severity_level_to_string(lvl),
        msg.c_str());
}

#endif

#ifdef BOOST_LOG_USE_WCHAR_T

inline void print_message(boost::log::trivial::severity_level lvl, std::wstring const& msg)
{
    const decomposed_time_point now = date_time::microsec_clock< decomposed_time_point >::local_time();

    std::printf("[%04u-%02u-%02u %02u:%02u:%02u.%06u] "
#if !defined(BOOST_LOG_NO_THREADS)
                "[0x%08x] "
#endif
                "%s %ls\n",
        static_cast< unsigned int >(now.date.year),
        static_cast< unsigned int >(now.date.month),
        static_cast< unsigned int >(now.date.day),
        static_cast< unsigned int >(now.time.hours),
        static_cast< unsigned int >(now.time.minutes),
        static_cast< unsigned int >(now.time.seconds),
        static_cast< unsigned int >(now.time.useconds),
#if !defined(BOOST_LOG_NO_THREADS)
        static_cast< unsigned int >(boost::log::aux::this_thread::get_id().native_id()),
#endif
        severity_level_to_string(lvl),
        msg.c_str());
}

#endif

} // namespace

template< typename CharT >
basic_default_sink< CharT >::basic_default_sink() :
    m_severity_extractor(boost::log::aux::default_attribute_names< char_type >::severity())
{
}

template< typename CharT >
basic_default_sink< CharT >::~basic_default_sink()
{
}

template< typename CharT >
bool basic_default_sink< CharT >::will_consume(values_view_type const&)
{
    return true;
}

template< typename CharT >
void basic_default_sink< CharT >::consume(record_type const& record)
{
    BOOST_LOG_EXPR_IF_MT(lock_guard< mutex_type > lock(m_mutex);)
    print_message
    (
        m_severity_extractor(record).get_value_or(boost::log::trivial::info),
        record.message()
    );
}

template< typename CharT >
void basic_default_sink< CharT >::flush()
{
    BOOST_LOG_EXPR_IF_MT(lock_guard< mutex_type > lock(m_mutex);)
    fflush(stdout);
}

#ifdef BOOST_LOG_USE_CHAR
template class basic_default_sink< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class basic_default_sink< wchar_t >;
#endif

} // namespace aux

} // namespace sinks

} // namespace log

} // namespace boost

