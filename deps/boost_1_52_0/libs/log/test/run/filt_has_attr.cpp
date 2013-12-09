/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   filt_has_attr.cpp
 * \author Andrey Semashev
 * \date   31.01.2009
 *
 * \brief  This header contains tests for the \c has_attr filter.
 */

#define BOOST_TEST_MODULE filt_has_attr

#include <string>
#include <boost/function.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>
#include <boost/log/filters/has_attr.hpp>
#include "char_definitions.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace flt = logging::filters;

// The test checks that the filter detects the attribute value presence
BOOST_AUTO_TEST_CASE_TEMPLATE(presence_check, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef logging::basic_attribute_values_view< CharT > values_view;
    typedef boost::function< bool (values_view const&) > filter;
    typedef test_data< CharT > data;

    attrs::constant< int > attr1(10);
    attrs::constant< double > attr2(5.5);
    attrs::constant< std::string > attr3("Hello, world!");

    attr_set set1, set2, set3;
    set1[data::attr1()] = attr1;
    set1[data::attr2()] = attr2;
    set1[data::attr3()] = attr3;

    values_view view1(set1, set2, set3);
    view1.freeze();

    filter f = flt::has_attr(data::attr1());
    BOOST_CHECK(f(view1));

    f = flt::has_attr(data::attr4());
    BOOST_CHECK(!f(view1));

    f = flt::has_attr< double >(data::attr2());
    BOOST_CHECK(f(view1));

    f = flt::has_attr< double >(data::attr3());
    BOOST_CHECK(!f(view1));

    typedef mpl::vector< unsigned int, char, std::string >::type value_types;
    f = flt::has_attr< value_types >(data::attr3());
    BOOST_CHECK(f(view1));

    f = flt::has_attr< value_types >(data::attr1());
    BOOST_CHECK(!f(view1));
}

// The test checks that the filter composition works
BOOST_AUTO_TEST_CASE_TEMPLATE(composition_check, CharT, char_types)
{
    typedef logging::basic_attribute_set< CharT > attr_set;
    typedef logging::basic_attribute_values_view< CharT > values_view;
    typedef boost::function< bool (values_view const&) > filter;
    typedef test_data< CharT > data;

    attrs::constant< int > attr1(10);
    attrs::constant< double > attr2(5.5);
    attrs::constant< std::string > attr3("Hello, world!");

    attr_set set1, set2, set3;
    values_view view1(set1, set2, set3);
    view1.freeze();
    set1[data::attr2()] = attr2;
    values_view view2(set1, set2, set3);
    view2.freeze();
    set1[data::attr3()] = attr3;
    set1[data::attr1()] = attr1;
    values_view view3(set1, set2, set3);
    view3.freeze();

    filter f = flt::has_attr(data::attr1()) || flt::has_attr< double >(data::attr2());
    BOOST_CHECK(!f(view1));
    BOOST_CHECK(f(view2));
    BOOST_CHECK(f(view3));

    f = flt::has_attr(data::attr1()) && flt::has_attr< double >(data::attr2());
    BOOST_CHECK(!f(view1));
    BOOST_CHECK(!f(view2));
    BOOST_CHECK(f(view3));
}
