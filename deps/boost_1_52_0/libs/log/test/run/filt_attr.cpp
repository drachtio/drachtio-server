/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   filt_attr.cpp
 * \author Andrey Semashev
 * \date   31.01.2009
 *
 * \brief  This header contains tests for the \c attr filter.
 */

#define BOOST_TEST_MODULE filt_attr

#include <memory>
#include <string>
#include <algorithm>
#include <boost/regex.hpp>
#include <boost/function.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/attributes/attribute_values_view.hpp>
#include <boost/log/filters/attr.hpp>
#include <boost/log/utility/type_dispatch/standard_types.hpp>
#include <boost/log/support/regex.hpp>
#include "char_definitions.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace flt = logging::filters;

// The test checks that general conditions work
BOOST_AUTO_TEST_CASE_TEMPLATE(general_conditions, CharT, char_types)
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

    filter f = flt::attr< int >(data::attr1()) == 10;
    BOOST_CHECK(f(view1));

    f = flt::attr< int >(data::attr1()) < 0;
    BOOST_CHECK(!f(view1));

    f = flt::attr< float >(data::attr1()) > 0;
    BOOST_CHECK_THROW(f(view1), logging::runtime_error);
    f = flt::attr< float >(data::attr1(), std::nothrow) > 0;
    BOOST_CHECK(!f(view1));

    f = flt::attr< int >(data::attr4()) >= 1;
    BOOST_CHECK_THROW(f(view1), logging::runtime_error);
    f = flt::attr< int >(data::attr4(), std::nothrow) >= 1;
    BOOST_CHECK(!f(view1));

    f = flt::attr< int >(data::attr4(), std::nothrow) < 1;
    BOOST_CHECK(!f(view1));

    f = flt::attr< logging::numeric_types >(data::attr2()) > 5;
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr3()) == "Hello, world!";
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr3()) > "AAA";
    BOOST_CHECK(f(view1));

    // Check that strings are saved into the filter by value
    char buf[128];
    std::strcpy(buf, "Hello, world!");
    f = flt::attr< std::string >(data::attr3()) == buf;
    std::fill_n(buf, sizeof(buf), static_cast< char >(0));
    BOOST_CHECK(f(view1));

    std::strcpy(buf, "Hello, world!");
    f = flt::attr< std::string >(data::attr3()) == static_cast< const char* >(buf);
    std::fill_n(buf, sizeof(buf), static_cast< char >(0));
    BOOST_CHECK(f(view1));
}

// The test checks that is_in_range condition works
BOOST_AUTO_TEST_CASE_TEMPLATE(in_range_check, CharT, char_types)
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

    filter f = flt::attr< int >(data::attr1()).is_in_range(5, 20);
    BOOST_CHECK(f(view1));

    f = flt::attr< int >(data::attr1()).is_in_range(5, 10);
    BOOST_CHECK(!f(view1));

    f = flt::attr< int >(data::attr1()).is_in_range(10, 20);
    BOOST_CHECK(f(view1));

    f = flt::attr< logging::numeric_types >(data::attr2()).is_in_range(5, 6);
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr3()).is_in_range("AAA", "zzz");
    BOOST_CHECK(f(view1));

    // Check that strings are saved into the filter by value
    char buf1[128];
    char buf2[128];
    std::strcpy(buf1, "AAA");
    std::strcpy(buf2, "zzz");
    f = flt::attr< std::string >(data::attr3()).is_in_range(buf1, buf2);
    std::fill_n(buf1, sizeof(buf1), static_cast< char >(0));
    std::fill_n(buf2, sizeof(buf2), static_cast< char >(0));
    BOOST_CHECK(f(view1));

    std::strcpy(buf1, "AAA");
    std::strcpy(buf2, "zzz");
    f = flt::attr< std::string >(data::attr3())
        .is_in_range(static_cast< const char* >(buf1), static_cast< const char* >(buf2));
    std::fill_n(buf1, sizeof(buf1), static_cast< char >(0));
    std::fill_n(buf2, sizeof(buf2), static_cast< char >(0));
    BOOST_CHECK(f(view1));
}

namespace {

    struct predicate
    {
        typedef bool result_type;

        explicit predicate(unsigned int& call_counter, bool& result) :
            m_CallCounter(call_counter),
            m_Result(result)
        {
        }

        template< typename T >
        result_type operator() (T const&) const
        {
            ++m_CallCounter;
            return m_Result;
        }

    private:
        unsigned int& m_CallCounter;
        bool& m_Result;
    };

} // namespace

