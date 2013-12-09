/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   event_log_constants.hpp
 * \author Andrey Semashev
 * \date   07.11.2008
 *
 * The header contains definition of constants related to Windows NT Event Log API.
 * The constants can be used in other places without the event log backend.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SINKS_EVENT_LOG_CONSTANTS_HPP_INCLUDED_
#define BOOST_LOG_SINKS_EVENT_LOG_CONSTANTS_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/tagged_integer.hpp>

#ifdef _MSC_VER
#pragma warning(push)
// 'm_A' : class 'A' needs to have dll-interface to be used by clients of class 'B'
#pragma warning(disable: 4251)
// non dll-interface class 'A' used as base for dll-interface class 'B'
#pragma warning(disable: 4275)
#endif // _MSC_VER

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sinks {

namespace event_log {

    struct event_id_tag;
    //! A tagged integral type that represents event identifier for the Windows API
    typedef boost::log::aux::tagged_integer< unsigned int, event_id_tag > event_id;
    /*!
     * The function constructs event identifier from an integer
     */
    inline event_id make_event_id(unsigned int id)
    {
        event_id iden = { id };
        return iden;
    }

    struct event_category_tag;
    //! A tagged integral type that represents event category for the Windows API
    typedef boost::log::aux::tagged_integer< unsigned short, event_category_tag > event_category;
    /*!
     * The function constructs event category from an integer
     */
    inline event_category make_event_category(unsigned short cat)
    {
        event_category category = { cat };
        return category;
    }

    //! Windows event types
    enum event_type
    {
        success = 0,                 //!< Equivalent to EVENTLOG_SUCCESS
        info = 4,                    //!< Equivalent to EVENTLOG_INFORMATION_TYPE
        warning = 2,                 //!< Equivalent to EVENTLOG_WARNING_TYPE
        error = 1                    //!< Equivalent to EVENTLOG_ERROR_TYPE
    };

    /*!
     * The function constructs log record level from an integer
     */
    BOOST_LOG_EXPORT event_type make_event_type(unsigned short lev);

} // namespace event_log

} // namespace sinks

} // namespace log

} // namespace boost

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#endif // BOOST_LOG_SINKS_EVENT_LOG_CONSTANTS_HPP_INCLUDED_
