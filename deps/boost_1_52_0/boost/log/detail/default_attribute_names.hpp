/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   default_attribute_names.hpp
 * \author Andrey Semashev
 * \date   15.01.2012
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DETAIL_DEFAULT_ATTRIBUTE_NAMES_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_DEFAULT_ATTRIBUTE_NAMES_HPP_INCLUDED_

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

//! A helper traits to get default attribute names
template< typename >
struct default_attribute_names;

#ifdef BOOST_LOG_USE_CHAR
template< >
struct default_attribute_names< char >
{
    static const char* severity() { return "Severity"; }
    static const char* channel() { return "Channel"; }
};
#endif

#ifdef BOOST_LOG_USE_WCHAR_T
template< >
struct default_attribute_names< wchar_t >
{
    static const wchar_t* severity() { return L"Severity"; }
    static const wchar_t* channel() { return L"Channel"; }
};
#endif

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DETAIL_DEFAULT_ATTRIBUTE_NAMES_HPP_INCLUDED_
