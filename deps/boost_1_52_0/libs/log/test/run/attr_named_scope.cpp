/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   attr_named_scope.cpp
 * \author Andrey Semashev
 * \date   25.01.2009
 *
 * \brief  This header contains tests for the named scope attribute.
 */

#define BOOST_TEST_MODULE attr_named_scope

#include <sstream>
#include <boost/optional.hpp>
#include <boost/mpl/vector.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/log/attributes/attribute.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/attributes/attribute_value.hpp>
#include <boost/log/utility/string_literal.hpp>
#include "char_definitions.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;

namespace {

    template< typename >
    struct scope_test_data;

#ifdef BOOST_LOG_USE_CHAR
    template< >
    struct scope_test_data< char >
    {
        static logging::string_literal scope1() { return logging::str_literal("scope1"); }
        static logging::string_literal scope2() { return logging::str_literal("scope2"); }
        static logging::string_literal file() { return logging::str_literal(__FILE__); }
    };
#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T
    template< >
    struct scope_test_data< wchar_t >
    {
        static logging::wstring_literal scope1() { return logging::str_literal(L"scope1"); }
        static logging::wstring_literal scope2() { return logging::str_literal(L"scope2"); }
        static logging::wstring_literal file() { return logging::str_literal(BOOST_PP_CAT(L, __FILE__)); }
    };
#endif // BOOST_LOG_USE_WCHAR_T

} // namespace

// The test verifies that the scope macros are defined
BOOST_AUTO_TEST_CASE(macros)
{
#ifdef BOOST_LOG_USE_CHAR
    BOOST_CHECK(BOOST_IS_DEFINED(BOOST_LOG_NAMED_SCOPE(name)));
    BOOST_CHECK(BOOST_IS_DEFINED(BOOST_LOG_FUNCTION()));
#endif // BOOST_LOG_USE_CHAR
#ifdef BOOST_LOG_USE_WCHAR_T
    BOOST_CHECK(BOOST_IS_DEFINED(BOOST_LOG_WNAMED_SCOPE(name)));
    BOOST_CHECK(BOOST_IS_DEFINED(BOOST_LOG_WFUNCTION()));
#endif // BOOST_LOG_USE_WCHAR_T
}

// The test checks that scope tracking works correctly
BOOST_AUTO_TEST_CASE_TEMPLATE(scope_tracking, CharT, char_types)
{
    typedef attrs::basic_named_scope< CharT > named_scope;
    typedef typename named_scope::sentry sentry;
    typedef attrs::basic_named_scope_list< CharT > scopes;
    typedef attrs::basic_named_scope_entry< CharT > scope;
    typedef scope_test_data< CharT > scope_data;

    named_scope attr;

    // First scope
    const unsigned int line1 = __LINE__;
    sentry scope1(scope_data::scope1(), scope_data::file(), line1);

    BOOST_CHECK(!named_scope::get_scopes().empty());
    BOOST_CHECK_EQUAL(named_scope::get_scopes().size(), 1UL);

    logging::attribute_value val = attr.get_value();
    BOOST_REQUIRE(!!val);

    boost::optional< scopes > sc = val.extract< scopes >();
    BOOST_REQUIRE(!!sc);
    BOOST_REQUIRE(!sc->empty());
    BOOST_CHECK_EQUAL(sc->size(), 1UL);

    scope const& s1 = sc->front();
    BOOST_CHECK(s1.scope_name == scope_data::scope1());
    BOOST_CHECK(s1.file_name == scope_data::file());
    BOOST_CHECK(s1.line == line1);

    // Second scope
    const unsigned int line2 = __LINE__;
    scope new_scope(scope_data::scope2(), scope_data::file(), line2);
    named_scope::push_scope(new_scope);

    BOOST_CHECK(!named_scope::get_scopes().empty());
    BOOST_CHECK_EQUAL(named_scope::get_scopes().size(), 2UL);

    val = attr.get_value();
    BOOST_REQUIRE(!!val);

    sc = val.extract< scopes >();
    BOOST_REQUIRE(!!sc);
    BOOST_REQUIRE(!sc->empty());
    BOOST_CHECK_EQUAL(sc->size(), 2UL);

    typename scopes::const_iterator it = sc->begin();
    scope const& s2 = *(it++);
    BOOST_CHECK(s2.scope_name == scope_data::scope1());
    BOOST_CHECK(s2.file_name == scope_data::file());
    BOOST_CHECK(s2.line == line1);

    scope const& s3 = *(it++);
    BOOST_CHECK(s3.scope_name == scope_data::scope2());
    BOOST_CHECK(s3.file_name == scope_data::file());
    BOOST_CHECK(s3.line == line2);

    BOOST_CHECK(it == sc->end());

    // Second scope goes out
    named_scope::pop_scope();

    BOOST_CHECK(!named_scope::get_scopes().empty());
    BOOST_CHECK_EQUAL(named_scope::get_scopes().size(), 1UL);

    val = attr.get_value();
    BOOST_REQUIRE(!!val);

    sc = val.extract< scopes >();
    BOOST_REQUIRE(!!sc);
    BOOST_REQUIRE(!sc->empty());
    BOOST_CHECK_EQUAL(sc->size(), 1UL);

    scope const& s4 = sc->back(); // should be the same as front
    BOOST_CHECK(s4.scope_name == scope_data::scope1());
    BOOST_CHECK(s4.file_name == scope_data::file());
    BOOST_CHECK(s4.line == line1);
}

