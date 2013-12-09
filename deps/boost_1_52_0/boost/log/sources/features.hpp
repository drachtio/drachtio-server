/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sources/features.hpp
 * \author Andrey Semashev
 * \date   17.07.2009
 *
 * The header contains definition of a features list class template.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_SOURCES_FEATURES_HPP_INCLUDED_
#define BOOST_LOG_SOURCES_FEATURES_HPP_INCLUDED_

#include <boost/preprocessor/repetition/enum.hpp>
#include <boost/preprocessor/repetition/enum_shifted_params.hpp>
#include <boost/preprocessor/repetition/enum_params_with_a_default.hpp>
#include <boost/mpl/lambda.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/pp_identity.hpp>

//! The macro defines the maximum number of features that can be specified for a logger
#ifndef BOOST_LOG_FEATURES_LIMIT
#define BOOST_LOG_FEATURES_LIMIT 10
#endif // BOOST_LOG_FEATURES_LIMIT

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace sources {

#if !defined(BOOST_LOG_DOXYGEN_PASS)

//! An MPL sequence of logger features
template< BOOST_PP_ENUM_PARAMS_WITH_A_DEFAULT(BOOST_LOG_FEATURES_LIMIT, typename FeatureT, void) >
struct features
{
    typedef features type;
    typedef FeatureT0 head;
    typedef features< BOOST_PP_ENUM_SHIFTED_PARAMS(BOOST_LOG_FEATURES_LIMIT, FeatureT) > tail;
};

#else // !defined(BOOST_LOG_DOXYGEN_PASS)

/*!
 * \brief An MPL sequence of logger features
 *
 * This class template can be used to specify logger features in a \c basic_composite_logger instantiation.
 * The resulting type is an MPL type sequence.
 */
template< typename... FeaturesT >
struct features;

#endif // !defined(BOOST_LOG_DOXYGEN_PASS)

namespace aux {

    //! The metafunction produces the inherited features hierarchy with \c RootT as the ultimate base type
    template< typename RootT, typename FeaturesT >
    struct inherit_features
    {
        typedef typename mpl::lambda<
            typename FeaturesT::head
        >::type::BOOST_NESTED_TEMPLATE apply<
            typename inherit_features<
                RootT,
                typename FeaturesT::tail
            >::type
        >::type type;
    };

    template< typename RootT >
    struct inherit_features<
        RootT,
        features< BOOST_PP_ENUM(BOOST_LOG_FEATURES_LIMIT, BOOST_LOG_PP_IDENTITY, void) >
    >
    {
        typedef RootT type;
    };

} // namespace aux

} // namespace sources

} // namespace log

} // namespace boost

#endif // BOOST_LOG_SOURCES_FEATURES_HPP_INCLUDED_
