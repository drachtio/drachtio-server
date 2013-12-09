/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_value_def.hpp
 * \author Andrey Semashev
 * \date   21.05.2010
 *
 * The header contains \c attribute_value class definition. In order to actually
 * use the class, include <tt>attribute_value.hpp</tt>.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTE_VALUE_DEF_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTE_VALUE_DEF_HPP_INCLUDED_

#include <boost/intrusive_ptr.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/utility/explicit_operator_bool.hpp>
#include <boost/log/utility/intrusive_ref_counter.hpp>
#include <boost/log/utility/type_dispatch/type_dispatcher.hpp>
#include <boost/log/attributes/attribute_def.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

/*!
 * \brief An attribute value class
 *
 * An attribute value is an object that contains a piece of data that represents an attribute state
 * at the point of the value acquision. All major operations with log records, such as filtering and
 * formatting, involve attribute values contained in a single view. Most likely an attribute value is
 * implemented as a simple holder of some typed value. This holder implements the
 * \c attribute_value::implementation interface and acts as a pimpl for the \c attribute_value
 * object. The \c attribute_value class provides type dispatching support in order to allow
 * to extract the value from the holder.
 *
 * Normally, attributes and their values shall be designed in order to exclude as much interference as
 * reasonable. Such approach allows to have more than one attribute value simultaneously, which improves
 * scalability and allows to implement generating attributes.
 *
 * However, there are cases when this approach does not help to achieve the required level of independency
 * of attribute values and attribute itself from each other at a reasonable performance tradeoff.
 * For example, an attribute or its values may use thread-specific data, which is global and shared
 * between all the instances of the attribute/value. Passing such an attribute value to another thread
 * would be a disaster. To solve this the library defines an additional method for attribute values,
 * namely \c detach_from_thread. The \c attribute_value class forwards the call to its pimpl,
 * which is supposed to ensure that it no longer refers to any thread-specific data after the call.
 * The pimpl can create a new holder as a result of this method and return it to the \c attribute_value
 * wrapper, which will keep the returned reference for any further calls.
 * This method is called for all attribute values that are passed to another thread.
 */
class attribute_value
{
public:
    /*!
     * \brief A base class for an attribute value implementation
     *
     * All attribute value holders should derive from this interface.
     */
    struct BOOST_LOG_NO_VTABLE impl :
        public attribute::impl
    {
    public:
        /*!
         * The method dispatches the value to the given object.
         *
         * \param dispatcher The object that attempts to dispatch the stored value.
         * \return true if \a dispatcher was capable to consume the real attribute value type and false otherwise.
         */
        virtual bool dispatch(type_dispatcher& dispatcher) = 0;

        /*!
         * The method is called when the attribute value is passed to another thread (e.g.
         * in case of asynchronous logging). The value should ensure it properly owns all thread-specific data.
         *
         * \return An actual pointer to the attribute value. It may either point to this object or another.
         *         In the latter case the returned pointer replaces the pointer used by caller to invoke this
         *         method and is considered to be a functional equivalent to the previous pointer.
         */
        virtual intrusive_ptr< impl > detach_from_thread()
        {
            return this;
        }

        /*!
         * \return The attribute value that refers to self implementation.
         */
        virtual attribute_value get_value() { return attribute_value(this); }
    };

    /*!
     * \brief A metafunction that allows to acquire the result of the value extraction
     *
     * The metafunction results in a type that is in form of <tt>optional< T ></tt>, if \c T is
     * not a MPL type sequence, or <tt>optional< variant< T1, T2, ... > ></tt> otherwise, with
     * \c T1, \c T2, etc. being the types comprising the type sequence \c T.
     */
    template< typename T >
    struct result_of_extract;

private:
    //! Pointer to the value implementation
    intrusive_ptr< impl > m_pImpl;

public:
    /*!
     * Default constructor. Creates an empty (absent) attribute value.
     */
    attribute_value() {}
    /*!
     * Initializing constructor. Creates an attribute value that refers to the specified holder.
     *
     * \param p A pointer to the attribute value holder.
     */
    explicit attribute_value(intrusive_ptr< impl > p) { m_pImpl.swap(p); }

    /*!
     * The operator checks if the attribute value is empty
     */
    BOOST_LOG_EXPLICIT_OPERATOR_BOOL()
    /*!
     * The operator checks if the attribute value is empty
     */
    bool operator! () const { return !m_pImpl; }

    /*!
     * The method is called when the attribute value is passed to another thread (e.g.
     * in case of asynchronous logging). The value should ensure it properly owns all thread-specific data.
     *
     * \post The attribute value no longer refers to any thread-specific resources.
     */
    void detach_from_thread()
    {
        if (m_pImpl)
            m_pImpl->detach_from_thread().swap(m_pImpl);
    }

    /*!
     * The method dispatches the value to the given object.
     *
     * \param dispatcher The object that attempts to dispatch the stored value.
     * \return \c true if the value is not empty and the \a dispatcher was capable to consume
     *         the real attribute value type and \c false otherwise.
     */
    bool dispatch(type_dispatcher& dispatcher) const
    {
        if (m_pImpl)
            return m_pImpl->dispatch(dispatcher);
        else
            return false;
    }

    /*!
     * The method attempts to extract the stored value, assuming the value has the specified type.
     * One can specify either a single type or a MPL type sequence, in which case the stored value
     * is checked against every type in the sequence.
     *
     * \return The extracted value, if the attribute value is not empty and the value is the same
     *         as specified. Otherwise returns an empty value. See description of the \c result_of_extract
     *         metafunction for information on the nature of the result value.
     */
    template< typename T >
    typename result_of_extract< T >::type extract() const;

    /*!
     * The method attempts to extract the stored value, assuming the value has the specified type,
     * and pass it to the \a visitor function object.
     * One can specify either a single type or a MPL type sequence, in which case the stored value
     * is checked against every type in the sequence.
     *
     * \param visitor A function object that will be invoked on the extracted attribute value.
     *                The visitor should be capable to be called with a single argument of
     *                any type of the specified types in \c T.
     *
     * \return \c true, if the stored value has the requested type and had been passed to the \a visitor.
     *         Otherwise the method returns \c false.
     */
    template< typename T, typename VisitorT >
    bool visit(VisitorT visitor) const;

    /*!
     * The method swaps two attribute values
     */
    void swap(attribute_value& that)
    {
        m_pImpl.swap(that.m_pImpl);
    }
};

/*!
 * The function swaps two attribute values
 */
inline void swap(attribute_value& left, attribute_value& right)
{
    left.swap(right);
}

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTE_VALUE_DEF_HPP_INCLUDED_
