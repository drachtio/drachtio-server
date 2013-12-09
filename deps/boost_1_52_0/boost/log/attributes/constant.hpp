/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   constant.hpp
 * \author Andrey Semashev
 * \date   15.04.2007
 *
 * The header contains implementation of a constant attribute.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_CONSTANT_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_CONSTANT_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>
#include <boost/log/attributes/attribute.hpp>
#include <boost/log/attributes/attribute_cast.hpp>
#include <boost/log/attributes/basic_attribute_value.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace attributes {

/*!
 * \brief A class of an attribute that holds a single constant value
 *
 * The constant is a simpliest and one of the most frequently used types of attributes.
 * It stores a constant value, which it eventually returns as its value each time
 * requested.
 */
template< typename T >
class constant :
    public attribute
{
public:
    //! Attribute value type
    typedef T value_type;

protected:
    //! Factory implementation
    class BOOST_LOG_VISIBLE impl :
        public basic_attribute_value< value_type >
    {
        //! Base type
        typedef basic_attribute_value< value_type > base_type;

    public:
        /*!
         * Constructor with the stored value initialization
         */
        explicit impl(value_type const& value) : base_type(value) {}
    };

public:
    /*!
     * Constructor with the stored value initialization
     */
    explicit constant(value_type const& value) : attribute(new impl(value)) {}
    /*!
     * Constructor for casting support
     */
    explicit constant(cast_source const& source) : attribute(source.as< impl >())
    {
    }

    /*!
     * \return Reference to the contained value.
     */
    value_type const& get() const
    {
        return static_cast< impl* >(this->get_impl())->get();
    }
};

} // namespace attributes

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTES_CONSTANT_HPP_INCLUDED_
