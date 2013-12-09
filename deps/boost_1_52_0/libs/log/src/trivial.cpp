/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   trivial.cpp
 * \author Andrey Semashev
 * \date   07.11.2009
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <boost/log/detail/prologue.hpp>

#if defined(BOOST_LOG_USE_CHAR)

#include <boost/log/trivial.hpp>
#include <boost/log/sources/global_logger_storage.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace trivial {

//! Initialization routine
BOOST_LOG_EXPORT logger::logger_type logger::construct_logger()
{
    return logger_type(keywords::severity = info);
}

//! Returns a reference to the trivial logger instance
BOOST_LOG_EXPORT logger::logger_type& logger::get()
{
    return log::sources::aux::logger_singleton< logger >::get();
}

BOOST_LOG_EXPORT const char* to_string(severity_level lvl)
{
    switch (lvl)
    {
    case trace:
        return "trace";
    case debug:
        return "debug";
    case info:
        return "info";
    case warning:
        return "warning";
    case error:
        return "error";
    case fatal:
        return "fatal";
    default:
        return NULL;
    }
}

} // namespace trivial

} // namespace log

} // namespace boost

#endif // defined(BOOST_LOG_USE_CHAR)