// The test checks that detaching from thread works correctly
BOOST_AUTO_TEST_CASE_TEMPLATE(detaching_from_thread, CharT, char_types)
{
    typedef attrs::basic_named_scope< CharT > named_scope;
    typedef typename named_scope::sentry sentry;
    typedef attrs::basic_named_scope_list< CharT > scopes;
    typedef attrs::basic_named_scope_entry< CharT > scope;
    typedef scope_test_data< CharT > scope_data;

    named_scope attr;

    sentry scope1(scope_data::scope1(), scope_data::file(), __LINE__);
    logging::attribute_value val1 = attr.get_value();
    val1.detach_from_thread();

    sentry scope2(scope_data::scope2(), scope_data::file(), __LINE__);
    logging::attribute_value val2 = attr.get_value();
    val2.detach_from_thread();

    boost::optional< scopes > sc1 = val1.extract< scopes >(), sc2 = val2.extract< scopes >();
    BOOST_REQUIRE(!!sc1);
    BOOST_REQUIRE(!!sc2);
    BOOST_CHECK_EQUAL(sc1->size(), 1UL);
    BOOST_CHECK_EQUAL(sc2->size(), 2UL);
}

// The test checks that output streaming is possible
BOOST_AUTO_TEST_CASE_TEMPLATE(ostreaming, CharT, char_types)
{
    typedef attrs::basic_named_scope< CharT > named_scope;
    typedef typename named_scope::sentry sentry;
    typedef scope_test_data< CharT > scope_data;

    sentry scope1(scope_data::scope1(), scope_data::file(), __LINE__);
    sentry scope2(scope_data::scope2(), scope_data::file(), __LINE__);

    std::basic_ostringstream< CharT > strm;
    strm << named_scope::get_scopes();

    BOOST_CHECK(!strm.str().empty());
}

// The test checks that the scope list becomes thread-independent after copying
BOOST_AUTO_TEST_CASE_TEMPLATE(copying, CharT, char_types)
{
    typedef attrs::basic_named_scope< CharT > named_scope;
    typedef typename named_scope::sentry sentry;
    typedef attrs::basic_named_scope_list< CharT > scopes;
    typedef attrs::basic_named_scope_entry< CharT > scope;
    typedef scope_test_data< CharT > scope_data;

    sentry scope1(scope_data::scope1(), scope_data::file(), __LINE__);
    scopes sc = named_scope::get_scopes();
    sentry scope2(scope_data::scope2(), scope_data::file(), __LINE__);
    BOOST_CHECK_EQUAL(sc.size(), 1UL);
    BOOST_CHECK_EQUAL(named_scope::get_scopes().size(), 2UL);
}
