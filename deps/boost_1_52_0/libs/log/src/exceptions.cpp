/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   exceptions.cpp
 * \author Andrey Semashev
 * \date   31.10.2009
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <boost/throw_exception.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/errinfo_at_line.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/support/exception.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

runtime_error::runtime_error(std::string const& descr) :
    std::runtime_error(descr)
{
}

runtime_error::~runtime_error() throw()
{
}

missing_value::missing_value() :
    runtime_error("Requested value not found")
{
}

missing_value::missing_value(std::string const& descr) :
    runtime_error(descr)
{
}

missing_value::~missing_value() throw()
{
}

void missing_value::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(missing_value())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void missing_value::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(missing_value(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

invalid_type::invalid_type() :
    runtime_error("Requested value has invalid type")
{
}

invalid_type::invalid_type(std::string const& descr) :
    runtime_error(descr)
{
}

invalid_type::~invalid_type() throw()
{
}

void invalid_type::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(invalid_type())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void invalid_type::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(invalid_type(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

invalid_value::invalid_value() :
    runtime_error("The value is invalid")
{
}

invalid_value::invalid_value(std::string const& descr) :
    runtime_error(descr)
{
}

invalid_value::~invalid_value() throw()
{
}

void invalid_value::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(invalid_value())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void invalid_value::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(invalid_value(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

parse_error::parse_error() :
    runtime_error("Failed to parse content")
{
}

parse_error::parse_error(std::string const& descr) :
    runtime_error(descr)
{
}

parse_error::~parse_error() throw()
{
}

void parse_error::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(parse_error())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void parse_error::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(parse_error(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void parse_error::throw_(const char* file, std::size_t line, std::string const& descr, std::size_t content_line)
{
    boost::throw_exception(boost::enable_error_info(parse_error(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
        << boost::errinfo_at_line(content_line)
    );
}

conversion_error::conversion_error() :
    runtime_error("Failed to perform conversion")
{
}

conversion_error::conversion_error(std::string const& descr) :
    runtime_error(descr)
{
}

conversion_error::~conversion_error() throw()
{
}

void conversion_error::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(conversion_error())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void conversion_error::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(conversion_error(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

system_error::system_error() :
    runtime_error("Underlying API operation failed")
{
}

system_error::system_error(std::string const& descr) :
    runtime_error(descr)
{
}

system_error::~system_error() throw()
{
}

void system_error::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(system_error())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void system_error::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(system_error(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

logic_error::logic_error(std::string const& descr) :
    std::logic_error(descr)
{
}

logic_error::~logic_error() throw()
{
}

odr_violation::odr_violation() :
    logic_error("ODR violation detected")
{
}

odr_violation::odr_violation(std::string const& descr) :
    logic_error(descr)
{
}

odr_violation::~odr_violation() throw()
{
}

void odr_violation::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(odr_violation())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void odr_violation::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(odr_violation(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

unexpected_call::unexpected_call() :
    logic_error("Invalid call sequence")
{
}

unexpected_call::unexpected_call(std::string const& descr) :
    logic_error(descr)
{
}

unexpected_call::~unexpected_call() throw()
{
}

void unexpected_call::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(unexpected_call())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void unexpected_call::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(unexpected_call(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

setup_error::setup_error() :
    logic_error("The library is not initialized properly")
{
}

setup_error::setup_error(std::string const& descr) :
    logic_error(descr)
{
}

setup_error::~setup_error() throw()
{
}

void setup_error::throw_(const char* file, std::size_t line)
{
    boost::throw_exception(boost::enable_error_info(setup_error())
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

void setup_error::throw_(const char* file, std::size_t line, std::string const& descr)
{
    boost::throw_exception(boost::enable_error_info(setup_error(descr))
        << boost::throw_file(file)
        << boost::throw_line(line)
    );
}

} // namespace log

} // namespace boost
