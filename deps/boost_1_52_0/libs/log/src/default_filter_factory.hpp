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

#ifndef BOOST_DEFAULT_FILTER_FACTORY_HPP_INCLUDED_
#define BOOST_DEFAULT_FILTER_FACTORY_HPP_INCLUDED_

#include <boost/log/utility/init/filter_parser.hpp>

namespace boost {

namespace BOOST_LOG_NAMESPACE {

namespace aux {

//! The default filter factory that supports creating filters for the standard types (see utility/type_dispatch/standard_types.hpp)
template< typename CharT >
class default_filter_factory :
    public filter_factory< CharT >
{
    //! Base type
    typedef filter_factory< CharT > base_type;
    //! Self type
    typedef default_filter_factory< CharT > this_type;

    //  Type imports
    typedef typename base_type::char_type char_type;
    typedef typename base_type::string_type string_type;
    typedef typename base_type::filter_type filter_type;

    //! The callback for equality relation filter
    virtual filter_type on_equality_relation(string_type const& name, string_type const& arg);
    //! The callback for inequality relation filter
    virtual filter_type on_inequality_relation(string_type const& name, string_type const& arg);
    //! The callback for less relation filter
    virtual filter_type on_less_relation(string_type const& name, string_type const& arg);
    //! The callback for greater relation filter
    virtual filter_type on_greater_relation(string_type const& name, string_type const& arg);
    //! The callback for less or equal relation filter
    virtual filter_type on_less_or_equal_relation(string_type const& name, string_type const& arg);
    //! The callback for greater or equal relation filter
    virtual filter_type on_greater_or_equal_relation(string_type const& name, string_type const& arg);

    //! The callback for custom relation filter
    virtual filter_type on_custom_relation(string_type const& name, string_type const& rel, string_type const& arg);

    //! The function parses the argument value for a binary relation and constructs the corresponding filter
    template< typename RelationT >
    static filter_type parse_argument(string_type const& name, string_type const& arg);

    template< typename RelationT >
    static void on_integral_argument(string_type const& name, long val, filter_type& filter);
    template< typename RelationT >
    static void on_fp_argument(string_type const& name, double val, filter_type& filter);
    template< typename RelationT >
    static void on_string_argument(string_type const& name, const char_type* b, const char_type* e, filter_type& filter);
};

} // namespace aux

} // namespace log

} // namespace boost

#endif // BOOST_DEFAULT_FILTER_FACTORY_HPP_INCLUDED_
