/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sinks/frontend_requirements.hpp
 * \author Andrey Semashev
 * \date   22.04.2007
 *
 * The header contains definition of requirement tags that sink backend may declare
 * with regard to frontends. These requirements ensure that a backend will not
 * be used with an incompatible frontend.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SINKS_FRONTEND_REQUIREMENTS_HPP_INCLUDED_
#define BOOST_LOG_SINKS_FRONTEND_REQUIREMENTS_HPP_INCLUDED_

#include <boost/mpl/aux_/na.hpp>
#include <boost/mpl/placeholders.hpp>
#include <boost/mpl/inherit.hpp>
#include <boost/mpl/inherit_linearly.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/type_traits/is_base_of.hpp>
#include <boost/log/detail/prologue.hpp>

#ifndef BOOST_LOG_COMBINE_REQUIREMENTS_LIMIT
//! The macro specifies the maximum number of requirements that can be combined with the \c combine_requirements metafunction
#define BOOST_LOG_COMBINE_REQUIREMENTS_LIMIT 5
#endif

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sinks {

/*!
 * The sink backend expects pre-synchronized calls, all needed synchronization is implemented
 * in the frontend (IOW, only one thread is feeding records to the backend concurrently, but
 * is is possible for several threads to write sequentially). Note that if a frontend supports
 * synchronized record feeding, it will also report capable of concurrent record feeding.
 */
struct synchronized_feeding {};

#if !defined(BOOST_LOG_NO_THREADS)

/*!
 * The sink backend ensures all needed synchronization, it is capable to handle multithreaded calls
 */
struct concurrent_feeding : synchronized_feeding {};

#else // !defined(BOOST_LOG_NO_THREADS)

//  If multithreading is disabled, threading models become redundant
typedef synchronized_feeding concurrent_feeding;

#endif // !defined(BOOST_LOG_NO_THREADS)

/*!
 * The sink backend requires the frontend to perform log record formatting before feeding
 */
struct formatted_records {};

/*!
 * The sink backend supports flushing
 */
struct flushing {};

#ifdef BOOST_LOG_DOXYGEN_PASS

/*!
 * The metafunction combines multiple requirement tags into one type. The resulting type will
 * satisfy all specified requirements (i.e. \c has_requirement metafunction will return positive result).
 */
template< typename... RequirementsT >
struct combine_requirements;

#else

template< BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(BOOST_LOG_COMBINE_REQUIREMENTS_LIMIT, typename ReqT, mpl::na) >
struct combine_requirements :
    mpl::inherit_linearly<
        mpl::vector< BOOST_PP_ENUM_PARAMS(BOOST_LOG_COMBINE_REQUIREMENTS_LIMIT, ReqT) >,
        mpl::inherit2< mpl::_1, mpl::_2 >
    >
{
};

#endif // BOOST_LOG_DOXYGEN_PASS

//! A helper metafunction to check if a requirement is satisfied
template< typename TestedT, typename RequiredT >
struct has_requirement :
    public is_base_of< RequiredT, TestedT >
{
};

} // namespace sinks

} // namespace log

} // namespace boost

#endif // BOOST_LOG_SINKS_FRONTEND_REQUIREMENTS_HPP_INCLUDED_
