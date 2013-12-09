/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   value_extraction.hpp
 * \author Andrey Semashev
 * \date   01.03.2008
 *
 * The header contains implementation of convenience tools to extract an attribute value
 * from the view.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_VALUE_EXTRACTION_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_VALUE_EXTRACTION_HPP_INCLUDED_

#include <boost/log/detail/prologue.hpp>
#include <boost/log/core/record.hpp>
#include <boost/log/attributes/attribute_name.hpp>
#include <boost/log/attributes/attribute_value.hpp>
#include <boost/log/attributes/attribute.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>
#include <boost/log/utility/explicit_operator_bool.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

/*!
 * \brief Generic attribute value extractor
 *
 * Attribute value extractor is a functional object that attempts to find and extract the stored
 * attribute value from the attribute values view or a log record. The extracted value is returned
 * from the extractor.
 *
 * The extractor can be specialized on one or several attribute value types that should be
 * specified in the second template argument.
 */
template< typename CharT, typename T >
class value_extractor
{
public:
    //! Function object result type
    typedef typename attribute_value::result_of_extract< T >::type result_type;

    //! Character type
    typedef CharT char_type;
    //! Attribute name type
    typedef basic_attribute_name< char_type > attribute_name_type;
    //! Attribute values view type
    typedef basic_attribute_values_view< char_type > values_view_type;
    //! Log record type
    typedef basic_record< char_type > record_type;
    //! Attribute value types
    typedef T value_types;

private:
    //! The name of the attribute value to extract
    attribute_name_type m_Name;

public:
    /*!
     * Constructor
     *
     * \param name Attribute name to be extracted on invokation
     */
    explicit value_extractor(attribute_name_type const& name) : m_Name(name) {}

    /*!
     * Extraction operator. Looks for an attribute value with the name specified on construction
     * and tries to acquire the stored value of one of the supported types. If extraction succeeds,
     * the extracted value is returned.
     *
     * \param attrs A set of attribute values in which to look for the specified attribute value.
     * \return The extracted value, if extraction succeeded, an empty value otherwise.
     */
    result_type operator() (values_view_type const& attrs) const
    {
        typename values_view_type::const_iterator it = attrs.find(m_Name);
        if (it != attrs.end())
            return it->second.BOOST_NESTED_TEMPLATE extract< value_types >();
        else
            return result_type();
    }

    /*!
     * Extraction operator. Looks for an attribute value with the name specified on construction
     * and tries to acquire the stored value of one of the supported types. If extraction succeeds,
     * the extracted value is returned.
     *
     * \param record A log record. The attribute value will be sought among those associated with the record.
     * \return The extracted value, if extraction succeeded, an empty value otherwise.
     */
    result_type operator() (record_type const& record) const
    {
        return operator() (record.attribute_values());
    }
};

#ifdef BOOST_LOG_DOXYGEN_PASS

/*!
 * The function extracts an attribute value from the view. The user has to explicitly specify the
 * type or set of possible types of the attribute value to be visited.
 *
 * \param name The name of the attribute value to extract.
 * \param attrs A set of attribute values in which to look for the specified attribute value.
 * \return The extracted value, if found. An empty value otherwise.
 */
template< typename T, typename CharT >
typename attribute_value::result_of_extract< T >::type extract(
    basic_attribute_name< CharT > const& name, basic_attribute_values_view< CharT > const& attrs);

/*!
 * The function extracts an attribute value from the view. The user has to explicitly specify the
 * type or set of possible types of the attribute value to be visited.
 *
 * \param name The name of the attribute value to extract.
 * \param record A log record. The attribute value will be sought among those associated with the record.
 * \return The extracted value, if found. An empty value otherwise.
 */
template< typename T, typename CharT, typename VisitorT >
typename attribute_value::result_of_extract< T >::type extract(
    basic_attribute_name< CharT > const& name, basic_record< CharT > const& record);

#else // BOOST_LOG_DOXYGEN_PASS

#ifdef BOOST_LOG_USE_CHAR

template< typename T >
inline typename attribute_value::result_of_extract< T >::type extract(
    basic_attribute_name< char > const& name, basic_attribute_values_view< char > const& attrs)
{
    value_extractor< char, T > extractor(name);
    return extractor(attrs);
}

template< typename T >
inline typename attribute_value::result_of_extract< T >::type extract(
    basic_attribute_name< char > const& name, basic_record< char > const& record)
{
    value_extractor< char, T > extractor(name);
    return extractor(record);
}

#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T

template< typename T >
inline typename attribute_value::result_of_extract< T >::type extract(
    basic_attribute_name< wchar_t > const& name, basic_attribute_values_view< wchar_t > const& attrs)
{
    value_extractor< wchar_t, T > extractor(name);
    return extractor(attrs);
}

template< typename T >
inline typename attribute_value::result_of_extract< T >::type extract(
    basic_attribute_name< wchar_t > const& name, basic_record< wchar_t > const& record)
{
    value_extractor< wchar_t, T > extractor(name);
    return extractor(record);
}

#endif // BOOST_LOG_USE_WCHAR_T

#endif // BOOST_LOG_DOXYGEN_PASS

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTES_VALUE_EXTRACTION_HPP_INCLUDED_
