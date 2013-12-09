/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attribute_value.hpp
 * \author Andrey Semashev
 * \date   21.05.2010
 *
 * The header contains methods of the \c attribute_value class. Use this header
 * to introduce the complete \c attribute_value implementation into your code.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_ATTRIBUTES_ATTRIBUTE_VALUE_HPP_INCLUDED_
#define BOOST_LOG_ATTRIBUTES_ATTRIBUTE_VALUE_HPP_INCLUDED_

#include <boost/mpl/eval_if.hpp>
#include <boost/mpl/identity.hpp>
#include <boost/mpl/is_sequence.hpp>
#include <boost/variant/variant_fwd.hpp>
#include <boost/optional/optional_fwd.hpp>
#include <boost/log/detail/prologue.hpp>
#include <boost/log/detail/functional.hpp>
#include <boost/log/attributes/attribute_value_def.hpp>
#include <boost/log/utility/type_dispatch/static_type_dispatcher.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

template< typename T >
struct attribute_value::result_of_extract
{
    typedef typename mpl::eval_if<
        mpl::is_sequence< T >,
        make_variant_over< T >,
        mpl::identity< T >
    >::type extracted_type;

    typedef optional< extracted_type > type;
};

template< typename T, typename VisitorT >
inline bool attribute_value::visit(VisitorT visitor) const
{
    static_type_dispatcher< T > disp(visitor);
    return this->dispatch(disp);
}

template< typename T >
inline typename attribute_value::result_of_extract< T >::type attribute_value::extract() const
{
    typedef typename result_of_extract< T >::type result_type;
    result_type res;
    this->BOOST_NESTED_TEMPLATE visit< T >(boost::log::aux::assign_fun< result_type >(res));
    return res;
}

} // namespace log

} // namespace boost

#endif // BOOST_LOG_ATTRIBUTES_ATTRIBUTE_VALUE_HPP_INCLUDED_
