/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   type_dispatcher.hpp
 * \author Andrey Semashev
 * \date   15.04.2007
 *
 * The header contains definition of generic type dispatcher interfaces.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_TYPE_DISPATCHER_HPP_INCLUDED_
#define BOOST_LOG_TYPE_DISPATCHER_HPP_INCLUDED_

#include <typeinfo>
#include <boost/static_assert.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/visible_type.hpp>
#include <boost/log/utility/explicit_operator_bool.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

/*!
 * \brief A type dispatcher interface
 *
 * All type dispatchers support this interface. It is used to acquire the
 * visitor interface for the requested type.
 */
class BOOST_LOG_NO_VTABLE type_dispatcher
{
public:

#ifndef BOOST_LOG_DOXYGEN_PASS

    //! The base class for type dispatcher callbacks
    class callback_base
    {
    protected:
        void* m_pVisitor;
        void* m_pTrampoline;

    public:
        explicit callback_base(void* visitor = 0, void* tramp = 0) :
            m_pVisitor(visitor),
            m_pTrampoline(tramp)
        {
        }
        template< typename ValueT >
        explicit callback_base(void* visitor, void (*tramp)(void*, ValueT const&)) :
            m_pVisitor(visitor)
        {
            typedef void (*trampoline_t)(void*, ValueT const&);
            BOOST_STATIC_ASSERT(sizeof(trampoline_t) == sizeof(void*));
            union
            {
                void* as_pvoid;
                trampoline_t as_trampoline;
            }
            caster;
            caster.as_trampoline = tramp;
            m_pTrampoline = caster.as_pvoid;
        }

        template< typename VisitorT, typename T >
        static void trampoline(void* visitor, T const& value)
        {
            (*static_cast< VisitorT* >(visitor))(value);
        }
    };

    //! An interface to the callback for the concrete type visitor
    template< typename T >
    class callback :
        private callback_base
    {
    private:
        //! Type of the trampoline method
        typedef void (*trampoline_t)(void*, T const&);

    public:
        //! The type, which the visitor is able to consume
        typedef T supported_type;

    public:
        callback() : callback_base()
        {
        }
        explicit callback(callback_base const& base) : callback_base(base)
        {
        }

        void operator() (T const& value) const
        {
            BOOST_STATIC_ASSERT(sizeof(trampoline_t) == sizeof(void*));
            union
            {
                void* as_pvoid;
                trampoline_t as_trampoline;
            }
            caster;
            caster.as_pvoid = this->m_pTrampoline;
            (caster.as_trampoline)(this->m_pVisitor, value);
        }

        BOOST_LOG_EXPLICIT_OPERATOR_BOOL()

        bool operator! () const { return (this->m_pVisitor == 0); }
    };

#else // BOOST_LOG_DOXYGEN_PASS

    /*!
     * This interface is used by type dispatchers to consume the dispatched value.
     */
    template< typename T >
    class callback
    {
    public:
        /*!
         * The operator invokes the visitor-specific logic with the given value
         *
         * \param value The dispatched value
         */
        void operator() (T const& value) const;

        /*!
         * The operator checks if the visitor is attached to a receiver
         */
        BOOST_LOG_EXPLICIT_OPERATOR_BOOL()

        /*!
         * The operator checks if the visitor is not attached to a receiver
         */
        bool operator! () const;
    };

#endif // BOOST_LOG_DOXYGEN_PASS

public:
    /*!
     * Virtual destructor
     */
    virtual ~type_dispatcher() {}

    /*!
     * The method requests a callback for the value of type \c T
     *
     * \return The type-specific callback or an empty value, if the type is not supported
     */
    template< typename T >
    callback< T > get_callback()
    {
        return callback< T >(this->get_callback(
            typeid(boost::log::aux::visible_type< T >)));
    }

private:
#ifndef BOOST_LOG_DOXYGEN_PASS
    //! The get_callback method implementation
    virtual callback_base get_callback(std::type_info const& type) = 0;
#endif // BOOST_LOG_DOXYGEN_PASS
};

} // namespace log

} // namespace boost

#endif // BOOST_LOG_TYPE_DISPATCHER_HPP_INCLUDED_