// The test checks that satisfies condition works
BOOST_AUTO_TEST_CASE_TEMPLATE(satisfies_check, CharT, char_types)
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

    unsigned int call_counter = 0;
    bool predicate_result = false;

    filter f = flt::attr< int >(data::attr1()).satisfies(predicate(call_counter, predicate_result));
    BOOST_CHECK_EQUAL(f(view1), predicate_result);
    BOOST_CHECK_EQUAL(call_counter, 1U);

    predicate_result = true;
    BOOST_CHECK_EQUAL(f(view1), predicate_result);
    BOOST_CHECK_EQUAL(call_counter, 2U);

    f = flt::attr< logging::numeric_types >(data::attr2()).satisfies(predicate(call_counter, predicate_result));
    BOOST_CHECK_EQUAL(f(view1), predicate_result);
    BOOST_CHECK_EQUAL(call_counter, 3U);

    f = flt::attr< int >(data::attr2()).satisfies(predicate(call_counter, predicate_result));
    BOOST_CHECK_THROW(f(view1), logging::runtime_error);
    f = flt::attr< int >(data::attr2(), std::nothrow).satisfies(predicate(call_counter, predicate_result));
    BOOST_CHECK_EQUAL(f(view1), false);
    BOOST_CHECK_EQUAL(call_counter, 3U);

    f = flt::attr< int >(data::attr4()).satisfies(predicate(call_counter, predicate_result));
    BOOST_CHECK_THROW(f(view1), logging::runtime_error);
    f = flt::attr< int >(data::attr4(), std::nothrow).satisfies(predicate(call_counter, predicate_result));
    BOOST_CHECK_EQUAL(f(view1), false);
    BOOST_CHECK_EQUAL(call_counter, 3U);
}

// The test checks that begins_with condition works
BOOST_AUTO_TEST_CASE_TEMPLATE(begins_with_check, CharT, char_types)
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

    filter f = flt::attr< std::string >(data::attr3()).begins_with("Hello");
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr3()).begins_with("hello");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr3()).begins_with("Bye");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr3()).begins_with("world!");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr2(), std::nothrow).begins_with("Hello");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr4(), std::nothrow).begins_with("Hello");
    BOOST_CHECK(!f(view1));
}

// The test checks that ends_with condition works
BOOST_AUTO_TEST_CASE_TEMPLATE(ends_with_check, CharT, char_types)
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

    filter f = flt::attr< std::string >(data::attr3()).ends_with("world!");
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr3()).ends_with("World!");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr3()).ends_with("Bye");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr3()).ends_with("Hello");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr2(), std::nothrow).ends_with("world!");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr4(), std::nothrow).ends_with("world!");
    BOOST_CHECK(!f(view1));
}

// The test checks that contains condition works
BOOST_AUTO_TEST_CASE_TEMPLATE(contains_check, CharT, char_types)
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

    filter f = flt::attr< std::string >(data::attr3()).contains("Hello");
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr3()).contains("hello");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr3()).contains("o, w");
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr3()).contains("world!");
    BOOST_CHECK(f(view1));

    f = flt::attr< std::string >(data::attr2(), std::nothrow).contains("Hello");
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr4(), std::nothrow).contains("Hello");
    BOOST_CHECK(!f(view1));
}

// The test checks that matches condition works
BOOST_AUTO_TEST_CASE_TEMPLATE(matches_check, CharT, char_types)
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

    boost::regex rex("hello");
    filter f = flt::attr< std::string >(data::attr3()).matches(rex);
    BOOST_CHECK(!f(view1));

    rex = ".*world.*";
    f = flt::attr< std::string >(data::attr3()).matches(rex);
    BOOST_CHECK(f(view1));

    rex = ".*";
    f = flt::attr< std::string >(data::attr2(), std::nothrow).matches(rex);
    BOOST_CHECK(!f(view1));

    f = flt::attr< std::string >(data::attr4(), std::nothrow).matches(rex);
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

    filter f =
        flt::attr< int >(data::attr1(), std::nothrow) <= 10 ||
        flt::attr< double >(data::attr2(), std::nothrow).is_in_range(2.2, 7.7);
    BOOST_CHECK(!f(view1));
    BOOST_CHECK(f(view2));
    BOOST_CHECK(f(view3));

    f = flt::attr< int >(data::attr1(), std::nothrow) == 10 &&
        flt::attr< std::string >(data::attr3(), std::nothrow).begins_with("Hello");
    BOOST_CHECK(!f(view1));
    BOOST_CHECK(!f(view2));
    BOOST_CHECK(f(view3));
}
