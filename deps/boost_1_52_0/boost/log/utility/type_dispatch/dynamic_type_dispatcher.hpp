/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   dynamic_type_dispatcher.hpp
 * \author Andrey Semashev
 * \date   15.04.2007
 *
 * The header contains implementation of the run-time type dispatcher.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DYNAMIC_TYPE_DISPATCHER_HPP_INCLUDED_
#define BOOST_LOG_DYNAMIC_TYPE_DISPATCHER_HPP_INCLUDED_

#include <new>
#include <memory>
#include <map>
#include <boost/ref.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/visible_type.hpp>
#include <boost/log/utility/type_info_wrapper.hpp>
#include <boost/log/utility/type_dispatch/type_dispatcher.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

/*!
 * \brief A dynamic type dispatcher
 *
 * The type dispatcher can be used to pass objects of arbitrary types from one
 * component to another. With regard to the library, the type dispatcher
 * can be used to extract attribute values.
 *
 * The dynamic type dispatcher can be initialized in run time and, therefore,
 * can support different types, depending on runtime conditions. Each
 * supported type is associated with a functional object that will be called
 * when an object of the type is dispatched.
 */
class dynamic_type_dispatcher :
    public type_dispatcher
{
private:
#ifndef BOOST_LOG_DOXYGEN_PASS
    template< typename T, typename VisitorT >
    class callback_impl :
        public callback_base
    {
    private:
        VisitorT m_Visitor;

    public:
        explicit callback_impl(VisitorT const& visitor) : m_Visitor(visitor)
        {
            this->m_pVisitor = (void*)boost::addressof(m_Visitor);
            typedef void (*trampoline_t)(void*, T const&);
            BOOST_STATIC_ASSERT(sizeof(trampoline_t) == sizeof(void*));
            union
            {
                void* as_pvoid;
                trampoline_t as_trampoline;
            }
            caster;
            caster.as_trampoline = &callback_base::trampoline< VisitorT, T >;
            this->m_pTrampoline = caster.as_pvoid;
        }
    };
#endif // BOOST_LOG_DOXYGEN_PASS

    //! The dispatching map
    typedef std::map< type_info_wrapper, shared_ptr< callback_base > > dispatching_map;
    dispatching_map m_DispatchingMap;

public:
    /*!
     * The method registers a new type
     *
     * \param visitor Function object that will be associated with the type \c T
     */
    template< typename T, typename VisitorT >
    void register_type(VisitorT const& visitor)
    {
        boost::shared_ptr< callback_base > p(
            boost::make_shared< callback_impl< T, VisitorT > >(boost::cref(visitor)));

        type_info_wrapper wrapper(typeid(aux::visible_type< T >));
        m_DispatchingMap[wrapper].swap(p);
    }

    /*!
     * The method returns the number of registered types
     */
    dispatching_map::size_type registered_types_count() const
    {
        return m_DispatchingMap.size();
    }

private:
#ifndef BOOST_LOG_DOXYGEN_PASS
    callback_base get_callback(std::type_info const& type)
    {
        type_info_wrapper wrapper(type);
        dispatching_map::iterator it = m_DispatchingMap.find(wrapper);
        if (it != m_DispatchingMap.end())
            return *it->second;
        else
            return callback_base();
    }
#endif // BOOST_LOG_DOXYGEN_PASS
};

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DYNAMIC_TYPE_DISPATCHER_HPP_INCLUDED_
