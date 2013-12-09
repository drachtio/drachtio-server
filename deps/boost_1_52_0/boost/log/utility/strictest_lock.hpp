/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   utility/strictest_lock.hpp
 * \author Andrey Semashev
 * \date   30.05.2010
 *
 * The header contains definition of the \c strictest_lock metafunction that
 * allows to select a lock with the strictest access requirements.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_UTILITY_STRICTEST_LOCK_HPP_INCLUDED_
#define BOOST_LOG_UTILITY_STRICTEST_LOCK_HPP_INCLUDED_

#include <boost/mpl/int.hpp>
#include <boost/mpl/less.hpp>
#include <boost/mpl/if.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/arithmetic/sub.hpp>
#include <boost/preprocessor/arithmetic/inc.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/repetition/enum_trailing.hpp>
#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/locks.hpp>
#include <boost/log/detail/pp_identity.hpp>

#ifndef BOOST_LOG_STRICTEST_LOCK_LIMIT
/*!
 * The macro defines the maximum number of template arguments that the \c strictest_lock
 * metafunction accepts. Should not be less than 2.
 */
#define BOOST_LOG_STRICTEST_LOCK_LIMIT 10
#endif // BOOST_LOG_STRICTEST_LOCK_LIMIT
#if BOOST_LOG_STRICTEST_LOCK_LIMIT < 2
#error The BOOST_LOG_STRICTEST_LOCK_LIMIT macro should not be less than 2
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

//! Access modes for different types of locks
enum lock_access_mode
{
    unlocked_access,    //!< A thread that owns this kind of lock doesn't restrict other threads in any way
    shared_access,      //!< A thread that owns this kind of lock requires that no other thread modify the locked data
    exclusive_access    //!< A thread that owns this kind of lock requires that no other thread has access to the locked data
};

//! The trait allows to select an access mode by the lock type
template< typename LockT >
struct thread_access_mode_of;

template< typename MutexT >
struct thread_access_mode_of< no_lock< MutexT > > : mpl::int_< unlocked_access >
{
};

#if !defined(BOOST_LOG_NO_THREADS)

template< typename MutexT >
struct thread_access_mode_of< lock_guard< MutexT > > : mpl::int_< exclusive_access >
{
};

template< typename MutexT >
struct thread_access_mode_of< unique_lock< MutexT > > : mpl::int_< exclusive_access >
{
};

template< typename MutexT >
struct thread_access_mode_of< shared_lock< MutexT > > : mpl::int_< shared_access >
{
};

template< typename MutexT >
struct thread_access_mode_of< upgrade_lock< MutexT > > : mpl::int_< shared_access >
{
};

template< typename MutexT >
struct thread_access_mode_of< boost::log::aux::exclusive_lock_guard< MutexT > > : mpl::int_< exclusive_access >
{
};

template< typename MutexT >
struct thread_access_mode_of< boost::log::aux::shared_lock_guard< MutexT > > : mpl::int_< shared_access >
{
};

#endif // !defined(BOOST_LOG_NO_THREADS)

namespace aux {

    //! The metafunction selects the most strict lock type of the two
    template< typename LeftLockT, typename RightLockT >
    struct strictest_lock_impl :
        mpl::if_<
            mpl::less< thread_access_mode_of< LeftLockT >, thread_access_mode_of< RightLockT > >,
            RightLockT,
            LeftLockT
        >
    {
    };

} // namespace aux

#if defined(BOOST_LOG_DOXYGEN_PASS)

/*!
 * \brief The metafunction selects the most strict lock type of the specified.
 *
 * The template supports all lock types provided by the Boost.Thread
 * library (except for \c upgrade_to_unique_lock), plus additional
 * pseudo-lock \c no_lock that indicates no locking at all.
 * Exclusive locks are considered the strictest, shared locks are weaker,
 * and \c no_lock is the weakest.
 */
template< typename... LocksT >
struct strictest_lock
{
    typedef implementation_defined type;
};

#else // defined(BOOST_LOG_DOXYGEN_PASS)

#   define BOOST_LOG_TYPE_INTERNAL(z, i, data) BOOST_PP_CAT(T, BOOST_PP_INC(i))

template<
    typename T,
    BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(BOOST_PP_DEC(BOOST_LOG_STRICTEST_LOCK_LIMIT), typename T, void)
>
struct strictest_lock
{
    typedef typename strictest_lock<
        typename boost::log::aux::strictest_lock_impl< T, T0 >::type
        BOOST_PP_ENUM_TRAILING(BOOST_PP_SUB(BOOST_LOG_STRICTEST_LOCK_LIMIT, 2), BOOST_LOG_TYPE_INTERNAL, ~)
    >::type type;
};

template< typename T >
struct strictest_lock<
    T
    BOOST_PP_ENUM_TRAILING(BOOST_PP_DEC(BOOST_LOG_STRICTEST_LOCK_LIMIT), BOOST_LOG_PP_IDENTITY, void)
>
{
    typedef T type;
};

#   undef BOOST_LOG_TYPE_INTERNAL

#endif // defined(BOOST_LOG_DOXYGEN_PASS)

} // namespace log

} // namespace boost

#endif // BOOST_LOG_UTILITY_STRICTEST_LOCK_HPP_INCLUDED_
