/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   has_attr.hpp
 * \author Andrey Semashev
 * \date   22.04.2007
 *
 * The header contains implementation of a filter that checks presence of an attribute in a log record.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_FILTERS_HAS_ATTR_HPP_INCLUDED_
#define BOOST_LOG_FILTERS_HAS_ATTR_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/functional.hpp>
#include <boost/log/attributes/attribute_name.hpp>
#include <boost/log/attributes/value_visitation.hpp>
#include <boost/log/filters/basic_filters.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace filters {

/*!
 * \brief A filter that detects if there is an attribute with given name and type in the attribute values view
 *
 * The filter can be instantiated either with one particular attribute value type or with a sequence of types.
 */
template< typename CharT, typename AttributeValueTypesT = void >
class flt_has_attr :
    public basic_filter< CharT, flt_has_attr< CharT, AttributeValueTypesT > >
{
private:
    //! Base type
    typedef basic_filter< CharT, flt_has_attr< CharT, AttributeValueTypesT > > base_type;

public:
    //! Attribute values container type
    typedef typename base_type::values_view_type values_view_type;
    //! Char type
    typedef typename base_type::char_type char_type;
    //! Attribute name type
    typedef typename base_type::attribute_name_type attribute_name_type;

private:
    //! Visitor invoker for the attribute value
    value_visitor_invoker< char_type, AttributeValueTypesT > m_Invoker;

public:
    /*!
     * Constructs the filter
     *
     * \param name Attribute name
     */
    explicit flt_has_attr(attribute_name_type const& name) : m_Invoker(name) {}

    /*!
     * Applies the filter
     *
     * \param values A set of attribute values of a single log record
     * \return true if the log record contains the sought attribute value, false otherwise
     */
    bool operator() (values_view_type const& values) const
    {
        return (m_Invoker(values, boost::log::aux::nop()).code() == visitation_result::ok);
    }
};

/*!
 * \brief A filter that detects if there is an attribute with given name in the complete attribute view
 *
 * The specialization is used when an attribute value of any type is sought.
 */
template< typename CharT >
class flt_has_attr< CharT, void > :
    public basic_filter< CharT, flt_has_attr< CharT, void > >
{
private:
    //! Base type
    typedef basic_filter< CharT, flt_has_attr< CharT, void > > base_type;

public:
    //! Attribute values container type
    typedef typename base_type::values_view_type values_view_type;
    //! Char type
    typedef typename base_type::char_type char_type;
    //! Attribute name type
    typedef typename base_type::attribute_name_type attribute_name_type;

private:
    //! Attribute name
    attribute_name_type m_AttributeName;

public:
    /*!
     * Constructs the filter
     *
     * \param name Attribute name
     */
    explicit flt_has_attr(attribute_name_type const& name) : m_AttributeName(name) {}

    /*!
     * Applies the filter
     *
     * \param values A set of attribute values of a single log record
     * \return true if the log record contains the sought attribute value, false otherwise
     */
    bool operator() (values_view_type const& values) const
    {
        return (values.find(m_AttributeName) != values.end());
    }
};

#ifdef BOOST_LOG_USE_CHAR

/*!
 * Filter generator
 */
inline flt_has_attr< char > has_attr(basic_attribute_name< char > const& name)
{
    return flt_has_attr< char >(name);
}

/*!
 * Filter generator
 */
template< typename AttributeValueTypesT >
inline flt_has_attr< char, AttributeValueTypesT > has_attr(basic_attribute_name< char > const& name)
{
    return flt_has_attr< char, AttributeValueTypesT >(name);
}

#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T

/*!
 * Filter generator
 */
inline flt_has_attr< wchar_t > has_attr(basic_attribute_name< wchar_t > const& name)
{
    return flt_has_attr< wchar_t >(name);
}

/*!
 * Filter generator
 */
template< typename AttributeValueTypesT >
inline flt_has_attr< wchar_t, AttributeValueTypesT > has_attr(basic_attribute_name< wchar_t > const& name)
{
    return flt_has_attr< wchar_t, AttributeValueTypesT >(name);
}

#endif // BOOST_LOG_USE_WCHAR_T

} // namespace filters

} // namespace log

} // namespace boost

#endif // BOOST_LOG_FILTERS_HAS_ATTR_HPP_INCLUDED_
