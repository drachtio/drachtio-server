/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute.hpp
 * \author Andrey Semashev
 * \date   15.04.2007
 *
 * The header contains attribute definition.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_ATTRIBUTE_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_ATTRIBUTE_HPP_INCLUDED_

#include <boost/intrusive_ptr.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/attributes/attribute_def.hpp>
#include <boost/log/attributes/attribute_value_def.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

inline attribute_value attribute::get_value() const
{
    return m_pImpl->get_value();
}

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTES_ATTRIBUTE_HPP_INCLUDED_
