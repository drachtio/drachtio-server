/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   default_sink.hpp
 * \author Andrey Semashev
 * \date   08.01.2012
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DEFAULT_SINK_HPP_INCLUDED_
#define BOOST_LOG_DEFAULT_SINK_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>
#include <boost/log/sinks/sink.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/trivial.hpp>
#if !defined(BOOST_LOG_NO_THREADS)
#include <boost/thread/mutex.hpp>
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sinks {

namespace aux {

//! The default sink to be used when no sinks are registered in the logging core
template< typename CharT >
class basic_default_sink :
    public sink< CharT >
{
    typedef sink< CharT > base_type;

public:
    typedef typename base_type::char_type char_type;
    typedef typename base_type::string_type string_type;
    typedef typename base_type::values_view_type values_view_type;
    typedef typename base_type::record_type record_type;

private:
#if !defined(BOOST_LOG_NO_THREADS)
    typedef mutex mutex_type;
    mutex_type m_mutex;
#endif
    value_extractor< char_type, boost::log::trivial::severity_level > const m_severity_extractor;

public:
    basic_default_sink();
    ~basic_default_sink();
    bool will_consume(values_view_type const&);
    void consume(record_type const& record);
    void flush();
};

} // namespace aux

} // namespace sinks

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DEFAULT_SINK_HPP_INCLUDED_
