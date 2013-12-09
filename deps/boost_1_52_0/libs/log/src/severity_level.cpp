/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   severity_level.cpp
 * \author Andrey Semashev
 * \date   10.05.2008
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <boost/log/detail/prologue.hpp>
#include <boost/log/sources/severity_feature.hpp>

#if !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_LOG_USE_COMPILER_TLS)
#include <boost/log/detail/singleton.hpp>
#include <boost/log/detail/thread_specific.hpp>
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sources {

namespace aux {

#if defined(BOOST_LOG_NO_THREADS)

static int g_Severity = 0;

#elif defined(BOOST_LOG_USE_COMPILER_TLS)

static BOOST_LOG_TLS int g_Severity = 0;

#else

//! Severity level storage class
class severity_level_holder :
    public boost::log::aux::lazy_singleton< severity_level_holder, boost::log::aux::thread_specific< int > >
{
};

#endif


#if !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_LOG_USE_COMPILER_TLS)

//! The method returns the severity level for the current thread
BOOST_LOG_EXPORT int get_severity_level()
{
    return severity_level_holder::get().get();
}

//! The method sets the severity level for the current thread
BOOST_LOG_EXPORT void set_severity_level(int level)
{
    severity_level_holder::get().set(level);
}

#else // !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_LOG_USE_COMPILER_TLS)

//! The method returns the severity level for the current thread
BOOST_LOG_EXPORT int get_severity_level()
{
    return g_Severity;
}

//! The method sets the severity level for the current thread
BOOST_LOG_EXPORT void set_severity_level(int level)
{
    g_Severity = level;
}

#endif // !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_LOG_USE_COMPILER_TLS)

} // namespace aux

} // namespace sources

} // namespace log

} // namespace boost
