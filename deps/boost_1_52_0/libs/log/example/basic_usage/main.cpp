/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   main.cpp
 * \author Andrey Semashev
 * \date   11.11.2007
 *
 * \brief  An example of basic library usage. See the library tutorial for expanded
 *         comments on this code. It may also be worthwhile reading the Wiki requirements page:
 *         http://www.crystalclearsoftware.com/cgi-bin/boost_wiki/wiki.pl?Boost.Logging
 */

// #define BOOST_LOG_USE_CHAR
// #define BOOST_ALL_DYN_LINK 1
// #define BOOST_LOG_DYN_LINK 1

#include <iostream>

#include <boost/log/common.hpp>
#include <boost/log/formatters.hpp>
#include <boost/log/filters.hpp>

#include <boost/log/utility/init/to_file.hpp>
#include <boost/log/utility/init/to_console.hpp>
#include <boost/log/utility/init/common_attributes.hpp>

#include <boost/log/attributes/timer.hpp>

#include <boost/log/sources/logger.hpp>

namespace logging = boost::log;
namespace fmt = boost::log::formatters;
namespace flt = boost::log::filters;
namespace sinks = boost::log::sinks;
namespace attrs = boost::log::attributes;
namespace src = boost::log::sources;
namespace keywords = boost::log::keywords;

using boost::shared_ptr;

// Here we define our application severity levels.
enum severity_level
{
    normal,
    notification,
    warning,
    error,
    critical
};

// The formatting logic for the severity level
template< typename CharT, typename TraitsT >
inline std::basic_ostream< CharT, TraitsT >& operator<< (
    std::basic_ostream< CharT, TraitsT >& strm, severity_level lvl)
{
    static const char* const str[] =
    {
        "normal",
        "notification",
        "warning",
        "error",
        "critical"
    };
    if (static_cast< std::size_t >(lvl) < (sizeof(str) / sizeof(*str)))
        strm << str[lvl];
    else
        strm << static_cast< int >(lvl);
    return strm;
}

int main(int argc, char* argv[])
{
    // This is a simple tutorial/example of Boost.Log usage

    // The first thing we have to do to get using the library is
    // to set up the logging sinks - i.e. where the logs will be written to.
    logging::init_log_to_console(std::clog, keywords::format = "%TimeStamp%: %_%");

    // One can also use lambda expressions to setup filters and formatters
    logging::init_log_to_file
    (
        "sample.log",
        keywords::filter = flt::attr< severity_level >("Severity", std::nothrow) >= warning,
        keywords::format = fmt::format("%1% [%2%] <%3%> %4%")
            % fmt::date_time("TimeStamp", std::nothrow)
            % fmt::time_duration("Uptime", std::nothrow)
            % fmt::attr< severity_level >("Severity", std::nothrow)
            % fmt::message()
    );

    // Also let's add some commonly used attributes, like timestamp and record counter.
    logging::add_common_attributes();

    // Now our logs will be written both to the console and to the file.
    // Let's do a quick test and output something. We have to create a logger for this.
    src::logger lg;

    // And output...
    BOOST_LOG(lg) << "Hello, World!";

    // Now, let's try logging with severity
    src::severity_logger< severity_level > slg;

    // Let's pretend we also want to profile our code, so add a special timer attribute.
    slg.add_attribute("Uptime", attrs::timer());

    BOOST_LOG_SEV(slg, normal) << "A normal severity message, will not pass to the file";
    BOOST_LOG_SEV(slg, warning) << "A warning severity message, will pass to the file";
    BOOST_LOG_SEV(slg, error) << "An error severity message, will pass to the file";

    return 0;
}
