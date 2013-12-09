/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   fake_mutex.hpp
 * \author Andrey Semashev
 * \date   31.07.2011
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DETAIL_FAKE_MUTEX_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_FAKE_MUTEX_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

//! Fake mutex that doesn't do anything
class fake_mutex
{
public:
    void lock() {}
    bool try_lock() { return true; }
    template< typename T >
    bool timed_lock(T const&) { return true; }
    void unlock() {}
};

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DETAIL_FAKE_MUTEX_HPP_INCLUDED_
