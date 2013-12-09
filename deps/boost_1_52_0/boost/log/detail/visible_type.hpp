/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   prologue.hpp
 * \author Andrey Semashev
 * \date   08.03.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html. In this file
 *         internal configuration macros are defined.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DETAIL_VISIBLE_TYPE_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_VISIBLE_TYPE_HPP_INCLUDED_

#include <boost/mpl/aux_/lambda_support.hpp>
#include <boost/log/detail/prologue.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

    //! The wrapper type whose type_info is always visible
    template< typename T >
    struct BOOST_LOG_VISIBLE visible_type
    {
        typedef T wrapped_type;

        BOOST_MPL_AUX_LAMBDA_SUPPORT(1, visible_type, (T))
    };

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_LOG_DETAIL_VISIBLE_TYPE_HPP_INCLUDED_
