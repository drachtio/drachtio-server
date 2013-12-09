/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   value_visitation.hpp
 * \author Andrey Semashev
 * \date   01.03.2008
 *
 * The header contains implementation of convenience tools to apply visitors to an attribute value
 * in the view.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_VALUE_VISITATION_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_VALUE_VISITATION_HPP_INCLUDED_

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
 * \brief The class represents attribute value visitation result
 *
 * The main purpose of this class is to provide a convenient interface for checking
 * whether the attribute value visitation succeeded or not. It also allows to discover
 * the actual cause of failure, should the operation fail.
 */
class visitation_result
{
public:
    //! Error codes for attribute value visitation
    enum error_code
    {
        ok,                     //!< The attribute value has been visited successfully
        value_not_found,        //!< The attribute value is not present in the view
        value_has_invalid_type  //!< The attribute value is present in the vew, but has an unexpected type
    };

private:
    error_code m_Code;

public:
    /*!
     * Initializing constructor. Creates the result that is equivalent to the
     * specified error code.
     */
    visitation_result(error_code code = ok) : m_Code(code) {}

    /*!
     * Checks if the visitation was successful.
     *
     * \return \c true if the value was visited successfully, \c false otherwise.
     */
    BOOST_LOG_EXPLICIT_OPERATOR_BOOL()
    /*!
     * Checks if the visitation was unsuccessful.
     *
     * \return \c false if the value was visited successfully, \c true otherwise.
     */
    bool operator! () const { return (m_Code != ok); }

    /*!
     * \return The actual result code of value visitation
     */
    error_code code() const { return m_Code; }
};

/*!
 * \brief Generic attribute value visitor invoker
 *
 * Attribute value invoker is a functional object that attempts to find and extract the stored
 * attribute value from the attribute value view or a log record. The extracted value is passed to
 * an unary function object (the visitor) provided by user.
 *
 * The invoker can be specialized on one or several attribute value types that should be
 * specified in the second template argument.
 */
template< typename CharT, typename T >
class value_visitor_invoker
{
public:
    //! Function object result type
    typedef visitation_result result_type;

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
    //! The name of the attribute value to visit
    attribute_name_type m_Name;

public:
    /*!
     * Constructor
     *
     * \param name Attribute name to be visited on invokation
     */
    explicit value_visitor_invoker(attribute_name_type const& name) : m_Name(name) {}

    /*!
     * Visitation operator. Looks for an attribute value with the name specified on construction
     * and tries to acquire the stored value of one of the supported types. If acquisition succeeds,
     * the value is passed to \a visitor.
     *
     * \param attrs A set of attribute values in which to look for the specified attribute value.
     * \param visitor A receiving function object to pass the attribute value to.
     * \return The result of visitation (see codes in the \c visitation_result class).
     */
    template< typename VisitorT >
    result_type operator() (values_view_type const& attrs, VisitorT visitor) const
    {
        typename values_view_type::const_iterator it = attrs.find(m_Name);
        if (it != attrs.end())
        {
            if (it->second.BOOST_NESTED_TEMPLATE visit< value_types >(visitor))
                return visitation_result::ok;
            else
                return visitation_result::value_has_invalid_type;
        }
        return visitation_result::value_not_found;
    }

    /*!
     * Visitation operator. Looks for an attribute value with the name specified on construction
     * and tries to acquire the stored value of one of the supported types. If acquisition succeeds,
     * the value is passed to \a visitor.
     *
     * \param record A log record. The attribute value will be sought among those associated with the record.
     * \param visitor A receiving function object to pass the attribute value to.
     * \return The result of visitation (see codes in the \c visitation_result class).
     */
    template< typename VisitorT >
    result_type operator() (record_type const& record, VisitorT visitor) const
    {
        return operator() (record.attribute_values(), visitor);
    }
};

#ifdef BOOST_LOG_DOXYGEN_PASS

/*!
 * The function applies a visitor to an attribute value from the view. The user has to explicitly specify the
 * type or set of possible types of the attribute value to be visited.
 *
 * \param name The name of the attribute value to visit.
 * \param attrs A set of attribute values in which to look for the specified attribute value.
 * \param visitor A receiving function object to pass the attribute value to.
 * \return The result of visitation (see codes in the \c visitation_result class).
 */
template< typename T, typename CharT, typename VisitorT >
visitation_result visit(
    basic_attribute_name< CharT > const& name, basic_attribute_values_view< CharT > const& attrs, VisitorT visitor);

/*!
 * The function applies a visitor to an attribute value from the view. The user has to explicitly specify the
 * type or set of possible types of the attribute value to be visited.
 *
 * \param name The name of the attribute value to visit.
 * \param record A log record. The attribute value will be sought among those associated with the record.
 * \param visitor A receiving function object to pass the attribute value to.
 * \return The result of visitation (see codes in the \c visitation_result class).
 */
template< typename T, typename CharT, typename VisitorT >
visitation_result visit(
    basic_attribute_name< CharT > const& name, basic_record< CharT > const& record, VisitorT visitor);

#else // BOOST_LOG_DOXYGEN_PASS

#ifdef BOOST_LOG_USE_CHAR

template< typename T, typename VisitorT >
inline visitation_result visit(
    basic_attribute_name< char > const& name, basic_attribute_values_view< char > const& attrs, VisitorT visitor)
{
    value_visitor_invoker< char, T > invoker(name);
    return invoker(attrs, visitor);
}

template< typename T, typename VisitorT >
inline visitation_result visit(
    basic_attribute_name< char > const& name, basic_record< char > const& record, VisitorT visitor)
{
    value_visitor_invoker< char, T > invoker(name);
    return invoker(record, visitor);
}

#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T

template< typename T, typename VisitorT >
inline visitation_result visit(
    basic_attribute_name< wchar_t > const& name, basic_attribute_values_view< wchar_t > const& attrs, VisitorT visitor)
{
    value_visitor_invoker< wchar_t, T > invoker(name);
    return invoker(attrs, visitor);
}

template< typename T, typename VisitorT >
inline visitation_result visit(
    basic_attribute_name< wchar_t > const& name, basic_record< wchar_t > const& record, VisitorT visitor)
{
    value_visitor_invoker< wchar_t, T > invoker(name);
    return invoker(record, visitor);
}

#endif // BOOST_LOG_USE_WCHAR_T

#endif // BOOST_LOG_DOXYGEN_PASS

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTES_VALUE_VISITATION_HPP_INCLUDED_
