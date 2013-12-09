/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   timer.hpp
 * \author Andrey Semashev
 * \date   02.12.2007
 *
 * The header contains implementation of a stop watch attribute.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_TIMER_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_TIMER_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>
#include <boost/log/attributes/attribute.hpp>
#include <boost/log/attributes/attribute_cast.hpp>
#include <boost/log/attributes/time_traits.hpp>

#ifdef _MSC_VER
#pragma warning(push)
// non dll-interface class 'A' used as base for dll-interface class 'B'
#pragma warning(disable: 4275)
// class 'A' needs to have dll-interface to be used by clients of class 'B'
#pragma warning(disable: 4251)
#endif // _MSC_VER

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace attributes {

/*!
 * \brief A class of an attribute that makes an attribute value of the time interval since construction
 *
 * The timer attribute calculates the time passed since its construction and returns it on value acquision.
 * The attribute value type is <tt>boost::posix_time::time_duration</tt>.
 *
 * On Windows platform there are two implementations of the attribute. The default one is more precise but
 * a bit slower. This version uses <tt>QueryPerformanceFrequence</tt>/<tt>QueryPerformanceCounter</tt> API
 * to calculate elapsed time.
 *
 * There are known problems with these functions when used with some CPUs, notably AMD Athlon with
 * Cool'n'Quiet technology enabled. See the following links for for more information and possible resolutions:
 *
 * http://support.microsoft.com/?scid=kb;en-us;895980
 * http://support.microsoft.com/?id=896256
 *
 * In case if none of these solutions apply, you are free to define <tt>BOOST_LOG_NO_QUERY_PERFORMANCE_COUNTER</tt> macro to
 * fall back to another implementation based on Boost.DateTime.
 */
class BOOST_LOG_EXPORT timer :
    public attribute
{
public:
    //! Attribute value type
    typedef utc_time_traits::time_type::time_duration_type value_type;

private:
    //! Factory implementation
    class BOOST_LOG_VISIBLE impl;

public:
    /*!
     * Constructor. Starts time counting.
     */
    timer();
    /*!
     * Constructor for casting support
     */
    explicit timer(cast_source const& source);
};

} // namespace attributes

} // namespace log

} // namespace boost

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // BOOST_LOG_ATTRIBUTES_TIMER_HPP_INCLUDED_
