/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   current_process_id.hpp
 * \author Andrey Semashev
 * \date   12.09.2009
 *
 * The header contains implementation of a current process id attribute
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_CURRENT_PROCESS_ID_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_CURRENT_PROCESS_ID_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/process_id.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_cast.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace attributes {

/*!
 * \brief A class of an attribute that holds the current process identifier
 */
class current_process_id :
    public constant< boost::log::aux::process::id >
{
    typedef constant< boost::log::aux::process::id > base_type;

public:
    /*!
     * Constructor. Inirializes the attribute with the current process identifier.
     */
    current_process_id() : base_type(boost::log::aux::this_process::get_id()) {}
    /*!
     * Constructor for casting support
     */
    explicit current_process_id(cast_source const& source) :
        base_type(source)
    {
    }
};

} // namespace attributes

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTES_CURRENT_PROCESS_ID_HPP_INCLUDED_
