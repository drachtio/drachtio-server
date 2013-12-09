/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   default_filter_factory.hpp
 * \author Andrey Semashev
 * \date   29.05.2010
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <new> // std::nothrow

#if !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_SPIRIT_THREADSAFE)
#define BOOST_SPIRIT_THREADSAFE
#endif // !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_SPIRIT_THREADSAFE)

#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/spirit/include/classic_core.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/filters/basic_filters.hpp>
#include <boost/log/filters/attr.hpp>
#include <boost/log/filters/has_attr.hpp>
#include <boost/log/utility/type_dispatch/standard_types.hpp>
#include <boost/log/detail/functional.hpp>
#include <boost/log/support/xpressive.hpp>
#include <boost/xpressive/xpressive_dynamic.hpp>
#include "default_filter_factory.hpp"
#include "parser_utils.hpp"

namespace bsc = boost::spirit::classic;

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

//! The callback for equality relation filter
template< typename CharT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::on_equality_relation(string_type const& name, string_type const& arg)
{
    return parse_argument< log::aux::equal_to >(name, arg);
}

//! The callback for inequality relation filter
template< typename CharT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::on_inequality_relation(string_type const& name, string_type const& arg)
{
    return parse_argument< log::aux::not_equal_to >(name, arg);
}

//! The callback for less relation filter
template< typename CharT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::on_less_relation(string_type const& name, string_type const& arg)
{
    return parse_argument< log::aux::less >(name, arg);
}

//! The callback for greater relation filter
template< typename CharT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::on_greater_relation(string_type const& name, string_type const& arg)
{
    return parse_argument< log::aux::greater >(name, arg);
}

//! The callback for less or equal relation filter
template< typename CharT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::on_less_or_equal_relation(string_type const& name, string_type const& arg)
{
    return parse_argument< log::aux::less_equal >(name, arg);
}

//! The callback for greater or equal relation filter
template< typename CharT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::on_greater_or_equal_relation(string_type const& name, string_type const& arg)
{
    return parse_argument< log::aux::greater_equal >(name, arg);
}

//! The callback for custom relation filter
template< typename CharT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::on_custom_relation(string_type const& name, string_type const& rel, string_type const& arg)
{
    typedef log::aux::char_constants< char_type > constants;
    if (rel == constants::begins_with_keyword())
        return filter_type(log::filters::attr< string_type >(name).begins_with(arg));
    else if (rel == constants::ends_with_keyword())
        return filter_type(log::filters::attr< string_type >(name).ends_with(arg));
    else if (rel == constants::contains_keyword())
        return filter_type(log::filters::attr< string_type >(name).contains(arg));
    else if (rel == constants::matches_keyword())
    {
        typedef xpressive::basic_regex< typename string_type::const_iterator > regex_t;
        regex_t rex = regex_t::compile(arg, regex_t::ECMAScript | regex_t::optimize);
        return filter_type(log::filters::attr< string_type >(name, std::nothrow).matches(rex));
    }
    else
    {
        BOOST_LOG_THROW_DESCR(parse_error, "The custom attribute relation is not supported");
    }
}

//! The function parses the argument value for a binary relation and constructs the corresponding filter
template< typename CharT >
template< typename RelationT >
typename default_filter_factory< CharT >::filter_type
default_filter_factory< CharT >::parse_argument(string_type const& name, string_type const& arg)
{
    filter_type filter;
    const bool full = bsc::parse(arg.c_str(), arg.c_str() + arg.size(),
        (
            bsc::strict_real_p[boost::bind(&this_type::BOOST_NESTED_TEMPLATE on_fp_argument< RelationT >, boost::cref(name), _1, boost::ref(filter))] |
            bsc::int_p[boost::bind(&this_type::BOOST_NESTED_TEMPLATE on_integral_argument< RelationT >, boost::cref(name), _1, boost::ref(filter))] |
            (+bsc::print_p)[boost::bind(&this_type::BOOST_NESTED_TEMPLATE on_string_argument< RelationT >, boost::cref(name), _1, _2, boost::ref(filter))]
        )
    ).full;

    if (!full || filter.empty())
        BOOST_LOG_THROW_DESCR(parse_error, "Failed to parse relation operand");

    return filter;
}

template< typename CharT >
template< typename RelationT >
void default_filter_factory< CharT >::on_integral_argument(string_type const& name, long val, filter_type& filter)
{
    filter = log::filters::attr<
        log::integral_types
    >(name, std::nothrow).satisfies(log::aux::bind2nd(RelationT(), val));
}

template< typename CharT >
template< typename RelationT >
void default_filter_factory< CharT >::on_fp_argument(string_type const& name, double val, filter_type& filter)
{
    filter = log::filters::attr<
        log::floating_point_types
    >(name, std::nothrow).satisfies(log::aux::bind2nd(RelationT(), val));
}

template< typename CharT >
template< typename RelationT >
void default_filter_factory< CharT >::on_string_argument(
    string_type const& name, const char_type* b, const char_type* e, filter_type& filter)
{
    filter = log::filters::attr<
        string_type
    >(name, std::nothrow).satisfies(log::aux::bind2nd(RelationT(), string_type(b, e)));
}

//  Explicitly instantiate factory implementation
#ifdef BOOST_LOG_USE_CHAR
template class default_filter_factory< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class default_filter_factory< wchar_t >;
#endif

} // namespace aux

} // namespace log

} // namespace boost
