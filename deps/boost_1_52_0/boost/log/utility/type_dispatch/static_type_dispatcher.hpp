/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   static_type_dispatcher.hpp
 * \author Andrey Semashev
 * \date   15.04.2007
 *
 * The header contains implementation of a compile-time type dispatcher.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_STATIC_TYPE_DISPATCHER_HPP_INCLUDED_
#define BOOST_LOG_STATIC_TYPE_DISPATCHER_HPP_INCLUDED_

#include <utility>
#include <iterator>
#include <algorithm>
#include <boost/array.hpp>
#include <boost/static_assert.hpp>
#include <boost/compatibility/cpp_c_headers/cstddef>
#include <boost/mpl/if.hpp>
#include <boost/mpl/assert.hpp>
#include <boost/mpl/size.hpp>
#include <boost/mpl/for_each.hpp>
#include <boost/mpl/quote.hpp>
#include <boost/mpl/is_sequence.hpp>
#include <boost/utility/addressof.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/visible_type.hpp>
#include <boost/log/utility/once_block.hpp>
#include <boost/log/utility/type_info_wrapper.hpp>
#include <boost/log/utility/type_dispatch/type_dispatcher.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

//! Ordering predicate for type dispatching map
struct dispatching_map_order
{
    typedef bool result_type;
    typedef std::pair< type_info_wrapper, void* > first_argument_type, second_argument_type;
    bool operator() (first_argument_type const& left, second_argument_type const& right) const
    {
        return (left.first < right.first);
    }
};

//! Dispatching map filler
template< typename VisitorT >
struct dispatching_map_initializer
{
    typedef void result_type;

    explicit dispatching_map_initializer(std::pair< type_info_wrapper, void* >*& p) : m_p(p)
    {
    }

    template< typename T >
    void operator() (visible_type< T >) const
    {
        m_p->first = typeid(visible_type< T >);

        typedef void (*trampoline_t)(void*, T const&);
        BOOST_STATIC_ASSERT(sizeof(trampoline_t) == sizeof(void*));
        union
        {
            void* as_pvoid;
            trampoline_t as_trampoline;
        }
        caster;
        caster.as_trampoline = &type_dispatcher::callback_base::trampoline< VisitorT, T >;
        m_p->second = caster.as_pvoid;

        ++m_p;
    }

private:
    std::pair< type_info_wrapper, void* >*& m_p;
};

//! A dispatcher that supports a sequence of types
template< typename TypeSequenceT >
class type_sequence_dispatcher :
    public type_dispatcher
{
public:
    //! Type sequence of the supported types
    typedef TypeSequenceT supported_types;

private:
    //! The dispatching map
    typedef array<
        std::pair< type_info_wrapper, void* >,
        mpl::size< supported_types >::value
    > dispatching_map;

private:
    //! Pointer to the receiver function
    void* m_pVisitor;
    //! Pointer to the dispatching map
    dispatching_map const& m_DispatchingMap;

public:
    /*!
     * Constructor. Initializes the dispatcher internals.
     */
    template< typename VisitorT >
    explicit type_sequence_dispatcher(VisitorT& visitor) :
        m_pVisitor((void*)boost::addressof(visitor)),
        m_DispatchingMap(get_dispatching_map< VisitorT >())
    {
    }

private:
    //! The get_callback method implementation
    callback_base get_callback(std::type_info const& type)
    {
        type_info_wrapper wrapper(type);
        typename dispatching_map::value_type const* begin = &*m_DispatchingMap.begin();
        typename dispatching_map::value_type const* end = begin + dispatching_map::static_size;
        typename dispatching_map::value_type const* it =
            std::lower_bound(
                begin,
                end,
                std::make_pair(wrapper, (void*)0),
                dispatching_map_order()
            );

        if (it != end && it->first == wrapper)
            return callback_base(m_pVisitor, it->second);
        else
            return callback_base();
    }

    //! The method returns the dispatching map instance
    template< typename VisitorT >
    static dispatching_map const& get_dispatching_map()
    {
        static const dispatching_map* pinstance = NULL;

        BOOST_LOG_ONCE_BLOCK()
        {
            static dispatching_map instance;
            typename dispatching_map::value_type* p = &*instance.begin();

            mpl::for_each< supported_types, mpl::quote1< visible_type > >(
                dispatching_map_initializer< VisitorT >(p));

            std::sort(instance.begin(), instance.end(), dispatching_map_order());

            pinstance = &instance;
        }

        return *pinstance;
    }

private:
    //  Copying and assignment closed
    type_sequence_dispatcher(type_sequence_dispatcher const&);
    type_sequence_dispatcher& operator= (type_sequence_dispatcher const&);
};

//! A simple dispatcher that only supports one type
template< typename T >
class single_type_dispatcher :
    public type_dispatcher
{
private:
    //! A callback for the supported type
    callback_base m_Callback;

public:
    //! Constructor
    template< typename VisitorT >
    explicit single_type_dispatcher(VisitorT& visitor) :
        m_Callback(
            (void*)boost::addressof(visitor),
            &callback_base::trampoline< VisitorT, T >
        )
    {
    }
    //! The get_callback method implementation
    callback_base get_callback(std::type_info const& type)
    {
        if (type == typeid(visible_type< T >))
            return m_Callback;
        else
            return callback_base();
    }

private:
    //  Copying and assignment closed
    single_type_dispatcher(single_type_dispatcher const&);
    single_type_dispatcher& operator= (single_type_dispatcher const&);
};

} // namespace aux

/*!
 * \brief A static type dispatcher class
 *
 * The type dispatcher can be used to pass objects of arbitrary types from one
 * component to another. With regard to the library, the type dispatcher
 * can be used to extract attribute values.
 *
 * Static type dispatchers allow to specify one or several supported types at compile
 * time.
 */
template< typename T >
class static_type_dispatcher
#ifndef BOOST_LOG_DOXYGEN_PASS
    :
    public mpl::if_<
        mpl::is_sequence< T >,
        boost::log::aux::type_sequence_dispatcher< T >,
        boost::log::aux::single_type_dispatcher< T >
    >::type
#endif
{
private:
    //! Base type
    typedef typename mpl::if_<
        mpl::is_sequence< T >,
        boost::log::aux::type_sequence_dispatcher< T >,
        boost::log::aux::single_type_dispatcher< T >
    >::type base_type;

public:
    /*!
     * Constructor. Initializes the dispatcher internals.
     */
    template< typename ReceiverT >
    explicit static_type_dispatcher(ReceiverT& receiver) :
        base_type(receiver)
    {
    }
};

} // namespace log

} // namespace boost

#endif // BOOST_LOG_STATIC_TYPE_DISPATCHER_HPP_INCLUDED_
