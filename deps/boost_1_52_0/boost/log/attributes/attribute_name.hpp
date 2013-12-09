/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_name.hpp
 * \author Andrey Semashev
 * \date   28.06.2010
 *
 * The header contains attribute name interface definition.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTE_NAME_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTE_NAME_HPP_INCLUDED_

#include <iosfwd>
#include <string>
#include <boost/assert.hpp>
#include <boost/cstdint.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/utility/explicit_operator_bool.hpp>

#ifdef _MSC_VER
#pragma warning(push)
// 'm_A' : class 'A' needs to have dll-interface to be used by clients of class 'B'
#pragma warning(disable: 4251)
// non dll-interface class 'A' used as base for dll-interface class 'B'
#pragma warning(disable: 4275)
#endif // _MSC_VER

namespace boost {

namespace BOOST_LOG_NAMESPACE {

/*!
 * \brief The class represents an attribute name in containers used by the library
 *
 * The class mostly serves for optimization purposes. Each attribute name that is used
 * with the library is automatically associated with a unique identifier, which is much
 * lighter in terms of memory footprint and operations complexity. This is done
 * transparently by this class, on object construction. Passing objects of this class
 * to other library methods, such as attribute lookup functions, will not require
 * this thanslation and/or string copying and thus will result in a more efficient code.
 */
template< typename CharT >
class basic_attribute_name
{
public:
    //! Character type
    typedef CharT char_type;
    //! String type
    typedef std::basic_string< char_type > string_type;
#ifdef BOOST_LOG_DOXYGEN_PASS
    //! Associated identifier
    typedef unspecified id_type;
#else
    typedef uint32_t id_type;
    enum { uninitialized = 0xFFFFFFFF };
#endif

private:
    //! Associated identifier
    id_type m_id;

public:
    /*!
     * Default constructor. Creates an object that does not refer to any attribute name.
     */
    basic_attribute_name() : m_id(uninitialized)
    {
    }
    /*!
     * Constructs an attribute name from the specified string
     *
     * \param name An attribute name
     * \pre \a name is not NULL and points to a zero-terminated string
     */
    basic_attribute_name(const char_type* name) :
        m_id(get_id_from_string(name))
    {
    }
    /*!
     * Constructs an attribute name from the specified string
     *
     * \param name An attribute name
     */
    basic_attribute_name(string_type const& name) :
        m_id(get_id_from_string(name.c_str()))
    {
    }

    /*!
     * Compares the attribute names
     *
     * \return \c true if <tt>*this</tt> and \c that refer to the same attribute name,
     *         and \c false otherwise.
     */
    bool operator== (basic_attribute_name const& that) const { return m_id == that.m_id; }
    /*!
     * Compares the attribute names
     *
     * \return \c true if <tt>*this</tt> and \c that refer to different attribute names,
     *         and \c false otherwise.
     */
    bool operator!= (basic_attribute_name const& that) const { return m_id != that.m_id; }

    /*!
     * Checks if the object was default-constructed
     *
     * \return \c true if <tt>*this</tt> was constructed with an attribute name, \c false otherwise
     */
    BOOST_LOG_EXPLICIT_OPERATOR_BOOL()
    /*!
     * Checks if the object was default-constructed
     *
     * \return \c true if <tt>*this</tt> was default-constructed and does not refer to any attribute name,
     *         \c false otherwise
     */
    bool operator! () const { return (m_id == uninitialized); }

    /*!
     * \return The associated id value
     * \pre <tt>(!*this) == false</tt>
     */
    id_type id() const
    {
        BOOST_ASSERT(m_id != uninitialized);
        return m_id;
    }
    /*!
     * \return The attribute name string that was used during the object construction
     * \pre <tt>(!*this) == false</tt>
     */
    string_type const& string() const { return get_string_from_id(m_id); }

private:
#ifndef BOOST_LOG_DOXYGEN_PASS
    static BOOST_LOG_EXPORT id_type get_id_from_string(const char_type* name);
    static BOOST_LOG_EXPORT string_type const& get_string_from_id(id_type id);
#endif
};

#ifdef BOOST_LOG_USE_CHAR
typedef basic_attribute_name< char > attribute_name;        //!< Convenience typedef for narrow-character logging
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
typedef basic_attribute_name< wchar_t > wattribute_name;    //!< Convenience typedef for wide-character logging
#endif

template< typename CharT, typename TraitsT >
BOOST_LOG_EXPORT std::basic_ostream< CharT, TraitsT >& operator<< (
    std::basic_ostream< CharT, TraitsT >& strm,
    basic_attribute_name< CharT > const& name);

} // namespace log

} // namespace boost

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#endif // BOOST_LOG_ATTRIBUTE_NAME_HPP_INCLUDED_
