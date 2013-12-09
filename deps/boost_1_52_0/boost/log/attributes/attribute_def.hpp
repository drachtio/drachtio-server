/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_def.hpp
 * \author Andrey Semashev
 * \date   15.04.2007
 *
 * The header contains attribute interface definition. For complete attribute definition,
 * include attribute.hpp.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_ATTRIBUTE_DEF_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_ATTRIBUTE_DEF_HPP_INCLUDED_

#include <boost/intrusive_ptr.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/utility/intrusive_ref_counter.hpp>
#include <boost/log/utility/explicit_operator_bool.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

#ifndef BOOST_LOG_DOXYGEN_PASS
class attribute_value;
#endif // BOOST_LOG_DOXYGEN_PASS

/*!
 * \brief A base class for an attribute value factory
 *
 * Every attribute is represented with a factory that is basically an attribute value generator.
 * The sole purpose of an attribute is to return an actual value when requested. A simplest attribute
 * can always return the same value that it stores internally, but more complex ones can
 * perform a considirable amount of work to return a value, and the returned values may differ
 * each time requested.
 *
 * A word about thread safety. An attribute should be prepared to be requested a value from
 * multiple threads concurrently.
 */
class attribute
{
public:
    /*!
     * \brief A base class for an attribute value factory
     *
     * All attributes must derive their implementation from this class.
     */
    struct BOOST_LOG_NO_VTABLE BOOST_LOG_VISIBLE impl :
        public intrusive_ref_counter
    {
        /*!
         * \return The actual attribute value. It shall not return empty values (exceptions
         *         shall be used to indicate errors).
         */
        virtual attribute_value get_value() = 0;
    };

private:
    //! Pointer to the attribute factory implementation
    intrusive_ptr< impl > m_pImpl;

public:
    /*!
     * Default constructor. Creates an empty attribute factory, which is not usable until
     * \c set_impl is called.
     */
    attribute() {}

    /*!
     * Initializing constructor
     *
     * \param p Pointer to the implementation. Must not be \c NULL.
     */
    explicit attribute(intrusive_ptr< impl > p) { m_pImpl.swap(p); }

    /*!
     * Verifies that the factory is not in empty state
     */
    BOOST_LOG_EXPLICIT_OPERATOR_BOOL()

    /*!
     * Verifies that the factory is in empty state
     */
    bool operator! () const { return !m_pImpl; }

    /*!
     * \return The actual attribute value. It shall not return empty values (exceptions
     *         shall be used to indicate errors).
     */
    attribute_value get_value() const;

    /*!
     * The method swaps two factories (i.e. their implementations).
     */
    void swap(attribute& that) { m_pImpl.swap(that.m_pImpl); }

protected:
    /*!
     * \returns The pointer to the implementation
     */
    impl* get_impl() const { return m_pImpl.get(); }
    /*!
     * Sets the pointer to the factory implementation.
     *
     * \param p Pointer to the implementation. Must not be \c NULL.
     */
    void set_impl(intrusive_ptr< impl > p) { m_pImpl.swap(p); }

    template< typename T >
    friend T attribute_cast(attribute const&);
};

/*!
 * The function swaps two attribute value factories
 */
inline void swap(attribute& left, attribute& right)
{
    left.swap(right);
}

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTES_ATTRIBUTE_DEF_HPP_INCLUDED_
